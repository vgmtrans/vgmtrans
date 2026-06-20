/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/formats/Akao/AkaoTypes.h"

#include <optional>

namespace vgmtrans::formats::akao {

inline constexpr u32 kAkaoSignature = 0x414B414F;
inline constexpr u32 kAkaoPpqn = 0x30;
inline constexpr u32 kAkaoMaxAnalysisCommands = 65536;
inline constexpr u32 kAkaoMaxTrackCommands = 262144;

struct AkaoCommandEffect {
  u32 size = 1;
  bool terminal = false;
  std::optional<u32> jump;
  std::optional<u32> branch;
  bool usesIndividualArts = false;
  std::optional<u32> individualArtId;
  std::optional<u32> customInstrumentOffset;
  std::optional<u32> drumInstrumentOffset;
};

[[nodiscard]] bool isAkaoSubEventPrefix(AkaoPs1Version version, u8 status);
[[nodiscard]] bool isAkaoNoteOpcode(AkaoPs1Version version, u8 status);
[[nodiscard]] u32 akaoRelativeDestination(u32 operandOffset, s16 relative, AkaoPs1Version version);
[[nodiscard]] u32 akaoDirectOperandBytes(AkaoPs1Version version, u8 status);
[[nodiscard]] u32 akaoSubOperandBytes(AkaoPs1Version version, u8 sub);
[[nodiscard]] AkaoCommandEffect inspectAkaoCommand(core::ByteReader reader, u32 offset, AkaoPs1Version version,
                                                   u32 sequenceEnd);
void analyzeAkaoTrack(core::ByteReader reader, AkaoSequenceAnalysis& analysis, u32 start);

[[nodiscard]] u32 akaoTempoMicrosPerQuarter(AkaoPs1Version version, u16 tempo);

}  // namespace vgmtrans::formats::akao
