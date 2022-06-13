/*
 * Copyright (C) 2020 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/shared_cap>
#include <l4/re/util/object_registry>
#include <l4/vbus/vbus>
#include <l4/vbus/vbus_pci>
#include <l4/cxx/bitfield>
#include <l4/drivers/hw_mmio_register_block>

#include <list>
#include <vector>
#include <stdio.h>
#include <cassert>
#include <string>

#include "nvme_types.h"
#include "queue.h"
#include "ns.h"
#include "inout_buffer.h"
#include "iomem.h"

#include <l4/libblock-device/device.h>

namespace Nvme {

/**
 * Encapsulates one single NVMe controller.
 *
 * Includes a server loop for handling device interrupts.
 */
class Ctl : public L4::Irqep_t<Ctl>
{
public:
  /**
   * Create a new NVMe controller from a vbus PCI device.
   */
  Ctl(L4vbus::Pci_dev const &dev,
      L4Re::Util::Shared_cap<L4Re::Dma_space> const &dma);

  Ctl(Ctl const &) = delete;
  Ctl(Ctl &&) = delete;

  /**
   * Dispatch interrupts for the HBA to the ports.
   */
  void handle_irq();

  /**
   * Register the interrupt handler with a registry.
   *
   * \param icu      ICU to request the capability for the hardware interrupt.
   * \param registry Registry that dispatches the interrupt IPCs.
   *
   * \throws L4::Runtime_error Resources are not available or accessible.
   */
  void register_interrupt_handler(L4::Cap<L4::Icu> icu,
                                  L4Re::Util::Object_registry *registry);


  /**
   * Identify the controller and the namespaces and initialize the ones that are
   * found.
   *
   * \param callback Function called for each active namespace.
   */
  void identify(std::function<void(cxx::unique_ptr<Namespace>)> callback);

  /**
   * Test if a VBUS device is a NVMe controller.
   *
   * \param dev      VBUS device to test.
   * \param dev_info Device information as returned by next_device()
   */
  static bool is_nvme_ctl(L4vbus::Device const &dev,
                          l4vbus_device_t const &dev_info);

  void add_ns(cxx::unique_ptr<Namespace> ns)
  { _nss.push_back(cxx::move(ns)); }

  L4::Cap<L4Re::Dma_space> dma() const
  { return _dma.get(); }

  bool supports_sgl() const
  { return use_sgls && _sgls; }

  std::string sn() const
  { return _sn; }

  cxx::unique_ptr<Queue::Completion_queue>
  create_iocq(l4_uint16_t id, l4_size_t size, Callback cb);
  cxx::unique_ptr<Queue::Submission_queue>
  create_iosq(l4_uint16_t id, l4_size_t size, l4_size_t sgls, Callback cb);
  void
  identify_namespace(l4_uint32_t n, l4_uint32_t nn,
                     std::function<void(cxx::unique_ptr<Namespace>)> callback);

private:
  l4_uint32_t cfg_read(l4_uint32_t reg) const
  {
    l4_uint32_t val;
    L4Re::chksys(_dev.cfg_read(reg, &val, 32));

    return val;
  }

  l4_uint16_t cfg_read_16(l4_uint32_t reg) const
  {
    l4_uint32_t val;
    L4Re::chksys(_dev.cfg_read(reg, &val, 16));

    return val;
  }

  void cfg_write_16(l4_uint32_t reg, l4_uint16_t val)
  {
    L4Re::chksys(_dev.cfg_write(reg, val, 16));
  }

  l4_uint64_t cfg_read_bar() const
  {
    return (((l4_uint64_t)cfg_read(0x14) << 32) | cfg_read(0x10))
           & 0xFFFFFFFFFFFFF000UL;
  }

  L4vbus::Pci_dev _dev;
  L4Re::Util::Shared_cap<L4Re::Dma_space> _dma;
  Iomem _iomem;
  L4drivers::Register_block<32> _regs;
  unsigned char _irq_trigger_type;
  std::list<cxx::unique_ptr<Namespace>> _nss;

  Ctl_cap _cap;

  bool _sgls;

  /// Serial number
  std::string _sn;

  // Admin Completion Queue
  cxx::unique_ptr<Queue::Completion_queue> _acq;
  // Admin Submission Queue
  cxx::unique_ptr<Queue::Submission_queue> _asq;

public:
  static bool use_sgls;
};
}
