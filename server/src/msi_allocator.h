/*
 * Copyright (C) 2019, 2024 Kernkonzept GmbH.
 * Author(s): Philipp Eppelt <philipp.eppelt@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/sys/icu>

namespace Msi {

/// Interface for access to the MSI-X allocator and the ICU providing the MSIs.
class Msi_allocator
{
public:
  virtual ~Msi_allocator() = 0;

  /// Access to the ICU providing the MSIs.
  virtual L4::Cap<L4::Icu> icu() const = 0;

  /**
   * Allocate a MSI vector with the app global MSI allocator.
   *
   * \retval >= 0        MSI number at the ICU.
   * \retval -L4_ENOMEM  Currently, no MSI is available.
   */
  virtual long alloc_msi() = 0;

  /// Free a previously allocated MSI vector.
  virtual void free_msi(unsigned num) = 0;

  /// Maximum number of MSIs at the ICU.
  virtual unsigned max_msis() const = 0;
};

inline Msi_allocator::~Msi_allocator() = default;

} // namespace Msi
