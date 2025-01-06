/*
 * Copyright (C) 2019, 2024 Kernkonzept GmbH.
 * Author(s): Matthias Lange <matthias.lange@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#pragma once

#include <l4/vbus/vbus>
#include <l4/vbus/vbus_pci>

#include <map>

#include "iomem.h"
#include "msi_allocator.h"

namespace Nvme {

class Pci_dev
{
private:
  struct Pci_msi
  {
    enum
    {
      Ctrl_offset = 0x2,
    };

    struct Ctrl
    {
      l4_uint16_t raw;
      CXX_BITFIELD_MEMBER(7, 7, large, raw);
      CXX_BITFIELD_MEMBER(4, 6, mme, raw);
      CXX_BITFIELD_MEMBER_RO(1, 3, mmc, raw);
      CXX_BITFIELD_MEMBER(0, 0, enabled, raw);
    };
  };

  struct Pci_msix
  {
    enum
    {
      Ctrl_offset = 0x2,
      Table_offset = 0x4,
      Pba_offset = 0x8,
    };
    struct Ctrl
    {
      l4_uint16_t raw;
      CXX_BITFIELD_MEMBER(15, 15, enabled, raw);
      CXX_BITFIELD_MEMBER(14, 14, masked, raw);
      CXX_BITFIELD_MEMBER_RO(0, 10, ts, raw);
    };
    struct Offset_bir
    {
      l4_uint32_t raw;
      CXX_BITFIELD_MEMBER_UNSHIFTED_RO(3, 31, offset, raw);
      CXX_BITFIELD_MEMBER_RO(0, 2, bir, raw);
    };
  };

  struct Cap
  {
    Cap() { reset(); }
    Cap(l4_uint8_t id, l4_uint32_t addr) : id(id), addr(addr) {}
    void reset()
    {
      id = 0;
      addr = 0;
    }
    l4_uint8_t id;
    l4_uint32_t addr;
  };

  struct Msi_cap : Cap
  {
    using Cap::Cap;

    /**
     * Multi Message Capable
     *
     * \return The number of requested vectors.
     */
    unsigned msis_supported()
    { return 1 << ctrl.mmc(); }

    Pci_msi::Ctrl ctrl;
  };

  struct Msix_cap : Cap
  {
    using Cap::Cap;

    unsigned msixs_supported()
    { return ctrl.ts() + 1; }

    Pci_msix::Ctrl ctrl;
  };

