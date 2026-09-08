/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PS1SeqScanner.h"

#include "formats/PS1/PS1Format.h"
#include "formats/PSDSE/PSDSEPS2Header.h"
#include "PSXSPU.h"
#include "ScannerManager.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace vgmtrans::scanners {
ScannerRegistration<PS1SeqScanner> s_ps1seq("PS1");
}

void PS1SeqScanner::scan(RawFile* file, void* /*info*/) {
  auto seqs = searchForPS1Seq(file);
  auto vabs = searchForVab(file);

  std::vector<std::pair<u32, u32>> dseBankRanges;
  for (u32 offset = 0; offset + 4 <= file->size(); ++offset) {
    if (file->readWordBE(offset) != PSDSEPS2::kSwdmMagic) {
      continue;
    }
    PSDSEPS2::BankHeader header;
    if (!header.read(file, offset)) {
      continue;
    }
    // [Shadow Hearts]: SOUND.PKB contains hundreds of SWDM sound-effect banks whose PS-ADPCM payloads are already
    // represented by DSE sample collections. Scanning the same bytes as headerless PSX collections duplicates the
    // samples and consumes hundreds of megabytes of RAM.
    dseBankRanges.emplace_back(offset, offset + header.fileLength);
    offset += header.fileLength - 1;
  }
  PSXSampColl::searchForPSXADPCMs(file, PS1Format::name, dseBankRanges);
}

std::vector<PS1Seq*> PS1SeqScanner::searchForPS1Seq(RawFile* file) {
  std::vector<PS1Seq*> loadedFiles;

  using namespace std::string_literals;
  const std::string signature = "SEQp"s;
  auto it = std::search(file->begin(), file->end(), std::boyer_moore_searcher(signature.rbegin(), signature.rend()));

  while (it != file->end()) {
    auto* newPS1Seq = pRoot->loadVGMFile<PS1Seq>(file, it - file->begin());
    if (newPS1Seq) {
      loadedFiles.push_back(newPS1Seq);
    }

    it = std::search(std::next(it), file->end(), std::boyer_moore_searcher(signature.rbegin(), signature.rend()));
  }

  return loadedFiles;
}

std::vector<Vab*> PS1SeqScanner::searchForVab(RawFile* file) {
  std::vector<Vab*> loadedFiles;

  using namespace std::string_literals;
  const std::string signature = "VABp"s;
  auto it = std::search(file->begin(), file->end(), std::boyer_moore_searcher(signature.rbegin(), signature.rend()));

  while (it != file->end()) {
    auto* newVab = pRoot->loadVGMFile<Vab>(file, it - file->begin());
    if (newVab) {
      loadedFiles.push_back(newVab);
    }

    it = std::search(std::next(it), file->end(), std::boyer_moore_searcher(signature.rbegin(), signature.rend()));
  }

  return loadedFiles;
}
