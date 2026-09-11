/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <exception>
#include <iostream>

void runCompileSnesModuleTests();

int main() {
  try {
    runCompileSnesModuleTests();
  } catch (const std::exception& error) {
    std::cerr << "CompileSnes tests failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
