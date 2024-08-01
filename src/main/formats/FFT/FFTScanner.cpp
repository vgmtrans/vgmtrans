/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "FFTSeq.h"
#include "FFTInstr.h"
#include "FFTScanner.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<FFTScanner> s_fft_snes("FFT");
}

#define SRCH_BUF_SIZE 0x20000

//==============================================================
//		scan "smds" and "wds"
//--------------------------------------------------------------
void FFTScanner::scan(RawFile *file, void *info) {
  searchForFFTSeq(file);
  searchForFFTwds(file);
}

//==============================================================
//		scan "smds"		(Sequence)
//--------------------------------------------------------------
void FFTScanner::searchForFFTSeq(RawFile *file) {
  size_t nFileLength = file->size();
  for (uint32_t i = 0; i + 4 < nFileLength; i++) {
    if (file->readWordBE(i) != 0x736D6473)
      continue;
    if (file->readShort(i + 10) != 0 && file->readShort(i + 16) != 0)
      continue;

    FFTSeq *NewFFTSeq = new FFTSeq(file, i);
    if (!NewFFTSeq->loadVGMFile())
      delete NewFFTSeq;
  }
}

//==============================================================
//		scan "wds"		(Instrumnt)
//--------------------------------------------------------------
//	memo:
//		object "SampColl" は、class "WdsInstrSet"内で生成する。
//--------------------------------------------------------------
void FFTScanner::searchForFFTwds(RawFile *file) {
  size_t nFileLength = file->size();
  for (uint32_t i = 0; i + 0x30 < nFileLength; i++) {
    uint32_t sig = file->readWordBE(i);
    if (sig != 0x64776473 && sig != 0x77647320)
      continue;

    // The sample collection size must not be impossibly large
    if (file->readWord(i + 0x14) > 0x100000)
      continue;

    uint32_t hdrSize = file->readWord(i + 0x10);
    // First 0x10 bytes of sample section should be 0s
    if (file->readWord(i + hdrSize) != 0 || file->readWord(i + hdrSize + 4) != 0 ||
        file->readWord(i + hdrSize + 8) != 0 || file->readWord(i + hdrSize + 12) != 0)
      continue;

    //if (size <= file->GetWord(i+0x10) || size <= file->GetWord(i+0x18))
    //	continue;

    WdsInstrSet *newWds = new WdsInstrSet(file, i);
    if (!newWds->loadVGMFile())
      delete newWds;
  }
}
