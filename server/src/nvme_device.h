/*
 * Copyright (C) 2018, 2023-2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/cxx/string>

#include <string>

#include "ctl.h"
#include "ns.h"

#include <l4/libblock-device/device.h>

namespace Nvme {

class Nvme_base_device
: public Block_device::Device,
  public Block_device::Device_discard_feature
{
};

class Nvme_device
: public Block_device::Device_with_notification_domain<Nvme_base_device>
{
public:
  Nvme_device(Namespace *ns) : _ns(cxx::move(ns))
  {
    _hid = _ns->ctl().sn() + ":n" + std::to_string(_ns->nsid());
  }

  bool is_read_only() const override
  { return _ns->ro(); }

  bool match_hid(cxx::String const &hid) const override
  {
    return hid == cxx::String(_hid.c_str());
  }

  l4_uint64_t capacity() const override
  { return _ns->nsze() * _ns->lba_sz(); }

  l4_size_t sector_size() const override
  { return _ns->lba_sz(); }

  l4_size_t max_size() const override
  {
    l4_size_t max_size;
    l4_size_t ps = 1UL << (Ctl::Mps_base + _ns->ctl().cap().mpsmin());

    if (_ns->ctl().supports_sgl())
      {
        max_size = 4 * 1024 * 1024;
        if (_ns->ctl().mdts())
          {
            // Spread the MDTS limit evenly over all allowed VIRTIO blk segments
            max_size =
              cxx::min(max_size, (ps << _ns->ctl().mdts()) / Queue::Ioq_sgls);
          }
      }
    else
      {
        // Account for the possibility of data starting at non-zero page offset.
        max_size = (Queue::Prp_data_entries - 1) * L4_PAGESIZE;
        if (_ns->ctl().mdts())
          max_size = cxx::min(max_size, ps << _ns->ctl().mdts());
      }
    return max_size;
  }

  unsigned max_segments() const override
  {
    // When SGLs are not available, the max number of segments must be one
    // because it is generally not possible to account for the page-unaligned
    // start of a segment in the middle of a PRP List.
    return _ns->ctl().supports_sgl() ? Queue::Ioq_sgls : 1;
  }

  Discard_info discard_info() const override
  {
    Discard_info di;

    di.max_discard_sectors = 0;
    di.max_discard_seg = 0;
    di.discard_sector_alignment = 0;
    di.max_write_zeroes_sectors = 65536;
    di.max_write_zeroes_seg = 1;
    di.write_zeroes_may_unmap = _ns->dlfeat().deallocwz();

    return di;
  }

  void reset() override
  {} // TODO

  int dma_map(Block_device::Mem_region *region, l4_addr_t offset,
              l4_size_t num_sectors, L4Re::Dma_space::Direction dir,
              L4Re::Dma_space::Dma_addr *phys) override
  {
    l4_size_t size = num_sectors * sector_size();
    return _ns->ctl().dma()->map(L4::Ipc::make_cap_rw(region->ds()), offset,
                                 &size, L4Re::Dma_space::Attributes::None, dir,
                                 phys);
  }

  int dma_unmap(L4Re::Dma_space::Dma_addr phys, l4_size_t num_sectors,
                L4Re::Dma_space::Direction dir) override
  {
    return _ns->ctl().dma()->unmap(phys, num_sectors * sector_size(),
                                   L4Re::Dma_space::Attributes::None, dir);
  }

  int inout_data(l4_uint64_t sector,
                 Block_device::Inout_block const &blocks,
                 Block_device::Inout_callback const &cb,
                 L4Re::Dma_space::Direction dir) override;

  int flush(Block_device::Inout_callback const &cb) override;

  int discard(l4_uint64_t offset, Block_device::Inout_block const &block,
              Block_device::Inout_callback const &cb, bool discard) override;

  void start_device_scan(Block_device::Errand::Callback const &callback) override
  {
    // Nothing to do at this point
    callback();
  };

private:
  Namespace *_ns;
  std::string _hid;
};



} // name space
