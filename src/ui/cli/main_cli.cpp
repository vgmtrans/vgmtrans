/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <ghc/filesystem.hpp>
#include <stdlib.h>

#include "pch.h"
#include "CLIVGMRoot.h"
#include "VGMColl.h"
#include "VGMExport.h"

using namespace std;
namespace fs = ghc::filesystem;

int main(int argc, char *argv[]) {

  // set global switch
  g_isCliMode = true;

  for(int i = 1; i < argc; ++i) {
    string s(argv[i]);
    if ((s == "-h") || (s == "--help")) {
      cliroot.DisplayHelp();
      return EXIT_SUCCESS;
    }
    else if (s == "-o") {
      if (i == argc - 1) {
        cerr << "Error: expected output directory" << endl;
        cliroot.DisplayUsage();
        return EXIT_FAILURE;
      }
      else {
        cliroot.outputDir = fs::path(argv[++i]);
      }
    }
    else {
      cliroot.inputFiles.insert(fs::path(s));
    }
  }

  bool success = false;

  if (cliroot.Init()) {
    if (cliroot.ExportAllCollections()) {
      success = true;
    }
  }

  cliroot.Exit();

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
