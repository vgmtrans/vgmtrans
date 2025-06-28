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

struct MAMEGame;
class KonamiArcadeSeq;

enum KonamiArcadeFormatVer: uint8_t {
  VERSION_UNDEFINED,
  MysticWarrior,
  GX
};

class KonamiArcadeScanner:
    public VGMScanner {
public:
  explicit KonamiArcadeScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info);
  void loadGX(MAMEGame* gameentry, KonamiArcadeFormatVer fmt_ver);
  void loadMysticWarrior(MAMEGame* gameentry, KonamiArcadeFormatVer fmt_ver);
  const std::vector<KonamiArcadeSeq*> loadSeqTable(
    RawFile *file,
    uint32_t offset,
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
