/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/base/Source.h"
#include "value/formats/KonamiSnes/KonamiSnesTypes.h"

#include <optional>

namespace vgmtrans::formats::konami_snes {

struct KonamiSnesLayout {
  KonamiSnesVersion version = KONAMISNES_NONE;
  bool hasSongList = false;
  u32 sequenceHeaderAddress = 0;
  u8 vcmdLengthItemSize = 0;
  u8 vcmd6xCountInList = 0;
  std::optional<u32> spcDirAddress;
  std::optional<u32> commonInstrumentTableAddress;
  std::optional<u32> bankedInstrumentTableAddress;
  u8 firstBankedInstrument = 0;
  std::optional<u32> percussionInstrumentTableAddress;
};

[[nodiscard]] std::optional<KonamiSnesLayout> findKonamiSnesLayout(core::ByteReader reader);
[[nodiscard]] const char* konamiSnesVersionName(KonamiSnesVersion version);

}  // namespace vgmtrans::formats::konami_snes
