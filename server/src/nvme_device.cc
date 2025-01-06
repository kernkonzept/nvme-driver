/*
 * Copyright (C) 2020-2022, 2024 Kernkonzept GmbH.
 * Author(s): Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include <algorithm>

#include "debug.h"
#include "nvme_device.h"
#include "nvme_types.h"
#include "ctl.h"
#include "queue.h"

int
Nvme::Nvme_device::inout_data(l4_uint64_t sector,
                              Block_device::Inout_block const &block,
                              Block_device::Inout_callback const &cb,
                              L4Re::Dma_space::Direction dir)
{
  Queue::Sqe volatile *sqe;
  l4_size_t sectors = 0;
  l4_size_t blocks = 0;
  l4_size_t sz;
  bool read = (dir == L4Re::Dma_space::Direction::From_device ? true : false);
  if (_ns->ctl().supports_sgl())
    {
      Sgl_desc *sgls;
      sqe = _ns->readwrite_prepare_sgl(read, sector, &sgls);

      if (!sqe)
        return -L4_EBUSY;

      // Construct the SGL
      auto *b = &block;
      for (auto i = 0u; b && i < Queue::Ioq_sgls; i++, b = b->next.get())
        {
          sectors += b->num_sectors;
          ++blocks;
          sgls[i].sgl_id = Sgl_id::Data;
          sgls[i].addr = b->dma_addr;
          sgls[i].len = b->num_sectors * sector_size();
        }

      sz = sectors * sector_size();
    }
  else
    {
      // Fallback to using PRPs

      Prp_list_entry *prps;

      l4_assert(!block.next);

      sectors =
        std::min((l4_size_t)block.num_sectors, max_size() / sector_size());
      ++blocks;
      sz = sectors * sector_size();
      sqe = _ns->readwrite_prepare_prp(read, sector, block.dma_addr, sz, &prps);

      if (!sqe)
        return -L4_EBUSY;

      // figure out what is covered by PRP0
      l4_uint64_t paddr = l4_trunc_page(block.dma_addr + L4_PAGESIZE);
      l4_size_t remains = 0;
      if (sz >= (paddr - block.dma_addr))
        remains = sz - (paddr - block.dma_addr);

      // covered already by PRP1?
      if (remains <= L4_PAGESIZE)
        remains = 0;

      // Construct the PRP List
      for (auto i = 0u, p = 0u; remains && i < Queue::Prp_list_entries; i++)
        {
          // Check for the last entry in a page and link it to the next page.
          if ((i % Queue::Prp_list_entries_per_page
               == Queue::Prp_list_entries_per_page - 1)
              && (remains > L4_PAGESIZE))
            {
              p++;
              prps[i].addr = sqe->prp.prp2 + p * L4_PAGESIZE;
              continue;
            }
          prps[i].addr = paddr;
          paddr += L4_PAGESIZE;
          remains -= cxx::min<l4_size_t>(remains, L4_PAGESIZE);
        }

      l4_assert(remains == 0);
    }

  // XXX: defer running of the callback to an Errand like the ahci-driver does?
  Block_device::Inout_callback callback = cb; // capture a copy
  _ns->readwrite_submit(sqe, sectors - 1, blocks,
                        [callback, sz](l4_uint16_t status) {
                          callback(status ? -L4_EIO : L4_EOK, status ? 0 : sz);
                        });

  return L4_EOK;
}

int
Nvme::Nvme_device::flush(Block_device::Inout_callback const &cb)
{
  // The NVMe driver does not enable the Volatile Write Cache in the controller
  // (if present) and neither it nor libblock-device implements a software block
  // cache, so there is nothing to flush at this point.
  cb(0, 0);
  return L4_EOK;
}

int
Nvme::Nvme_device::discard(l4_uint64_t offset,
                           Block_device::Inout_block const &block,
                           Block_device::Inout_callback const &cb, bool discard)
{
  l4_assert(!discard);
  l4_assert(!block.next.get());
  (void)discard;

  Block_device::Inout_callback callback = cb; // capture a copy
  bool sub = _ns->write_zeroes(offset + block.sector, block.num_sectors - 1,
                               block.flags & Block_device::Inout_f_unmap,
                               [callback](l4_uint16_t status) {
                                 callback(status ? -L4_EIO : L4_EOK, 0);
                               });
  if (!sub)
    return -L4_EBUSY;

  return L4_EOK;
}
