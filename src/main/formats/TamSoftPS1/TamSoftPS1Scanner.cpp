/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "TamSoftPS1Seq.h"

#include <spdlog/fmt/fmt.h>
#include "TamSoftPS1Instr.h"
#include "ScannerManager.h"
namespace vgmtrans::scanners {
ScannerRegistration<TamSoftPS1Scanner> s_tamsoft_ps1("TAMSOFTPS1", {"tsq", "tvb"});
}

void TamSoftPS1Scanner::Scan(RawFile *file, void *info) {
  std::string basename(file->stem());
  std::string extension(file->extension());

  if (extension == "tsq") {
    uint8_t numSongs = 0;
    uint32_t seqHeaderBoundaryOffset = 0xffffffff;
    for (numSongs = 0; numSongs < 128; numSongs++) {
      uint32_t dwSongItemOffset = numSongs * 4;
      if (dwSongItemOffset >= seqHeaderBoundaryOffset) {
        break;
      }

      uint32_t a32 = file->GetWord(dwSongItemOffset);
      if (a32 == 0xfffff0) {
        break;
      }
      if (a32 == 0) {
        continue;
      }

      uint16_t seqHeaderRelOffset = file->GetWord(dwSongItemOffset + 2);
      if (seqHeaderBoundaryOffset > seqHeaderRelOffset) {
        seqHeaderBoundaryOffset = seqHeaderRelOffset;
      }
    }

    for (uint8_t songIndex = 0; songIndex < numSongs; songIndex++) {
      std::string seqname = fmt::format("{} ({})", basename, songIndex);
      TamSoftPS1Seq *newSeq = new TamSoftPS1Seq(file, 0, songIndex, seqname);
      if (newSeq->LoadVGMFile()) {
        newSeq->unLength = file->size();
      } else {
        delete newSeq;
      }
    }
  } else if (extension == "tvb" || extension == "tvb2") {
    bool ps2 = false;
    if (extension == "tvb2") {
      // note: this is not a real extension
      ps2 = true;
    }

    TamSoftPS1InstrSet *newInstrSet = new TamSoftPS1InstrSet(file, 0, ps2, basename);
    if (newInstrSet->LoadVGMFile()) {
      newInstrSet->unLength = file->size();
    } else {
      delete newInstrSet;
    }
  }

  return;
}
