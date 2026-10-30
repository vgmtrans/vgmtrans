/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vgmtrans::tests {

// Keep checks active in Release builds and report the assertion's call site.
inline void expect(bool condition, std::string_view message,
                   std::source_location location = std::source_location::current()) {
  if (!condition) {
    throw std::runtime_error(std::string(location.file_name()) + ":" + std::to_string(location.line()) + ": " +
                             std::string(message));
  }
}

template <class Exception, class Action>
void expectThrows(Action action, std::string_view message,
                  std::source_location location = std::source_location::current()) {
  try {
    action();
  } catch (const Exception&) {
    return;
  }
  expect(false, message, location);
}

}  // namespace vgmtrans::tests

using vgmtrans::tests::expect;
using vgmtrans::tests::expectThrows;
