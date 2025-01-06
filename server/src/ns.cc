/*
 * Copyright (C) 2020-2024 Kernkonzept GmbH.
 * Author(s): Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include "ns.h"
#include "ctl.h"
#include "queue.h"
#include "debug.h"
#include "inout_buffer.h"

static Dbg trace(Dbg::Trace, "nvme-ns");

namespace Nvme {

Namespace::Namespace(Ctl &ctl, l4_uint32_t nsid, l4_size_t lba_sz,
                     cxx::Ref_ptr<Inout_buffer> const &in)
: _callback(nullptr),
  _ctl(ctl),
  _msi(0),
  _nsid(nsid),
  _lba_sz(lba_sz),
  _dlfeat(0)
{
  _nsze = *in->get<l4_uint64_t>(Cns_in::Nsze);
  _ro = *in->get<l4_uint8_t>(Cns_in::Nsattr) & Nsattr::Wp;
  _dlfeat.raw = *in->get<l4_uint8_t>(Cns_in::Dlfeat);
}

Namespace::~Namespace()
{
  _ctl.free_msi(_msi, this);
}

void
Namespace::async_loop_init(
  l4_uint32_t nsids, std::function<void(cxx::unique_ptr<Namespace>)> callback)
{
  _callback = callback;

  _msi = _ctl.allocate_msi(this);

  _iocq =
    _ctl.create_iocq(qid(), Queue::Ioq_size, _msi, [=](l4_uint16_t status) {
      if (status)
        {
          trace.printf(
            "Create I/O Completion Queue command failed with status=%u\n",
            status);

          // Start identifying the next NSID
          if (_nsid + 1 < nsids)
            _ctl.identify_namespace(nsids, _nsid + 1, callback);

          // Self-destruct
          auto del = cxx::unique_ptr<Namespace>(this);

          return;
        }
      _iosq = _ctl.create_iosq(
        qid(), Queue::Ioq_size, _ctl.supports_sgl() ? Queue::Ioq_sgls : 0,
        [=](l4_uint16_t status) {

          // Start identifying the next NSID
          if (_nsid + 1 < nsids)
            _ctl.identify_namespace(nsids, _nsid + 1, callback);

          if (status)
            {
              trace.printf(
                "Create I/O Submission Queue command failed with status=%u\n",
                status);
              // Self-destruct
              auto del = cxx::unique_ptr<Namespace>(this);
              return;
            }

          _callback(cxx::unique_ptr<Namespace>(this));
        });
    });
}

Queue::Sqe volatile *
Namespace::readwrite_prepare_prp(bool read, l4_uint64_t slba, l4_uint64_t paddr,
                                 l4_size_t sz, Prp_list_entry **prpp) const
{
  auto *sqe = _iosq->produce();
  if (!sqe)
    return 0;

  auto prp_list_start =
    sqe->cid() * Queue::Prp_list_entries * sizeof(Prp_list_entry);

  l4_uint64_t prp2 = l4_trunc_page(paddr + sz - 1);
  if (l4_trunc_page(paddr) == prp2)
    prp2 = 0; // reserved: set to 0
  else if (l4_trunc_page(paddr) != prp2 - L4_PAGESIZE)
    prp2 = _iosq->_prps->pget(prp_list_start);

  sqe->opc() = (read ? Iocs::Read : Iocs::Write);
  sqe->nsid = _nsid;
  sqe->psdt() = Psdt::Use_prps;
  sqe->prp.prp1 = paddr;
  sqe->prp.prp2 = prp2;
  sqe->cdw10 = slba & 0xfffffffful;
  sqe->cdw11 = slba >> 32;
  sqe->cdw13 = 0;
  sqe->cdw14 = 0;
  sqe->cdw15 = 0;
  if (_iosq->_prps)
    *prpp = _iosq->_prps->get<Prp_list_entry>(prp_list_start);
  return sqe;
}

Queue::Sqe volatile *
Namespace::readwrite_prepare_sgl(bool read, l4_uint64_t slba,
                                 Sgl_desc **sglp) const
{
  auto *sqe = _iosq->produce();
  if (!sqe)
    return 0;
  auto sgl_start = sqe->cid() * Queue::Ioq_sgls * sizeof(Sgl_desc);
  sqe->opc() = (read ? Iocs::Read : Iocs::Write);
  sqe->nsid = _nsid;
  sqe->psdt() = Psdt::Use_sgls;
  sqe->sgl1.sgl_id = Sgl_id::Last_segment_addr;
  sqe->sgl1.addr = _iosq->_sgls->pget(sgl_start);
  sqe->cdw10 = slba & 0xfffffffful;
  sqe->cdw11 = slba >> 32;
  sqe->cdw13 = 0;
  sqe->cdw14 = 0;
  sqe->cdw15 = 0;
  *sglp = _iosq->_sgls->get<Sgl_desc>(sgl_start);
  return sqe;
}

void
Namespace::readwrite_submit(Queue::Sqe volatile *sqe, l4_uint16_t nlb, l4_size_t blocks,
                            Callback cb) const
{
  if (sqe->psdt() == Psdt::Use_sgls)
    sqe->sgl1.len = blocks * sizeof(Sgl_desc);
  sqe->nlb() = nlb;
  _iosq->_callbacks[sqe->cid()] = cb;
  _iosq->submit();
}

bool
Namespace::write_zeroes(l4_uint64_t slba, l4_uint16_t nlb, bool dealloc,
                        Callback cb) const
{
  auto *sqe = _iosq->produce();
  if (!sqe)
    return false;

  sqe->opc() = Iocs::Write_zeroes;
  sqe->nsid = _nsid;
  sqe->cdw10 = slba & 0xfffffffful;
  sqe->cdw11 = slba >> 32;
  sqe->nlb() = nlb;
  sqe->deac() = dealloc;
  sqe->cdw14 = 0;
  sqe->cdw15 = 0;
  _iosq->_callbacks[sqe->cid()] = cb;
  _iosq->submit();
  return true;
}

}
