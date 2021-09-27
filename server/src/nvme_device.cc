/*
 * Copyright (C) 2020 Kernkonzept GmbH.
 * Author(s): Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
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
  Queue::Sqe *sqe;
  l4_size_t sectors = 0;
  l4_size_t blocks = 0;
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
    }
  else
    {
      // Fallback to using PRPs
      sectors =
        std::min((l4_size_t)block.num_sectors, max_size() / sector_size());
      ++blocks;
      sqe = _ns->readwrite_prepare_prp(read, sector, block.dma_addr,
                                       sectors * sector_size());
      if (!sqe)
        return -L4_EBUSY;
    }

  // XXX: defer running of the callback to an Errand like the ahci-driver does?
  l4_size_t sz = sectors * sector_size();
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
