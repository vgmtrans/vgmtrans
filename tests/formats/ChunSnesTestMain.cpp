/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueFormatCorpus.h"
#include "value/formats/ChunSnes/ChunSnes.h"

#include <exception>
#include <iostream>

void runChunSnesModuleTests();

int main(int argc, char** argv) {
  try {
    if (argc == 2) {
      return vgmtrans::tests::scanValueFormatArchive(argv[1], {.format = "ChunSnes"});
    }
    runChunSnesModuleTests();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
