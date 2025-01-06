/*
 * Copyright (C) 2020-2021, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/shared_cap>
#include <l4/cxx/ref_ptr>

#include <stdio.h>
#include <cassert>

namespace Nvme {

class Inout_buffer : public cxx::Ref_obj
{
public:
  Inout_buffer(l4_size_t size,
               L4Re::Util::Shared_cap<L4Re::Dma_space> const &dma,
               L4Re::Dma_space::Direction dir,
               L4Re::Rm::Flags flags = L4Re::Rm::Flags(0))
  : _size(size), _dma(dma), _paddr(0), _dir(dir)
  {
    _ds = L4Re::chkcap(L4Re::Util::make_unique_cap<L4Re::Dataspace>(),
                       "Allocate dataspace capability for IO memory.");

    auto *e = L4Re::Env::env();
    L4Re::chksys(e->mem_alloc()->alloc(size, _ds.get(),
                                       L4Re::Mem_alloc::Continuous
                                       | L4Re::Mem_alloc::Pinned),
                 "Allocate pinned memory.");

    L4Re::chksys(
      e->rm()->attach(&_region, size,
                      L4Re::Rm::F::Search_addr | L4Re::Rm::F::RW | flags,
                      L4::Ipc::make_cap_rw(_ds.get()), 0, L4_PAGESHIFT),
      "Attach IO memory.");

    l4_size_t out_size = size;
    L4Re::chksys(_dma->map(L4::Ipc::make_cap_rw(_ds.get()), 0, &out_size,
                           L4Re::Dma_space::Attributes::None, dir, &_paddr),
                 "Lock memory region for DMA.");
    if (out_size < size)
      L4Re::chksys(-L4_ENOMEM, "Mapping whole region into DMA space");
  }

  Inout_buffer(Inout_buffer const &) = delete;
  Inout_buffer(Inout_buffer &&) = delete;
  Inout_buffer &operator=(Inout_buffer &&rhs) = delete;

  ~Inout_buffer()
  {
    if (_paddr)
      unmap();
  }

  void unmap()
  {
    L4Re::chksys(
      _dma->unmap(_paddr, _size, L4Re::Dma_space::Attributes::None, _dir));
    _paddr = 0;
  }

  template <class T>
  T *get(unsigned offset = 0) const
  { return reinterpret_cast<T *>(_region.get() + offset); }

  l4_addr_t pget(unsigned offset = 0) const
  { return _paddr + offset; }

  l4_size_t size() const
  { return _size; }

private:
  l4_size_t _size;
  L4Re::Util::Shared_cap<L4Re::Dma_space> _dma;
  L4Re::Util::Unique_cap<L4Re::Dataspace> _ds;
  L4Re::Rm::Unique_region<char *> _region;
  L4Re::Dma_space::Dma_addr _paddr;
  L4Re::Dma_space::Direction _dir;
};

}
