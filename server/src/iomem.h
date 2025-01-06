/*
 * Copyright (C) 2020, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/dataspace>
#include <l4/re/rm>

namespace Nvme {

/**
 * Self-attaching IO memory.
 */
struct Iomem
{
  Iomem(l4_addr_t phys_addr, l4_size_t size, L4::Cap<L4Re::Dataspace> iocap)
  {
    L4Re::chksys(L4Re::Env::env()->rm()->attach(
      &vaddr, l4_round_page(size),
      L4Re::Rm::F::Search_addr | L4Re::Rm::F::Cache_uncached | L4Re::Rm::F::RW,
      L4::Ipc::make_cap_rw(iocap), phys_addr, L4_PAGESHIFT));
  }

  Iomem() {}

  Iomem(Iomem &&mem)
  { vaddr = std::move(mem.vaddr); }

  Iomem& operator=(Nvme::Iomem &&mem)
  {
    vaddr = std::move(mem.vaddr);
    return *this;
  }

  L4Re::Rm::Unique_region<l4_addr_t> vaddr;
};

}
