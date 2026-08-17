/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <type_traits>

namespace vgmtrans::core::detail {

// Process-local identity for an erased C++ type. Values using these tokens are
// deliberately not serializable and never cross a process boundary.
template <typename T>
inline constexpr unsigned char kTypeToken = 0;

template <typename T>
[[nodiscard]] constexpr const void* typeToken() noexcept {
  return &kTypeToken<std::remove_cvref_t<T>>;
}

}  // namespace vgmtrans::core::detail
