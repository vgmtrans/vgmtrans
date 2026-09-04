/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueFormatCorpus.h"
#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"

#include <exception>
#include <iostream>

void runWolfTeamSnesModuleTests();

int main(int argc, char** argv) {
  try {
    if (argc == 2) {
      return vgmtrans::tests::scanValueFormatArchive(argv[1], {.format = "WolfTeamSnes"});
    }
    runWolfTeamSnesModuleTests();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
