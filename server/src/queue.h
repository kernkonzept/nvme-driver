/*
 * Copyright (C) 2020 Kernkonzept GmbH.
 * Author(s): Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/cxx/bitfield>
#include <l4/re/util/shared_cap>
#include <l4/drivers/hw_mmio_register_block>

#include <vector>

#include "nvme_types.h"
#include "inout_buffer.h"

namespace Nvme {

class Ctl;
class Namespace;

namespace Queue {

// These are tunables
enum
{
  Ioq_size = 32, ///< Number of entries per I/O queue.
  Ioq_sgls = 32, ///< Number of SGL entries per I/O queue entry.
};

/// Submission Queue Entry
struct Sqe
{
  volatile l4_uint32_t cdw0;
  volatile l4_uint32_t nsid;
  volatile l4_uint64_t _res;
  volatile l4_uint64_t mptr;
  union
  {
    struct
    {
      volatile l4_uint64_t prp1;
      volatile l4_uint64_t prp2;
    } prp;
    volatile Sgl_desc sgl1;
  };
  volatile l4_uint32_t cdw10;
  volatile l4_uint32_t cdw11;
  volatile l4_uint32_t cdw12;
  volatile l4_uint32_t cdw13;
  volatile l4_uint32_t cdw14;
  volatile l4_uint32_t cdw15;

  CXX_BITFIELD_MEMBER(0, 7, opc, cdw0);    ///< Opcode
  CXX_BITFIELD_MEMBER(14, 15, psdt, cdw0); ///< PRP or SGL Data Transfer
  CXX_BITFIELD_MEMBER(16, 31, cid, cdw0);  ///< Command Identifier

  // Identify command
  CXX_BITFIELD_MEMBER(0, 7, cns, cdw10);     ///< Controller or Namespace Structure
  CXX_BITFIELD_MEMBER(16, 31, cntid, cdw10); ///< Controller Identifier

  // Create I/O Completion / Submission Queue commands
  CXX_BITFIELD_MEMBER(0, 15, qid, cdw10);    ///< Queue Identifier
  CXX_BITFIELD_MEMBER(16, 31, qsize, cdw10); ///< Queue Size

  // Identify Namespace command
  CXX_BITFIELD_MEMBER(0, 15, nvmsetid, cdw11); ///< NVM Set Identifier

  // Create I/O Completion / Submission Queue commands
  CXX_BITFIELD_MEMBER(0, 0, pc, cdw11);  ///< Physically Contiguous

  // Create I/O Completion Queue command
  CXX_BITFIELD_MEMBER(1, 1, ien, cdw11); ///< Interrupt Enable

  // Create I/O Completion Submission command
  CXX_BITFIELD_MEMBER(16, 31, cqid, cdw11); ///< Completion Queue Identifier

  // Read / Write / Write Zeroes commands
  CXX_BITFIELD_MEMBER(0, 15, nlb, cdw12); ///< Number of Logical Blocks

  // Write Zeroes command
  CXX_BITFIELD_MEMBER(25, 25, deac, cdw12); ///< Deallocate
};

/// Completion Queue Entry
struct Cqe
{
  volatile l4_uint32_t dw0;
  volatile l4_uint32_t dw1;
  volatile l4_uint32_t dw2;
  volatile l4_uint32_t dw3;

  CXX_BITFIELD_MEMBER_RO(16, 31, sqid, dw2); ///< SQ Identifier
  CXX_BITFIELD_MEMBER_RO(0, 15, sqhd, dw2);  ///< SQ Head Pointer

  CXX_BITFIELD_MEMBER_RO(0, 15, cid, dw3); ///< Command Identifier
  CXX_BITFIELD_MEMBER_RO(16, 16, p, dw3);  ///< Phase Tag
  CXX_BITFIELD_MEMBER_RO(17, 31, sf, dw3); ///< Status Field
};

class Queue
{
public:
  Queue(l4_uint16_t size, unsigned y, unsigned dstrd,
        L4drivers::Register_block<32> &regs,
        L4Re::Util::Shared_cap<L4Re::Dma_space> const &dma,
        L4Re::Dma_space::Direction dir)
  : _size(size), _y(y), _dstrd(dstrd), _regs(regs), _head(0)
  {
    _entry_size = (dir == L4Re::Dma_space::Direction::From_device)
                    ? sizeof(Cqe)
                    : sizeof(Sqe);
    _buf =
      cxx::make_ref_obj<Inout_buffer>(l4_round_page(size * _entry_size), dma,
                                      dir, L4Re::Rm::F::Cache_uncached);
    memset(_buf->get<void *>(), 0, _buf->size());
  }

  l4_addr_t phys_base() const { return _buf->pget(); }

  l4_uint16_t size() const { return _size; }

protected:
    l4_uint16_t wrap_around(l4_uint16_t i) const
    {
      return i % _size;
    }

    l4_uint16_t _size;
    l4_size_t _entry_size;

    unsigned _y;
    unsigned _dstrd;

    L4drivers::Register_block<32> _regs;

    l4_uint16_t _head;

    cxx::Ref_ptr<Inout_buffer> _buf;
};

class Submission_queue : public Queue
{
  friend class Nvme::Ctl;
  friend class Nvme::Namespace;

public:
  Submission_queue(l4_uint16_t size, unsigned y, unsigned dstrd,
                   L4drivers::Register_block<32> &regs,
                   L4Re::Util::Shared_cap<L4Re::Dma_space> const &dma,
                   l4_size_t sgls = 0)
  : Queue(size, y, dstrd, regs, dma, L4Re::Dma_space::Direction::To_device),
    _tail(0)
  {
    for (auto i = 0u; i < size; i++)
      {
        Sqe *sqe = _buf->get<Sqe>(i * _entry_size);
        sqe->cdw0 = i << 16;
      }

    _callbacks.reserve(_size);

    if (sgls)
      {
        _sgls = cxx::make_ref_obj<Inout_buffer>(
          l4_round_page(size * sgls * sizeof(Sgl_desc)), dma,
          L4Re::Dma_space::Direction::To_device, L4Re::Rm::F::Cache_uncached);
      }
  }

  bool is_full() const { return _head == wrap_around(_tail + 1); }

  Sqe *produce()
  {
    if (is_full())
      return 0;
    if (_callbacks[_tail])
      {
        // Need to wait for the callback to be finished first before we can
        // use this entry again.
        return 0;
      }
    Sqe *sqe = _buf->get<Sqe>(_tail * _entry_size);
    _tail = wrap_around(_tail + 1);

    // Clear all but the Command Identifier
    memset((void *)sqe, 0, sizeof(l4_uint16_t));
    memset((void *)&sqe->nsid, 0, sizeof(*sqe) - sizeof(sqe->cdw0));
    return sqe;
  }

  void submit() { _regs.r<32>(tdbl()).write(_tail); }

private:
  std::vector<std::function<void(l4_uint16_t)>> _callbacks;
  cxx::Ref_ptr<Inout_buffer> _sgls;

  unsigned tdbl() const { return 0x1000 + ((2 * _y) * (4 << _dstrd)); }

  l4_uint16_t _tail;
};


class Completion_queue : public Queue
{
public:
  Completion_queue(l4_uint16_t size, unsigned y, unsigned dstrd,
                   L4drivers::Register_block<32> &regs,
                   L4Re::Util::Shared_cap<L4Re::Dma_space> const &dma)
  : Queue(size, y, dstrd, regs, dma, L4Re::Dma_space::Direction::From_device),
    _p(true)
  {
  }

  Cqe *consume()
  {
    Cqe *cqe = _buf->get<Cqe>(_head * _entry_size);
    if (cqe->p() == _p)
      {
        _head = wrap_around(_head + 1);
        if (!_head)
          _p = !_p;
        return cqe;
      }
    return 0;
  }

  void complete() { _regs.r<32>(hdbl()).write(_head); }

private:
  unsigned hdbl() const { return 0x1000 + ((2 * _y + 1) * (4 << _dstrd)); }

  bool _p;
};

}
}
