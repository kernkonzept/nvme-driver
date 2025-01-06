/*
 * Copyright (C) 2020-2021, 2024 Kernkonzept GmbH.
 * Author(s): Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#pragma once

#include <l4/cxx/bitfield>

#include "nvme_types.h"
#include "queue.h"
#include "inout_buffer.h"

namespace Nvme {

class Ctl;

class Namespace : public L4::Irqep_t<Namespace>
{
public:
  Namespace(Ctl &ctl, l4_uint32_t nsid, l4_size_t lba_sz,
            cxx::Ref_ptr<Inout_buffer> const &in);

  ~Namespace();

  void
  async_loop_init(l4_uint32_t nsids,
                  std::function<void(cxx::unique_ptr<Namespace>)> callback);

  Ctl const &ctl() const
  { return _ctl; }

  l4_uint32_t nsid() const
  { return _nsid; }

  l4_uint64_t nsze() const
  { return _nsze; }

  l4_size_t lba_sz() const
  { return _lba_sz; }

  bool ro() const
  { return _ro; }

  Ns_dlfeat dlfeat() const
  { return _dlfeat; }


  void handle_irq()
  {
    while (auto *cqe = _iocq->consume())
      {
        assert(cqe->sqid() == qid());
        _iosq->_head = cqe->sqhd();
        assert(_iosq->_callbacks[cqe->cid()]);
        auto cb = _iosq->_callbacks[cqe->cid()];
        _iosq->_callbacks[cqe->cid()] = nullptr;
        cb(cqe->sf());
        _iocq->complete();
      }
  }

  Queue::Sqe volatile *readwrite_prepare_sgl(bool read, l4_uint64_t slba,
                                             Sgl_desc **sglp) const;
  Queue::Sqe volatile *readwrite_prepare_prp(bool read, l4_uint64_t slba,
                                             l4_uint64_t paddr, l4_size_t sz,
                                             Prp_list_entry **prpp) const;
  void readwrite_submit(Queue::Sqe volatile *sqe, l4_uint16_t nlb, l4_size_t blocks,
                        Callback cb) const;

  bool write_zeroes(l4_uint64_t slba, l4_uint16_t nlb, bool dealloc, Callback cb) const;

private:
  /// Returns the I/O queue identifier for this namespace
  l4_uint16_t qid() const
  {
    // For simplicity, we reuse the namespace identifier as the I/O queue
    // identifier. Valid namespace identifiers start counting from 1, just as
    // valid I/O queue identifiers do. One drawback of this approach is that it
    // only works for namespace identifiers below the 64K limit. For now we
    // assert that the namespace identifier is below this limit.
    l4_assert(_nsid < 65536);
    return _nsid;
  }

  /// Callback to be called when the initialization of the namespace is complete
  std::function<void(cxx::unique_ptr<Namespace>)> _callback;

  Ctl &_ctl;

  unsigned _msi;

  cxx::unique_ptr<Queue::Completion_queue> _iocq;
  cxx::unique_ptr<Queue::Submission_queue> _iosq;

  l4_uint32_t _nsid; ///< Namespace Identifier
  l4_uint64_t _nsze; ///< Namespace Size [number of LBAs]
  l4_size_t _lba_sz; ///< LBA size [bytes]
  bool _ro;          ///< Read-only
  Ns_dlfeat _dlfeat; ///< Deallocate Logical Block Features
};

}
