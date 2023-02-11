/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "pch.h"
#include "CLIVGMRoot.h"
#include "VGMColl.h"
#include "VGMExport.h"

using namespace std;

int main(int argc, char *argv[]) {

  for(int i = 1; i < argc; ++i) {
    string s(argv[i]);
    if (s == "-o") {
      if (i == argc - 1) {
        cliroot.DisplayUsage();
        cerr << "error: expected output directory" << endl;
        return EXIT_FAILURE;
      }
      else {
        cliroot.outputDir = std::string(argv[++i]);
      }
    }
    else {
      cliroot.inputFiles.push_back(s);
    }
  }

  bool success = false;

  if (cliroot.Init()) {
    if (cliroot.ConvertAllCollections()) {
      success = true;
    }
  }

  cliroot.Exit();

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
