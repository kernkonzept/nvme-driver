/*
 * Copyright (C) 2020 Kernkonzept GmbH.
 * Author(s): Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
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
: _callback(nullptr), _ctl(ctl), _nsid(nsid), _lba_sz(lba_sz), _dlfeat(0)
{
  _nsze = *in->get<l4_uint64_t>(Cns_in::Nsze);
  _ro = *in->get<l4_uint8_t>(Cns_in::Nsattr) & Nsattr::Wp;
  _dlfeat.raw = *in->get<l4_uint8_t>(Cns_in::Dlfeat);
}

void
Namespace::async_loop_init(
  l4_uint32_t nsids, std::function<void(cxx::unique_ptr<Namespace>)> callback)
{
  _callback = callback;
  _iocq =
    _ctl.create_iocq(qid(), Queue::Ioq_size, [=](l4_uint16_t status) {
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

Queue::Sqe *
Namespace::readwrite_prepare_prp(bool read, l4_uint64_t slba, l4_uint64_t paddr,
                                 l4_size_t sz) const
{
  l4_uint64_t prp2 = l4_trunc_page(paddr + sz - 1);
  if (l4_trunc_page(paddr) == prp2)
    prp2 = 0; // reserved: set to 0
  else if (l4_trunc_page(paddr) != prp2 - 1)
    return 0; // unsupported: would need a PRP list

  auto *sqe = _iosq->produce();
  if (!sqe)
    return 0;
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
  return sqe;
}

Queue::Sqe *
Namespace::readwrite_prepare_sgl(bool read, l4_uint64_t slba,
                                 Sgl_desc **sglp) const
{
  auto *sqe = _iosq->produce();
  if (!sqe)
    return 0;
  sqe->opc() = (read ? Iocs::Read : Iocs::Write);
  sqe->nsid = _nsid;
  sqe->psdt() = Psdt::Use_sgls;
  sqe->sgl1.sgl_id = Sgl_id::Last_segment_addr;
  sqe->sgl1.addr = _iosq->_sgls->pget(sqe->cid() * Queue::Ioq_sgls);
  sqe->cdw10 = slba & 0xfffffffful;
  sqe->cdw11 = slba >> 32;
  sqe->cdw13 = 0;
  sqe->cdw14 = 0;
  sqe->cdw15 = 0;
  *sglp = _iosq->_sgls->get<Sgl_desc>(sqe->cid() * Queue::Ioq_sgls);
  return sqe;
}

void
Namespace::readwrite_submit(Queue::Sqe *sqe, l4_uint16_t nlb, l4_size_t blocks,
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
