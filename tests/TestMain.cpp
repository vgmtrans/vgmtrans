/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <exception>
#include <iostream>

// CMake supplies the suite function for test files without a corpus CLI.
void VGMTRANS_TEST_SUITE();

int main() {
  try {
    VGMTRANS_TEST_SUITE();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
