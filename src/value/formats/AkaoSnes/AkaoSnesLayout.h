/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/base/Source.h"
#include "value/formats/AkaoSnes/AkaoSnesTypes.h"

#include <optional>

namespace vgmtrans::formats::akao_snes {

struct AkaoSnesLayout {
  AkaoSnesVersion version = AKAOSNES_NONE;
  AkaoSnesMinorVersion minorVersion = AKAOSNES_NOMINORVERSION;
  u32 sequenceHeaderAddress = 0;
  u32 apuRelocBase = 0;
  bool relocatable = false;
  u32 vcmdAddressTable = 0;
  u32 vcmdLengthTable = 0;
  std::optional<u32> spcDirAddress;
  std::optional<u32> tuningTableAddress;
  std::optional<u32> adsrTableAddress;
  std::optional<u32> percussionTableAddress;
};

[[nodiscard]] std::optional<AkaoSnesLayout> findAkaoSnesLayout(core::ByteReader reader);

}  // namespace vgmtrans::formats::akao_snes
