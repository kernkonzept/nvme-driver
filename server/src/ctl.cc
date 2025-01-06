/*
 * Copyright (C) 2020, 2022-2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
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

#include "pci.h"
#include "icu.h"

#include <l4/util/util.h>

static Dbg trace(Dbg::Trace, "ctl");
static Dbg warn(Dbg::Warn, "ctl");

namespace Nvme {

bool Ctl::use_sgls = true;
bool Ctl::use_msis = true;
bool Ctl::use_msixs = true;

Ctl::Ctl(L4vbus::Pci_dev const &dev, cxx::Ref_ptr<Icu> icu,
         L4Re::Util::Object_registry *registry,
         L4Re::Util::Shared_cap<L4Re::Dma_space> const &dma)
: _dev(dev),
  _pci_dev(cxx::make_unique<Nvme::Pci_dev>(dev)),
  _icu(icu),
  _registry(registry),
  _dma(dma),
  _iomem(cfg_read_bar(), Regs::Ctl::Sq0tdbl + 1,
         L4::cap_reinterpret_cast<L4Re::Dataspace>(_dev.bus_cap())),
  _regs(new L4drivers::Mmio_register_block<32>(_iomem.vaddr.get())),
  _cap(_regs.r<32>(Regs::Ctl::Cap).read()
       | ((l4_uint64_t)_regs.r<32>(Regs::Ctl::Cap + 4).read() << 32)),
  _sgls(false)
{
  trace.printf("Device registers 0%llx @ 0%lx, CAP=%llx, VS=%x\n",
               cfg_read_bar(), _iomem.vaddr.get(), _cap.raw,
               _regs.r<32>(Regs::Ctl::Vs).read());

  enable_quirks();

  if (_cap.css() & 1)
    trace.printf("Controller supports NVM command set\n");
  else
    L4Re::chksys(-L4_ENOSYS, "Controller does not support NVM command set");

  // Configure PCI / PCI Express registers
  //
  // This step needs to be done before enabling the controller.
  _pci_dev->detect_msi_support();
  if (_icu->msis_supported())
    {
      if (Ctl::use_msixs && _pci_dev->msixs_supported())
        _pci_dev->enable_msix_pci();
      else if (Ctl::use_msis && _pci_dev->msis_supported())
        _pci_dev->enable_msi_pci();
    }

  // Start by resetting the controller, mostly to get the admin queue doorbell
  // registers to a known state.
  Ctl_cc cc(0);
  if (Ctl_csts(_regs.r<32>(Regs::Ctl::Csts).read()).rdy())
    {
      cc.raw = _regs.r<32>(Regs::Ctl::Cc).read();
      cc.en() = 0;
      _regs.r<32>(Regs::Ctl::Cc).write(cc.raw);
      (void) _regs.r<32>(Regs::Ctl::Cc).read(); // flush

      cc.raw = 0;

      trace.printf("Waiting for the controller to become disabled...\n");
      while (Ctl_csts(_regs.r<32>(Regs::Ctl::Csts).read()).rdy())
        ;
      trace.printf("done.\n");

      // A short delay seems to be necessary for some controllers
      if (_quirks.delay_after_disable())
        l4_sleep(3);
    }
  else
    trace.printf("The controller was not enabled, not disabling.\n");

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
  cc.mps() = L4_PAGESHIFT - Mps_base;
  if ((_cap.mpsmin() > cc.mps()) || (_cap.mpsmax() < cc.mps()))
    L4Re::chksys(-L4_ENOSYS, "Controller does not support the architectural page size");

  cc.css() = Regs::Ctl::Cc::Css_nvm;
  cc.en() = 1;
  _regs.r<32>(Regs::Ctl::Cc).write(cc.raw);

  trace.printf("Waiting for the controller to become ready...\n");
  while (!Ctl_csts(_regs.r<32>(Regs::Ctl::Csts).read()).rdy())
    ;
  trace.printf("done.\n");

  // Some controllers need a delay after the controller becomes ready
  if (_quirks.delay_after_enable())
    l4_sleep(_quirks.delay_after_enable_ms);

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
  Queue::Cqe volatile *cqe = _acq->consume();
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
Ctl::register_interrupt_handler()
{
  // check if the ICU supports MSIs
  l4_icu_info_t icu_info;
  L4Re::chksys(l4_error(_icu->icu()->info(&icu_info)), "Retrieving ICU infos");

  Dbg::info().printf("ICU info: features=%x #Irqs=%u, #MSIs=%u\n",
                      icu_info.features, icu_info.nr_irqs, icu_info.nr_msis);

  trace.printf("Registering IRQ server object with registry....\n");
  auto cap = L4Re::chkcap(_registry->register_irq_obj(this),
                          "Registering IRQ server object.");

  int irq = -1;
  unsigned char polarity = 0;

  if (msis_enabled())
    {
      irq = _icu->alloc_msi();

      if (irq >= 0)
        {
          trace.printf("Allocated MSI vector: %d\n", irq);

          irq |= L4::Icu::F_msi;

          // assume MSIs are edge triggered
          _irq_trigger_type = 1;
        }
      else
        irq = -1;
    }


  if (irq == -1)
    // use the legacy interrupt
    irq = L4Re::chksys(_dev.irq_enable(&_irq_trigger_type, &polarity),
                       "Enabling legacy interrupt.");

  int unmask_via_icu = l4_error(_icu->icu()->bind(irq, cap));
  L4Re::chksys(unmask_via_icu, "Binding interrupt to ICU.");

  trace.printf("IRQ[%x] unmask: %s\n", irq,
               unmask_via_icu ? "via ICU" : "direct");

  if (irq & L4::Icu::F_msi)
    {
      l4_icu_msi_info_t msi_info;
      l4_uint64_t source = _dev.dev_handle() | L4vbus::Icu::Src_dev_handle;
      L4Re::chksys(_icu->icu()->msi_info(irq, source, &msi_info),
                   "Retrieving MSI info.");
      Dbg::info().printf("MSI info: vector=0x%x addr=%llx, data=%x\n",
                         irq, msi_info.msi_addr, msi_info.msi_data);

      enable_msi(irq, msi_info);
    }

  Dbg::info().printf("Device: interrupt : %x trigger: %d, polarity: %d\n",
                     irq, (int)_irq_trigger_type, (int)polarity);
  trace.printf("Device: interrupt mask: %x\n",
               _regs.r<32>(Regs::Ctl::Intms).read());

  _regs.r<32>(Regs::Ctl::Intms).write(~0U);

  if (unmask_via_icu)
    L4Re::chksys(l4_ipc_error(_icu->icu()->unmask(irq), l4_utcb()),
                 "Unmasking interrupt");
  else
    L4Re::chksys(l4_ipc_error(cap->unmask(), l4_utcb()),
                 "Unmasking interrupt");

  _regs.r<32>(Regs::Ctl::Intmc).write(~0U);

  trace.printf("Attached to interupt %x\n", irq);
}

cxx::unique_ptr<Queue::Completion_queue>
Ctl::create_iocq(l4_uint16_t id, l4_size_t size, unsigned iv, Callback cb)
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
  sqe->iv() = _pci_dev->get_local_vector(iv);
  sqe->ien() = 1;
  sqe->pc() = 1;
  _asq->_callbacks[sqe->cid()] = cb;
  _asq->submit();

  return cq;
}

unsigned Ctl::allocate_msi(Nvme::Namespace *ns)
{
  // Default case for when MSI/X are not supported or none can be allocated.
  // In this case the namespace will use vector 0 and the same handler as
  // the controller uses for handling the admin queues.
  unsigned iv = 0;

  if (msis_enabled())
    {
      long msi = _icu->alloc_msi();
      if (msi >= 0)
        {
          iv = msi;
          msi |= L4::Icu::F_msi;

          auto cap = L4Re::chkcap(_registry->register_irq_obj(ns),
                                  "Registering IRQ server object.");

          L4Re::chksys(l4_error(_icu->icu()->bind(msi, cap)),
                       "Binding interrupt to ICU.");

          l4_icu_msi_info_t msi_info;
          l4_uint64_t source = _dev.dev_handle() | L4vbus::Icu::Src_dev_handle;
          L4Re::chksys(_icu->icu()->msi_info(msi, source, &msi_info),
                       "Retrieving MSI info.");
          Dbg::info().printf("MSI info: vector=0x%lx addr=%llx, data=%x\n", msi,
                             msi_info.msi_addr, msi_info.msi_data);

          enable_msi(msi, msi_info);

          L4Re::chksys(l4_ipc_error(cap->unmask(), l4_utcb()),
                       "Unmasking interrupt");
        }
      else
        iv = 0;
    }

  return iv;
}

void Ctl::free_msi(unsigned iv, Nvme::Namespace *ns)
{
  if (iv == 0)
    return;

  // We need to delete the IRQ object created in register_irq_obj() ourselves
  L4::Cap<L4::Task>(L4Re::This_task)
    ->unmap(ns->obj_cap().fpage(), L4_FP_ALL_SPACES | L4_FP_DELETE_OBJ);
  _registry->unregister_obj(ns);
  _icu->free_msi(iv);
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
    auto pos = _sn.find(' ');
    if (pos != _sn.npos)
      _sn.erase(pos);

    printf("Serial Number: %s\n", _sn.c_str());
    printf("Model Number: %.40s\n", ic->get<char>(Cns_ic::Mn));
    printf("Firmware Revision: %.8s\n", ic->get<char>(Cns_ic::Fr));

    _mdts = *ic->get<l4_uint8_t>(Cns_ic::Mdts);
    printf("Maximum Transfer Data Size: %u\n", _mdts);
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

void
Ctl::enable_quirks()
{
  l4_uint32_t val = 0;
  L4Re::chksys(_dev.cfg_read(0, &val, 32));

  l4_uint16_t vendor_id = val & 0xffffU;
  l4_uint16_t device_id = val >> 16;

  // 15b7:5011 Sandisk Corp WD PC SN810 / Black SN850 NVMe SSD
  if ((vendor_id == 0x15b7) && (device_id == 0x5011))
    _quirks.delay_after_disable() = true;

  // 144d:a80a Samsung Electronics Co Ltd NVMe SSD Controller PM9A1/PM9A3/980PRO
  // 144d:a80c Samsung Electronics Co Ltd NVMe SSD Controller S4LV008[Pascal]
  if ((vendor_id == 0x144d) && ((device_id == 0xa80a) || (device_id == 0xa80c)))
    {
      _quirks.delay_after_enable() = true;
      _quirks.delay_after_enable_ms = 60;
    }

  // 1e0f:000d KIOXIA Corporation NVMe SSD Controller XG7
  if ((vendor_id == 0x1e0f) && (device_id == 0x000d))
    {
      _quirks.delay_after_enable() = true;
      _quirks.delay_after_enable_ms = 60;
    }

  // Micron Technology 2300 NVMe (Santana)
  if ((vendor_id == 0x1344) && (device_id == 0x5405))
    {
      _quirks.delay_after_enable() = true;
      _quirks.delay_after_enable_ms = 3;
    }

  // Some (new, unknown) NVMe devices also require quirks. Here we enable a
  // default quirk. This shall make controller initialization more robust at
  // the cost of a small general slow down.
  if (!_quirks.raw)
    {
      warn.printf("Unknown NVMe controller. Enabling default quirks.\n");
      _quirks.delay_after_enable() = true;
      _quirks.delay_after_enable_ms = 60;
      _quirks.delay_after_disable() = true;
    }
  trace.printf("Enabled quirks: %#x\n", (unsigned int) _quirks.raw);
}

}