public:
  Pci_dev(L4vbus::Pci_dev const &dev) : _dev(dev), _last(0) {}

  l4_uint32_t cfg_read_32(l4_uint32_t reg, char const *msg = "") const
  {
    l4_uint32_t val;
    L4Re::chksys(_dev.cfg_read(reg, &val, 32), msg);
    return val;
  }

  l4_uint16_t cfg_read_16(l4_uint32_t reg, char const *msg = "") const
  {
    l4_uint32_t val;
    L4Re::chksys(_dev.cfg_read(reg, &val, 16), msg);
    return val;
  }

  l4_uint8_t cfg_read_8(l4_uint32_t reg, char const *msg = "") const
  {
    l4_uint32_t val;
    L4Re::chksys(_dev.cfg_read(reg, &val, 8), msg);
    return val;
  }

  void cfg_write_32(l4_uint32_t reg, l4_uint32_t val, char const *msg = "")
  {
    L4Re::chksys(_dev.cfg_write(reg, val, 32), msg);
  }

  void cfg_write_16(l4_uint32_t reg, l4_uint16_t val, char const *msg = "")
  {
    L4Re::chksys(_dev.cfg_write(reg, val, 16), msg);
  }

  /** Return the number of supported MSIs
   * \retval 0  Device does not support MSIs.
   * \retval >0 Number of MSIs supported / requested by the device
   */
  unsigned msis_supported()
  {
    return (_msi_cap.id == Msi) ? _msi_cap.msis_supported() : 0;
  }

  /** Return the number of supported MSI-Xs
   * \retval 0  Device does not support MSI-Xs.
   * \retval >0 Number of MSI-Xs supported / requested by the device
   */
  unsigned msixs_supported()
  {
    return (_msix_cap.id == Msi_x) ? _msix_cap.msixs_supported() : 0;
  }

  /** Detects whether MSIs or MSI-Xs are supported */
  void detect_msi_support()
  {
    // 1. check bit 4 of status register if capabilities are supported
    // 2. read capabilities pointer and mask lower two bits
    l4_uint16_t status = cfg_read_16(Cfg_status, "Reading PCI Status register");

    // check if capabilities list is valid
    if (status & 0x10)
      {
        l4_uint8_t cap_ptr =
          cfg_read_8(Cfg_cap_ptr, "Reading PCI Capabilities Pointer register")
          & ~0x3;
        // search for MSI capability
        for (; cap_ptr;
             cap_ptr =
               cfg_read_8(cap_ptr + 1, "Reading PCI Next Pointer register") & ~0x3)
          {
            l4_uint8_t id =
              cfg_read_8(cap_ptr, "Reading PCI Capability ID register");

            switch (id)
              {
              case Msi:
                _msi_cap = Msi_cap(id, cap_ptr);
                _msi_cap.ctrl.raw =
                  cfg_read_16(cap_ptr + Pci_msi::Ctrl_offset,
                              "Reading MSI Message Control register");
                break;

              case Msi_x:
                _msix_cap = Msix_cap(id, cap_ptr);
                _msix_cap.ctrl.raw =
                  cfg_read_16(cap_ptr + Pci_msix::Ctrl_offset,
                              "Reading MSI-X Message Control register");
                break;
              }
          }
      }
  }

  void enable_msi_pci()
  {
    _msi_cap.ctrl.enabled() = 1;
    // Enable all supported of vectors
    _msi_cap.ctrl.mme() = _msi_cap.ctrl.mmc();
    cfg_write_16(_msi_cap.addr + Pci_msi::Ctrl_offset, _msi_cap.ctrl.raw,
                 "Writing MSI Capability Control register");
  }

  void enable_msix_pci()
  {
    _msix_cap.ctrl.enabled() = 1;
    _msix_cap.ctrl.masked() = 0;
    cfg_write_16(_msix_cap.addr + Pci_msix::Ctrl_offset, _msix_cap.ctrl.raw,
                 "Writing MSI-X Capability Control register");
  }

  void enable_msi(int, l4_icu_msi_info_t msi_info)
  {
    // This can be called repeatedly, we just clear the least significant bits
    // according to the number of allocated vectors.
    cfg_write_32(_msi_cap.addr + 4, msi_info.msi_addr & 0xffffffff,
                 "Writing MSI Message Address register");
    if (_msi_cap.ctrl.large())
      {
        cfg_write_32(_msi_cap.addr + 8, msi_info.msi_addr & ~0xffffffffULL,
                     "Writing MSI Message Upper Address register");
        cfg_write_16(_msi_cap.addr + 0xc,
                     msi_info.msi_data & ~(msis_supported() - 1),
                     "Writing MSI Message Data register");
      }
    else
      cfg_write_16(_msi_cap.addr + 8,
                   msi_info.msi_data & ~(msis_supported() - 1),
                   "Writing MSI Message Data register");
  }

  unsigned get_local_vector(unsigned irq)
  {
    unsigned msi = (irq & ~L4::Icu::F_msi);

    // Convert the ICU-global MSI number to a controller-local vector
    if (_vectors.find(msi) == _vectors.end())
      _vectors[msi] = _last++;

    return _vectors[msi];
  }

  void enable_msix(int irq, l4_icu_msi_info_t msi_info)
  {
    if (!_msix_table.vaddr.get())
      {
        Pci_msix::Offset_bir table_offset;

        table_offset.raw =
          cfg_read_32(_msix_cap.addr + Pci_msix::Table_offset,
                      "Reading MSI-X capability Table Offset register");

        l4_uint64_t bar = cfg_read_32(0x10 + (unsigned)table_offset.bir() * 4,
                                      "Reading Table Offset BAR");

        if (bar & 4) // 64bit BAR?
          {
            l4_uint32_t bar2 = cfg_read_32(0x14 + (unsigned)table_offset.bir() * 4,
                                           "Reading Table Offset BAR 2");
            bar |= (l4_uint64_t)bar2 << 32;
          }

        Iomem msix_table_mem(
          (bar & ~0xfull) + table_offset.offset(), _msix_cap.ctrl.ts() * 16,
          L4::cap_reinterpret_cast<L4Re::Dataspace>(_dev.bus_cap()));
        _msix_table = std::move(msix_table_mem);
      }

    L4drivers::Mmio_register_block<32> msix_table_mmio(_msix_table.vaddr.get());
    L4drivers::Register_block<32> msix_table(&msix_table_mmio);

    // Configure MSI in MSI-X table
    unsigned v = get_local_vector(irq & ~L4::Icu::F_msi);
    msix_table.r<32>(v * 16 + 0).write(msi_info.msi_addr & 0xffffffff);
    msix_table.r<32>(v * 16 + 4).write(msi_info.msi_addr >> 32);
    msix_table.r<32>(v * 16 + 8).write(msi_info.msi_data);
    msix_table.r<32>(v * 16 + 12).clear(1); // Unmask
  }

  enum
  {
    Cfg_vendor = 0x0,
    Cfg_device = 0x2,
    Cfg_status = 0x6,
    Cfg_cap_ptr = 0x34,
  };

  enum Cap_types
  {
    Msi   = 0x5,
    Msi_x = 0x11,
  };

private:
  L4vbus::Pci_dev _dev;
  Msi_cap _msi_cap;
  Msix_cap _msix_cap;
  Iomem _msix_table;
  // Map for converting ICU-global MSI numbers to controller-local vectors
  std::map<unsigned, unsigned> _vectors;
  // Last assigned controller-local vector
  unsigned _last;
};

} // namespace Nvme
