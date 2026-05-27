/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "base/Types.h"
#include "BytePattern.h"
#include "KonamiArcadeInstr.h"
#include "Scanner.h"

#include <array>
#include <string>
#include <vector>

struct MAMEGame;
class KonamiArcadeSeq;

enum KonamiArcadeFormatVer: u8 {
  VERSION_UNDEFINED,
  MysticWarrior,
  GX
};

class KonamiArcadeScanner:
    public VGMScanner {
public:
  explicit KonamiArcadeScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info);
  const std::vector<KonamiArcadeSeq*> loadSeqTable(
    RawFile *file,
    u32 offset,
    const std::array<KonamiArcadeInstrSet::drum, 46>& drums,
    float nmiRate,
    std::string gameName,
    KonamiArcadeFormatVer fmtVer
  );
  const std::vector<konami_mw_sample_info> loadSampleInfos(
    RawFile *file,
    u32 tablesOffset,
    u32 drumSampTableOffset,
    u32 drumInstrTableOffset,
    KonamiArcadeFormatVer fmtVer
  );

private:
  static BytePattern ptn_MW_SetNmiRate;
  static BytePattern ptn_MW_NmiSkip;
  static BytePattern ptn_GX_SetNmiRate;
  static BytePattern ptn_GX_setSeqPlaylistTable;
  static BytePattern ptn_GX_setSampInfoSetPtrTable;
  static BytePattern ptn_GX_setDrumkitPtrs;
};
