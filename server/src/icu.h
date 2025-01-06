/*
 * Copyright (C) 2019, 2022, 2024 Kernkonzept GmbH.
 * Author(s): Matthias Lange <matthias.lange@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#pragma once

#include <l4/vbus/vbus>
#include <l4/cxx/bitmap>

#include <mutex>
#include <utility>

#include "msi_allocator.h"

namespace Nvme
{

class Icu : public Msi::Msi_allocator, public cxx::Ref_obj
{
public:
  Icu(L4::Cap<L4::Icu> icu) : _icu(icu)
  {
    L4Re::chksys(l4_error(_icu->info(&_icu_info)), "Retrieving ICU infos");
    _msis.set_msi_limit(_icu_info.nr_msis);
  }

  L4::Cap<L4::Icu> icu() const override { return _icu; }

  /**
   * Does the ICU support MSIs
   *
   * \retval true   ICU supports MSIs.
   * \retval false  ICU does not support MSIs.
   */
  bool msis_supported() const
  {
    return ((_icu_info.features & L4::Icu::F_msi) && (_icu_info.nr_msis > 0));
  }

private:
  class Msi_bitmap
  {
    enum { Num_msis = 2048 };

  public:
    Msi_bitmap() : _max_available(0) { _m.clear_all(); }

    /**
     * Set the maximum number of available MSIs.
     *
     * \param max_avail  Number of MSIs supperted by the vBus ICU.
     */
    void set_msi_limit(unsigned max_avail)
    {
      // Only allow to set the limit once. It is only necessary once.
      assert(_max_available == 0);

      _max_available = max_avail;

      if (_max_available > Num_msis)
        L4Re::Util::Dbg().printf(
          "Msi_bitmap: ICU supported number of MSIs is greater than "
          "the number the allocator supports.");
    }

    long alloc()
    {
      std::lock_guard<std::mutex> lock(_mut);

      long num = _m.scan_zero();

      if (num <= -1 || (unsigned)num >= _max_available)
        return -L4_ENOMEM;

      _m[num] = 1;
      return num;
    }

    void free(unsigned num)
    {
      std::lock_guard<std::mutex> lock(_mut);
      _m[num] = 0;
    }

    unsigned limit() const { return _max_available; }

  private:
    cxx::Bitmap<Num_msis> _m;
    unsigned _max_available;
    std::mutex _mut;
  };

public:
  /// Allocate a MSI vector with the app global MSI allocator.
  long alloc_msi() override { return _msis.alloc(); }
  /// Free a previously allocated MSI vector.
  void free_msi(unsigned num) override { _msis.free(num); }
  /// Maximum number of MSIs at the ICU.
  unsigned max_msis() const override { return _msis.limit(); }

private:
  L4::Cap<L4::Icu> _icu;
  Msi_bitmap _msis;
  l4_icu_info_t _icu_info;
};

} // namespace Nvme
