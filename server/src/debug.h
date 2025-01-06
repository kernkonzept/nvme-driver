/*
 * Copyright (C) 2020, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jakub Jermar <jakub.jermar@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/re/util/debug>

struct Err : L4Re::Util::Err
{
  explicit Err(Level l = Normal) : L4Re::Util::Err(l, "NVMe") {}
};

class Dbg : public L4Re::Util::Dbg
{
public:
  enum Level
  {
    Warn  = 1,
    Info  = 2,
    Trace = 4,
    Steptrace = 8
  };

  Dbg(unsigned long l = Info, char const *subsys = "")
  : L4Re::Util::Dbg(l, "NVMe", subsys) {}

  static Dbg warn(char const *subsys = "")
  { return Dbg(Dbg::Warn, subsys); }

  static Dbg info(char const *subsys = "")
  { return Dbg(Dbg::Info, subsys); }

  static Dbg trace(char const *subsys = "")
  { return Dbg(Dbg::Trace, subsys); }

  static Dbg steptrace(char const *subsys = "")
  { return Dbg(Dbg::Steptrace, subsys); }
};

using Err_blockdev = Err;

struct Dbg_blockdev : L4Re::Util::Dbg
{
  Dbg_blockdev(unsigned long l, char const *subsys)
  : L4Re::Util::Dbg(l, "NVMe", subsys) {}
};

#define LIBBLOCKDEV_DEBUG_ERR Err_blockdev
#define LIBBLOCKDEV_DEBUG_DBG Dbg_blockdev
