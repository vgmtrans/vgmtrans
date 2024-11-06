/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "common.h"
#include "Scanner.h"

class KonamiArcadeSeq;

struct konami_mw_sample_info {
  enum class sample_type: u8 {
    PCM_8 = 0,
    PCM_16 = 4,
    ADPCM = 8,
  };

  u8 loop_lsb;
  u8 loop_mid;
  u8 loop_msb;
  u8 start_lsb;
  u8 start_mid;
  u8 start_msb;
  sample_type type;
  bool loops;
  u8 unknown;
};

class KonamiArcadeScanner:
    public VGMScanner {
public:
  explicit KonamiArcadeScanner(Format* format) : VGMScanner(format) {}

  virtual void scan(RawFile *file, void *info = 0);
  std::vector<KonamiArcadeSeq*> loadSeqTable(RawFile *file, uint32_t offset);
  std::vector<konami_mw_sample_info> loadSampleInfos(
    RawFile *file,
    u32 tablesOffset,
    u32 drumSampTableOffset,
    u32 drumInstrTableOffset
  );

};
