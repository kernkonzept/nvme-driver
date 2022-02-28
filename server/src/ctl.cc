/*
 * Copyright (C) 2020 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */

#include <l4/re/env>
#include <l4/re/dataspace>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>

#include <string>

#include <l4/vbus/vbus>
#include <l4/vbus/vbus_pci>
#include <l4/vbus/vbus_interfaces.h>
#include <cstring>

#include "ctl.h"
#include "inout_buffer.h"
#include "ns.h"
#include "debug.h"
#include "queue.h"

static Dbg trace(Dbg::Trace, "ctl");

namespace Nvme {

bool Ctl::use_sgls = false;

Ctl::Ctl(L4vbus::Pci_dev const &dev,
         L4Re::Util::Shared_cap<L4Re::Dma_space> const &dma)
: _dev(dev),
  _dma(dma),
  _iomem(cfg_read_bar(),
         L4::cap_reinterpret_cast<L4Re::Dataspace>(_dev.bus_cap())),
  _regs(new L4drivers::Mmio_register_block<32>(_iomem.vaddr.get())),
  _cap(_regs.r<32>(Regs::Ctl::Cap).read()
       | ((l4_uint64_t)_regs.r<32>(Regs::Ctl::Cap + 4).read() << 32)),
  _sgls(false)
{
  trace.printf("Device registers 0%llx @ 0%lx, CAP=%llx, VS=%x\n",
               cfg_read_bar(), _iomem.vaddr.get(), _cap.raw,
               _regs.r<32>(Regs::Ctl::Vs).read());

  if (_cap.css() & 1)
    trace.printf("Controller supports NVM command set\n");
  else
    L4Re::chksys(-L4_ENOSYS, "Controller does not support NVM command set");

  // Start by resetting the controller, mostly to get the admin queue doorbell
  // registers to a known state.
  Ctl_cc cc(0);
  _regs.r<32>(Regs::Ctl::Cc).write(cc.raw);

  // Set the admin queues' sizes
  Ctl_aqa aqa(0);
  aqa.acqs() = 1; // 2 entries
  aqa.asqs() = 1; // 2 entries
  _regs.r<32>(Regs::Ctl::Aqa).write(aqa.raw);

  // Allocate the admin queues
  _acq = cxx::make_unique<Queue::Completion_queue>(aqa.acqs() + 1, Aq_id,
                                                   _cap.dstrd(), _regs, _dma);
  _asq = cxx::make_unique<Queue::Submission_queue>(aqa.asqs() + 1, Aq_id,
                                                   _cap.dstrd(), _regs, _dma);
  // Write the queues' addresses to the controller
  _regs.r<32>(Regs::Ctl::Acq).write(_acq->phys_base() & 0xffffffffUL);
  _regs.r<32>(Regs::Ctl::Acq + 4).write((l4_uint64_t)_acq->phys_base() >> 32U);
  _regs.r<32>(Regs::Ctl::Asq).write(_asq->phys_base() & 0xffffffffUL);
  _regs.r<32>(Regs::Ctl::Asq + 4).write((l4_uint64_t)_asq->phys_base() >> 32U);

  // Configure the IO queue entry sizes
  //
  // The specification says these must be set before creating IO queues, so not
  // required when enabling the controller. However, QEMU 5.0 insists on these
  // being set at least to the minimal allowed values, otherwise it fails to
  // enable the controller.
  cc.iocqes() = 4; // 16 bytes
  cc.iosqes() = 6; // 64 bytes

  cc.ams() = Regs::Ctl::Cc::Ams_rr;
  cc.mps() = L4_PAGESHIFT - 12;
  if ((_cap.mpsmin() > cc.mps()) || (_cap.mpsmax() < cc.mps()))
    L4Re::chksys(-L4_ENOSYS, "Controller does not support the architectural page size");

  cc.css() = Regs::Ctl::Cc::Css_nvm;
  cc.en() = 1;
  _regs.r<32>(Regs::Ctl::Cc).write(cc.raw);

  trace.printf("Waiting for the controller to become ready...\n");
  while (!Ctl_csts(_regs.r<32>(Regs::Ctl::Csts).read()).rdy())
    ;
  trace.printf("done.\n");

  l4_uint16_t cmd = cfg_read_16(0x04);
  if (!(cmd & 4))
    {
      trace.printf("Enabling PCI bus master\n");
      cfg_write_16(0x04, cmd | 4);
    }
}

void
Ctl::handle_irq()
{
  Queue::Cqe *cqe = _acq->consume();
  if (cqe)
    {
      assert(cqe->sqid() == Aq_id);
      _asq->_head = cqe->sqhd();
      assert(_asq->_callbacks[cqe->cid()]);
      auto cb = _asq->_callbacks[cqe->cid()];
      _asq->_callbacks[cqe->cid()] = nullptr;
      cb(cqe->sf());
      _acq->complete();
    }

  for (auto &ns: _nss)
    ns->handle_irq();

  if (!_irq_trigger_type)
    obj_cap()->unmask();
}


void
Ctl::register_interrupt_handler(L4::Cap<L4::Icu> icu,
                                L4Re::Util::Object_registry *registry)
{
  // find the interrupt
  unsigned char polarity;
  int irq = L4Re::chksys(_dev.irq_enable(&_irq_trigger_type, &polarity),
                         "Enabling interrupt.");

  Dbg::info().printf("Device: interrupt : %d trigger: %d, polarity: %d\n",
                     irq, (int)_irq_trigger_type, (int)polarity);
  trace.printf("Device: interrupt mask: %x\n",
               _regs.r<32>(Regs::Ctl::Intms).read());

  _regs.r<32>(Regs::Ctl::Intms).write(~0U);

  trace.printf("Registering server with registry....\n");
  auto cap = L4Re::chkcap(registry->register_irq_obj(this),
                          "Registering IRQ server object.");

  trace.printf("Binding interrupt %d...\n", irq);
  L4Re::chksys(l4_error(icu->bind(irq, cap)), "Binding interrupt to ICU.");

  trace.printf("Unmasking interrupt...\n");
  L4Re::chksys(l4_ipc_error(cap->unmask(), l4_utcb()),
               "Unmasking interrupt");

  trace.printf("Enabling Ctl interrupts...\n");
  _regs.r<32>(Regs::Ctl::Intmc).write(~0U);

  trace.printf("Attached to interupt %d\n", irq);
}

cxx::unique_ptr<Queue::Completion_queue>
Ctl::create_iocq(l4_uint16_t id, l4_size_t size, Callback cb)
{
  auto cq = cxx::make_unique<Queue::Completion_queue>(size, id, _cap.dstrd(),
                                                      _regs, _dma);
  auto *sqe = _asq->produce();
  sqe->opc() = Acs::Create_iocq;
  sqe->nsid = 0;
  sqe->psdt() = Psdt::Use_prps;
  sqe->prp.prp1 = cq->phys_base();
  sqe->prp.prp2 = 0;
  sqe->qid() = id;
  sqe->qsize() = cq->size() - 1;
  sqe->ien() = 1;
  sqe->pc() = 1;
  _asq->_callbacks[sqe->cid()] = cb;
  _asq->submit();

  return cq;
}

cxx::unique_ptr<Queue::Submission_queue>
Ctl::create_iosq(l4_uint16_t id, l4_size_t size, l4_size_t sgls, Callback cb)
{
  auto sq = cxx::make_unique<Queue::Submission_queue>(size, id, _cap.dstrd(),
                                                      _regs, _dma, sgls);

  auto *sqe = _asq->produce();
  sqe->opc() = Acs::Create_iosq;
  sqe->nsid = 0;
  sqe->psdt() = Psdt::Use_prps;
  sqe->prp.prp1 = sq->phys_base();
  sqe->prp.prp2 = 0;
  sqe->qid() = id;
  sqe->qsize() = sq->size() - 1;
  sqe->pc() = 1;
  sqe->cqid() = id;
  sqe->cdw12 = 0;
  _asq->_callbacks[sqe->cid()] = cb;
  _asq->submit();

  return sq;
}

void
Ctl::identify_namespace(l4_uint32_t nn, l4_uint32_t n,
                        std::function<void(cxx::unique_ptr<Namespace>)> callback)
{
  auto in =
    cxx::make_ref_obj<Inout_buffer>(4096, _dma,
                                    L4Re::Dma_space::Direction::From_device);

  // Note that the admin queues have the smallest possible size, so that at any
  // one time, there can be at most one admin queue command in progress. This
  // prevents us from using a for-loop for identifying possibly many
  // namespaces.  We workaround that by implementing the for-loop within the
  // nesting structure of the callbacks.

  auto *sqe = _asq->produce();
  sqe->opc() = Acs::Identify;
  sqe->nsid = n;
  sqe->psdt() = Psdt::Use_prps;
  sqe->prp.prp1 = in->pget();
  sqe->prp.prp2 = 0;
  sqe->cntid() = 0;
  sqe->cns() = Cns::Identify_namespace;
  sqe->nvmsetid() = 0;

  auto cb = [=](l4_uint16_t status) {
    if (status)
      {
        printf("Namespace Identify command failed with status %u\n", status);
        return;
      }

    l4_uint64_t nsze = *in->get<l4_uint64_t>(Cns_in::Nsze);
    l4_uint64_t ncap = *in->get<l4_uint64_t>(Cns_in::Ncap);
    l4_uint64_t nuse = *in->get<l4_uint64_t>(Cns_in::Nuse);
    trace.printf("Namespace nsze=%llu, ncap=%llu, nuse=%llu\n", nsze, ncap, nuse);

    l4_uint8_t nlbaf = *in->get<l4_uint8_t>(Cns_in::Nlbaf);
    l4_uint8_t flbas = *in->get<l4_uint8_t>(Cns_in::Flbas);

    trace.printf("Number of LBA formats: %u, formatted LBA size: %u\n",
                 nlbaf + 1, flbas);

    bool skipped = true;
    if (nsze)
      {
        if ((flbas & 0xf) <= nlbaf)
          {
            l4_uint32_t lbaf =
              *in->get<l4_uint32_t>(Cns_in::Lbaf0 + (flbas & 0xf) * 4);
            if ((lbaf & 0xffffu) == 0)
              {
                l4_size_t lba_sz = (1ULL << ((lbaf >> 16) & 0xffu));
                trace.printf("LBA size: %zu\n", lba_sz);

                skipped = false;
                auto ns =
                  cxx::make_unique<Nvme::Namespace>(*this, n, lba_sz, in);
                ns.release()->async_loop_init(nn, callback);
              }
            else
              trace.printf("LBAF uses metadata, skipping namespace %u\n", n);
          }
        else
          trace.printf("Invalid TLBAS, skipping namespace %u\n", n);
      }
    else
      trace.printf("Skipping non-active namespace %u\n", n);

    in->unmap();

    if (skipped && n + 1 < nn)
      identify_namespace(nn, n + 1, callback);
  };

  _asq->_callbacks[sqe->cid()] = cb;
  _asq->submit();
}

void
Ctl::identify(std::function<void(cxx::unique_ptr<Namespace>)> callback)
{
  auto ic =
    cxx::make_ref_obj<Inout_buffer>(4096, _dma,
                                    L4Re::Dma_space::Direction::From_device);

  auto *sqe = _asq->produce();
  sqe->opc() = Acs::Identify;
  sqe->psdt() = Psdt::Use_prps;
  sqe->prp.prp1 = ic->pget();
  sqe->prp.prp2 = 0;
  sqe->cntid() = 0;
  sqe->cns() = Cns::Identify_controller;
  sqe->nvmsetid() = 0;

  auto cb = [=](l4_uint16_t status) {
    if (status)
      {
        trace.printf("Identify_controller command failed with status=%u\n", status);
        return;
      }

    _sn = std::string(ic->get<char>(Cns_ic::Sn), 20);
    _sn.erase(_sn.find(' '));

    printf("Serial Number: %s\n", _sn.c_str());
    printf("Model Number: %.40s\n", ic->get<char>(Cns_ic::Mn));
    printf("Firmware Revision: %.8s\n", ic->get<char>(Cns_ic::Fr));

    printf("Controller ID: %x\n", *ic->get<l4_uint16_t>(Cns_ic::Cntlid));

    _sgls = (*ic->get<l4_uint32_t>(Cns_ic::Sgls) & 0x3) != 0;
    printf("SGL Support: %s\n", _sgls ? "yes" : "no");

    l4_uint32_t nn = *ic->get<l4_uint32_t>(Cns_ic::Nn);

    printf("Number of Namespaces: %d\n", nn);

    ic->unmap();

    // Identify all namespaces
    //
    // Note this is done as an asynchronous for-loop because we keep the
    // size of the admin queue as small as possible.
    identify_namespace(nn, 1, callback);
  };

  _asq->_callbacks[sqe->cid()] = cb;
  _asq->submit();
}

bool
Ctl::is_nvme_ctl(L4vbus::Device const &dev, l4vbus_device_t const &dev_info)
{
  if (!l4vbus_subinterface_supported(dev_info.type, L4VBUS_INTERFACE_PCIDEV))
    return false;

  L4vbus::Pci_dev const &pdev = static_cast<L4vbus::Pci_dev const &>(dev);
  l4_uint32_t val = 0;
  if (pdev.cfg_read(0, &val, 32) != L4_EOK)
    return false;

  // seems to be a PCI device
  trace.printf("Found PCI Device. Vendor 0x%x\n", val);
  L4Re::chksys(pdev.cfg_read(8, &val, 32));

  l4_uint32_t class_code = val >> 8;

  // class    = 01 (mass storage controller)
  // subclass = 08 (non-volatile memory controller)
  // prgif    = 02 (NVMe)
  return (class_code == 0x10802);
}

}
