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
//		Constructor
//--------------------------------------------------------------
FFTScanner::FFTScanner() {}

//==============================================================
//		Destructor
//--------------------------------------------------------------
FFTScanner::~FFTScanner() {}

//==============================================================
//		scan "smds" and "wds"
//--------------------------------------------------------------
void FFTScanner::Scan(RawFile *file, void *info) {
  SearchForFFTSeq(file);
  SearchForFFTwds(file);
}

//==============================================================
//		scan "smds"		(Sequence)
//--------------------------------------------------------------
void FFTScanner::SearchForFFTSeq(RawFile *file) {
  uint32_t nFileLength = file->size();
  for (uint32_t i = 0; i + 4 < nFileLength; i++) {
    if (file->GetWordBE(i) != 0x736D6473)
      continue;
    if (file->GetShort(i + 10) != 0 && file->GetShort(i + 16) != 0)
      continue;

    FFTSeq *NewFFTSeq = new FFTSeq(file, i);
    if (!NewFFTSeq->LoadVGMFile())
      delete NewFFTSeq;
  }
}

//==============================================================
//		scan "wds"		(Instrumnt)
//--------------------------------------------------------------
//	memo:
//		object "SampColl" は、class "WdsInstrSet"内で生成する。
//--------------------------------------------------------------
void FFTScanner::SearchForFFTwds(RawFile *file) {
  uint32_t nFileLength = file->size();
  for (uint32_t i = 0; i + 0x30 < nFileLength; i++) {
    uint32_t sig = file->GetWordBE(i);
    if (sig != 0x64776473 && sig != 0x77647320)
      continue;

    // The sample collection size must not be impossibly large
    if (file->GetWord(i + 0x14) > 0x100000)
      continue;

    uint32_t hdrSize = file->GetWord(i + 0x10);
    // First 0x10 bytes of sample section should be 0s
    if (file->GetWord(i + hdrSize) != 0 || file->GetWord(i + hdrSize + 4) != 0 ||
        file->GetWord(i + hdrSize + 8) != 0 || file->GetWord(i + hdrSize + 12) != 0)
      continue;

    //if (size <= file->GetWord(i+0x10) || size <= file->GetWord(i+0x18))
    //	continue;

    WdsInstrSet *newWds = new WdsInstrSet(file, i);
    if (!newWds->LoadVGMFile())
      delete newWds;
  }
}
