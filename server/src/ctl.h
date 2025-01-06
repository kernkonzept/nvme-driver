/*
 * Copyright (C) 2020, 2022-2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
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

#include "pci.h"
#include "icu.h"

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
  Ctl(L4vbus::Pci_dev const &dev, cxx::Ref_ptr<Icu> icu,
      L4Re::Util::Object_registry *registry,
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
   * \throws L4::Runtime_error Resources are not available or accessible.
   */
  void register_interrupt_handler();


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

  bool msis_enabled() const
  {
    return _icu->msis_supported()
           && ((use_msixs && _pci_dev->msixs_supported())
               || (use_msis && _pci_dev->msis_supported()));
  }

  bool enable_msi(int irq, l4_icu_msi_info_t msi_info)
  {
    if (!(irq & L4::Icu::F_msi))
      return false;

    if (use_msixs && _pci_dev->msixs_supported())
      _pci_dev->enable_msix(irq, msi_info);
    else if (use_msis && _pci_dev->msis_supported())
      _pci_dev->enable_msi(irq, msi_info);

    return true;
  }

  unsigned allocate_msi(Nvme::Namespace *ns);
  void free_msi(unsigned iv, Nvme::Namespace *ns);

  Ctl_cap const &cap() const
  { return _cap; }

  std::string sn() const
  { return _sn; }

  l4_uint8_t mdts() const
  { return _mdts; }

  cxx::unique_ptr<Queue::Completion_queue>
  create_iocq(l4_uint16_t id, l4_size_t size, unsigned iv, Callback cb);
  cxx::unique_ptr<Queue::Submission_queue>
  create_iosq(l4_uint16_t id, l4_size_t size, l4_size_t sgls, Callback cb);
  void
  identify_namespace(l4_uint32_t n, l4_uint32_t nn,
                     std::function<void(cxx::unique_ptr<Namespace>)> callback);

private:
  l4_uint32_t cfg_read(l4_uint32_t reg) const
  {
    return _pci_dev->cfg_read_32(reg);
  }

  l4_uint16_t cfg_read_16(l4_uint32_t reg) const
  {
    return _pci_dev->cfg_read_16(reg);
  }

  void cfg_write_16(l4_uint32_t reg, l4_uint16_t val)
  {
    _pci_dev->cfg_write_16(reg, val);
  }

  l4_uint64_t cfg_read_bar() const
  {
    return (((l4_uint64_t)cfg_read(0x14) << 32) | cfg_read(0x10))
           & 0xFFFFFFFFFFFFF000UL;
  }

  void enable_quirks();

  L4vbus::Pci_dev _dev;
  cxx::unique_ptr<Pci_dev> _pci_dev;
  cxx::Ref_ptr<Icu> _icu;
  L4Re::Util::Object_registry *_registry;
  L4Re::Util::Shared_cap<L4Re::Dma_space> _dma;
  Iomem _iomem;
  L4drivers::Register_block<32> _regs;
  unsigned char _irq_trigger_type;
  std::list<cxx::unique_ptr<Namespace>> _nss;

  Ctl_cap _cap;

  bool _sgls;

  /// Serial number
  std::string _sn;

  l4_uint8_t _mdts;

  // Admin Completion Queue
  cxx::unique_ptr<Queue::Completion_queue> _acq;
  // Admin Submission Queue
  cxx::unique_ptr<Queue::Submission_queue> _asq;

  struct Quirks
  {
    l4_uint8_t raw;
    CXX_BITFIELD_MEMBER(1, 2, delay_after_enable,
                        raw); ///< Controller needs a delay after it's enbaled
    CXX_BITFIELD_MEMBER(0, 1, delay_after_disable,
                        raw); ///< Controller needs a delay after it's disabled

    unsigned delay_after_enable_ms;

    Quirks() : raw(0), delay_after_enable_ms(0) {}
  } _quirks;

public:
  enum
  {
    Mps_base = 12,  ///< Base page width supported by NVMe
  };

  static bool use_sgls;
  static bool use_msis;
  static bool use_msixs;
};
}
