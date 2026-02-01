/**
 * VGMTrans (c) - 2002-2026
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "DBGVGMRoot.h"
#include <iostream>
#include "LogManager.h"
#include "VGMFile.h"

DBGVGMRoot dbgRoot;

DBGVGMRoot::DBGVGMRoot() {
}

bool DBGVGMRoot::init() {
  return VGMRoot::init();
}

void DBGVGMRoot::UI_setRootPtr(VGMRoot** theRoot) {
  *theRoot = &dbgRoot;
}

void DBGVGMRoot::UI_log(LogItem* theLog) {
  // Simple stdout logging for now, maybe filtered
  if (theLog->logLevel() <= LOG_LEVEL_WARN) {
    std::cout << "[" << theLog->source() << "] " << theLog->text() << std::endl;
  }
}

std::filesystem::path DBGVGMRoot::UI_getSaveFilePath(const std::string& suggestedFilename,
                                                     const std::string& extension) {
  // For debug tool, mainly specialized dumps.
  // If needed, can prompt user or use current dir.
  return std::filesystem::path(suggestedFilename).replace_extension(extension);
}

std::filesystem::path DBGVGMRoot::UI_getSaveDirPath(const std::filesystem::path& suggestedDir) {
  return std::filesystem::current_path();
}

std::vector<VGMFile*> DBGVGMRoot::getLoadedFiles() {
  std::vector<VGMFile*> files;
  for (auto& variant : vgmFiles()) {
    VGMFile* file = variantToVGMFile(variant);
    if (file) {
      files.push_back(file);
    }
  }
  return files;
}
