/*
 * Copyright (C) 2020-2021, 2023-2024 Kernkonzept GmbH.
 * Author(s): Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/cxx/bitfield>

#include <functional>

#include <l4/libblock-device/types.h>

namespace Nvme {

using Callback = std::function<void(l4_uint16_t)>;

enum
{
  Aq_id = 0u,   ///< Admin Queue ID
  Ioq_id = 1u,  ///< I/O Queue ID
};

/// Admin Command Set commands
enum Acs
{
  Create_iosq = 1u, ///< Create I/O Submission Queue
  Create_iocq = 5u, ///< Create I/O Completion Queue
  Identify = 6u,
};

enum Cns
{
  Identify_namespace = 0u,
  Identify_controller = 1u,
};

/// I/O Command Set commands
enum Iocs
{
  Write = 1u,
  Read = 2u,
  Write_zeroes = 8u,
};

/// Identify Namespace offsets
enum Cns_in
{
  Nsze = 0u,    ///< Namespace Size
  Ncap = 8u,    ///< Namespace Capacity
  Nuse = 16u,   ///< Namespace Utilization
  Nsfeat = 24u, ///< Namespace Features
  Nlbaf = 25u,  ///< Number of LBA Formats
  Flbas = 26u,  ///< Formatted LBA Size
  Dlfeat = 33u, ///< Deallocate Logical Block Features
  Nsattr = 99u, ///< Namespace Attributes
  Lbaf0 = 128u, ///< LBA Format 0 Support
};

/// Identify Controller offsets
enum Cns_ic
{
  Sn = 4u,   ///< Serial Number
  Mn = 24u,  ///< Model Number
  Fr = 64u,  ///< Firmware Revision
  Mdts = 77u, ///< Maximum Data Transfer Size
  Cntlid = 78u, ///< Controller ID
  Nn = 516u, ///< Number of Namespaces
  Sgls = 536u, ///< SGL Support
};

/** Deallocate Logical Block Features
 */
struct Ns_dlfeat
{
  l4_uint8_t raw;
  CXX_BITFIELD_MEMBER_RO(3, 3, deallocwz, raw);   ///< Deallocate bit supported in Write Zeroes
  CXX_BITFIELD_MEMBER_RO(0, 2, dlbrdbhv, raw);    ///< Deallocated Logical Block Behavior

  explicit Ns_dlfeat(l4_uint8_t v) : raw(v) {}
};

enum Nsattr
{
  Wp = 1u,  ///< Write-protected
};


/** PRP or SGL Data Transfer */
enum Psdt
{
  Use_prps = 0u, ///< PRPs are used
  Use_sgls = 1u, ///< SGLs are used
};

enum Sgl_id
{
  Data = 0u,                 ///< SGL Data Block descriptor with address
  Last_segment_addr = 0x30u, ///< SGL Last Segment descriptor with address
};

/** SGL descriptor */
struct Sgl_desc
{
  l4_uint64_t addr;
  l4_uint32_t len;
  l4_uint8_t res[3];
  l4_uint8_t sgl_id;
} __attribute__ ((aligned(8)));

static_assert(sizeof(Sgl_desc) == 16, "Sgl_desc is 16 bytes");
static_assert(alignof(Sgl_desc) == 8, "Sgl_desc is qword aligned");

struct Prp_list_entry
{
  l4_uint64_t addr;
} __attribute__ ((aligned(8)));

static_assert(sizeof(Prp_list_entry) == 8, "Prp_list_entry is 8 bytes");
static_assert(alignof(Prp_list_entry) == 8, "Prp_list_entru is qword aligned");

/** Controller Capabilities register of a NVMe Controller
 */
struct Ctl_cap
{
  l4_uint64_t raw;
  CXX_BITFIELD_MEMBER_RO(57, 57, cmbs, raw);   ///< Controller Memory Buffer Supported
  CXX_BITFIELD_MEMBER_RO(56, 56, pmrs, raw);   ///< Persistent Memory Region Supported
  CXX_BITFIELD_MEMBER_RO(52, 55, mpsmax, raw); ///< Memory Page Size Maximum
  CXX_BITFIELD_MEMBER_RO(48, 51, mpsmin, raw); ///< Memory Page Size Minimum
  CXX_BITFIELD_MEMBER_RO(45, 45, bps, raw);    ///< Boot Partition Support
  CXX_BITFIELD_MEMBER_RO(37, 44, css, raw);    ///< Command Sets Supported
  CXX_BITFIELD_MEMBER_RO(44, 44, noio_css, raw); ///< No I/O Command Set supported
  CXX_BITFIELD_MEMBER_RO(37, 37, nvm_css, raw); ///< NVM command set supported
  CXX_BITFIELD_MEMBER_RO(36, 36, nssrs, raw);  ///< NVM Subsystem Reset Supported
  CXX_BITFIELD_MEMBER_RO(32, 35, dstrd, raw);  ///< Doorbell Stride
  CXX_BITFIELD_MEMBER_RO(24, 31, to, raw);     ///< Timeout (500-millisecond units)
  CXX_BITFIELD_MEMBER_RO(17, 18, ams, raw);    ///< Arbitration Mechanism Supported
  CXX_BITFIELD_MEMBER_RO(16, 16, cqr, raw);    ///< Contiguous Queues Required
  CXX_BITFIELD_MEMBER_RO(0, 15, mqes, raw);    ///< Maximum Queue Entries Supported

