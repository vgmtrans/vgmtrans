/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PS1SeqScanner.h"

#include <algorithm>
#include <functional>
#include "formats/PS1/PS1Format.h"
#include "PSXSPU.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<PS1SeqScanner> s_ps1seq("PS1");
}

constexpr int SRCH_BUF_SIZE = 0x20000;

void PS1SeqScanner::scan(RawFile* file, void* /*info*/) {
  auto seqs = searchForPS1Seq(file);
  auto vabs = searchForVab(file);
  PSXSampColl::searchForPSXADPCMs(file, PS1Format::name);
}

std::vector<PS1Seq *> PS1SeqScanner::searchForPS1Seq(RawFile *file) {
  std::vector<PS1Seq *> loadedFiles;

  using namespace std::string_literals;
  const std::string signature = "SEQp"s;
  auto it = std::search(file->begin(), file->end(),
                        std::boyer_moore_searcher(signature.rbegin(), signature.rend()));

  while (it != file->end()) {
    PS1Seq *newPS1Seq = new PS1Seq(file, it - file->begin());
    if (newPS1Seq->loadVGMFile()) {
      loadedFiles.push_back(newPS1Seq);
    } else {
      delete newPS1Seq;
    }

    it = std::search(std::next(it), file->end(),
                     std::boyer_moore_searcher(signature.rbegin(), signature.rend()));
  }

  return loadedFiles;
}

std::vector<Vab *> PS1SeqScanner::searchForVab(RawFile *file) {
  std::vector<Vab *> loadedFiles;

  using namespace std::string_literals;
  const std::string signature = "VABp"s;
  auto it = std::search(file->begin(), file->end(),
                        std::boyer_moore_searcher(signature.rbegin(), signature.rend()));

  while (it != file->end()) {
    Vab *newVab = new Vab(file, it - file->begin());
    if (newVab->loadVGMFile()) {
      loadedFiles.push_back(newVab);
    } else {
      delete newVab;
    }

    it = std::search(std::next(it), file->end(),
                     std::boyer_moore_searcher(signature.rbegin(), signature.rend()));
  }

  return loadedFiles;
}
