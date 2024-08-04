/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "KonamiPS1Seq.h"
#include <spdlog/fmt/fmt.h>
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<KonamiPS1Scanner> s_konami_ps1("KonamiPS1");
}

void KonamiPS1Scanner::scan(RawFile *file, void *info) {
  uint32_t offset = 0;
  int numSeqFiles = 0;
  while (offset < file->size()) {
    if (KonamiPS1Seq::isKDT1Seq(file, offset)) {
      std::string name = file->tag.hasTitle() ? file->tag.title : file->stem();
      if (numSeqFiles >= 1) {
        name += fmt::format("({})", numSeqFiles + 1);
      }

      KonamiPS1Seq *newSeq = new KonamiPS1Seq(file, offset, name);
      if (newSeq->loadVGMFile()) {
        offset += newSeq->unLength;
        numSeqFiles++;
        continue;
      } else {
        delete newSeq;
      }
    }
    offset++;
  }
}
