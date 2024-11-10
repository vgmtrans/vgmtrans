/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"
#include "KonamiArcadeInstr.h"
#include "common.h"

class KonamiArcadeSeq;

class KonamiArcadeScanner:
    public VGMScanner {
public:
  explicit KonamiArcadeScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info);
  const std::vector<KonamiArcadeSeq*> loadSeqTable(
    RawFile *file,
    uint32_t offset,
    const std::array<KonamiArcadeInstrSet::drum, 46>& drums,
    float nmiRate
  );
  const std::vector<konami_mw_sample_info> loadSampleInfos(
    RawFile *file,
    u32 tablesOffset,
    u32 drumSampTableOffset,
    u32 drumInstrTableOffset
  );

private:
  static BytePattern ptnSetNmiRate;
  static BytePattern ptnNmiSkip;
};