  explicit Ctl_cap(l4_uint64_t v) : raw(v) {}
};

/** Version register of a NVMe Controller
 */
struct Ctl_ver
{
  l4_uint32_t raw;
  CXX_BITFIELD_MEMBER_RO(16, 31, mjr, raw); ///< Major Version Number
  CXX_BITFIELD_MEMBER_RO(8, 15, mnr, raw);  ///< Minor Version Number
  CXX_BITFIELD_MEMBER_RO(0, 7, ter, raw);   ///< Tertiary Version Number

  explicit Ctl_ver(l4_uint32_t v) : raw(v) {}
};

/** Controller Configuration register of a NVMe Controller
 */
struct Ctl_cc
{
  l4_uint32_t raw;
  CXX_BITFIELD_MEMBER(20, 23, iocqes, raw); ///< I/O Completion Queue Entry Size
  CXX_BITFIELD_MEMBER(16, 19, iosqes, raw); ///< I/O Submission Queue Entry Size
  CXX_BITFIELD_MEMBER(14, 15, shn, raw); ///< Shutdown Notification
  CXX_BITFIELD_MEMBER(11, 13, ams, raw); ///< Arbitration Mechanism Selected
  CXX_BITFIELD_MEMBER(7, 10, mps, raw);  ///< Memory Page Size
  CXX_BITFIELD_MEMBER(4, 6, css, raw);   ///< I/O Command Set Selected
  CXX_BITFIELD_MEMBER(0, 0, en, raw);    ///< Enable

  explicit Ctl_cc(l4_uint32_t v) : raw(v) {}
};

/** Controller Status register of a NVMe Controller
 */
struct Ctl_csts
{
  l4_uint32_t raw;
  CXX_BITFIELD_MEMBER_RO(5, 5, pp, raw);     ///< Processing Paused
  CXX_BITFIELD_MEMBER_RO(4, 4, nssro, raw);  ///< NVM Subsystem Reset Occurred
  CXX_BITFIELD_MEMBER_RO(2, 3, shst, raw);   ///< Shutdown Status
  CXX_BITFIELD_MEMBER_RO(1, 1, cfs, raw);    ///< Controller Fatal Status
  CXX_BITFIELD_MEMBER_RO(0, 0, rdy, raw);    ///< Ready

  explicit Ctl_csts(l4_uint32_t v) : raw(v) {}
};

/** Admin Queue Attributes of a NVMe Controller
 */
struct Ctl_aqa
{
  l4_uint32_t raw;
  CXX_BITFIELD_MEMBER(16, 27, acqs, raw); ///< Admin Completion Queue Size
  CXX_BITFIELD_MEMBER(0, 11, asqs, raw);  ///< Admin Submission Queue Size

  explicit Ctl_aqa(l4_uint32_t v) : raw(v) {}
};

namespace Regs {

namespace Ctl {

/** Controller registers */
enum Regs
{
  Cap = 0x00U,   ///< Controller Capabilities
  Vs = 0x08U,    ///< Version
  Intms = 0x0cU, ///< Interrupt Mask Set
  Intmc = 0x10U, ///< Interrupt Mask Clear
  Cc = 0x14U,    ///< Controller Configuration
  Csts = 0x14U,  ///< Controller Status
  Nssr = 0x20U,  ///< NVM Subsystem Reset
  Aqa = 0x24U,   ///< Admin Queue Attributes
  Asq = 0x28U,   ///< Amdin Submission Queue Base Address
  Acq = 0x30U,   ///< Admin Completion Queue Base Address
  Sq0tdbl = 0x1000U ///< Submission Queue 0 Tail Doorbell
};

enum Cc
{
  Ams_rr = 0u,  ///< Arbitration Mechanism Selected: Round Robin
  Css_nvm = 0u, ///< I/O Command Set Selected: NVM Command Set
};

} // namespace Ctl

} // namespace Regs

} // namespace Nvme
