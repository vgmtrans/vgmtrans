/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Scanner.h"
#include "BytePattern.h"
#include <vector>

class KonamiTMNT2Seq;
class MAMEGame;
struct MAMERomGroup;
struct konami_tmnt2_drum_info;

enum KonamiTMNT2FormatVer: uint8_t {
  VERSION_UNDEFINED,
  TMNT2,
  SSRIDERS,
  VENDETTA,
};

class KonamiTMNT2Scanner : public VGMScanner {
 public:
  explicit KonamiTMNT2Scanner(Format* format) : VGMScanner(format) {}

  std::vector<std::vector<konami_tmnt2_drum_info>> loadDrumTables(
    RawFile* programRom,
    std::vector<u32> drumTablePtrs,
    u32 minDrumPtr
  );

  void scan(RawFile *file, void *info) override;

private:
  std::vector<KonamiTMNT2Seq*> loadSeqTable(
    RawFile* programRom,
    u32 seqTableAddr,
    KonamiTMNT2FormatVer fmtVer,
    u8 defaultTickSkipInterval,
    const std::string& gameName
  );
  void scanTMNT2(
    MAMERomGroup* programRomGroup,
    MAMERomGroup* sampsRomGroup,
    KonamiTMNT2FormatVer fmtVer,
    const std::string& name
  );
  void scanVendetta(
    MAMERomGroup* programRomGroup,
    MAMERomGroup* sampsRomGroup,
    KonamiTMNT2FormatVer fmtVer,
    const std::string& name
  );

  static BytePattern ptn_tmnt2_LoadSeqTable;
  static BytePattern ptn_simp_LoadSeqTable;
  static BytePattern ptn_moomesa_LoadSeqTable;
  static BytePattern ptn_tmnt2_LoadInstrTable;
  static BytePattern ptn_tmnt2_LoadDrumTable;
  static BytePattern ptn_tmnt2_LoadYM2151InstrTable;
  static BytePattern ptn_ssriders_LoadYM2151InstrTable;
};
