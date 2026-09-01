/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/InstrumentIdentity.h"
#include "value/scan/CollectionDiscovery.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::konami_ps1 {

inline constexpr std::string_view kKonamiPs1FormatName = "KonamiPS1";
inline constexpr std::string_view kKonamiPs1CollectionResolver = "konami-ps1";
inline constexpr std::string_view kKonamiPs1CommandKindPrefix = "konami-ps1:sequence";

enum class EventKind {
  Note,
  Controller,
  SetChannel,
  Tempo,
  PitchBend,
  Program,
  NoteOff,
  End,
};

struct EventLayout {
  u32 offset = 0;
  u32 end = 0;
  u32 delta = 0;
  EventKind kind = EventKind::Controller;
  u8 command = 0;
  u8 value = 0;
  bool chained = false;
  std::optional<u32> loopDestination;
  u8 loopCount = 0;
};

struct TrackLayout {
  u32 offset = 0;
  u32 end = 0;
  std::vector<EventLayout> events;
};

struct SequenceLayout {
  u32 containerOffset = 0;
  u32 containerLength = 0;
  u32 offset = 0;
  u32 length = 0;
  u32 sequenceId = 0;
  bool hasKdt2Header = false;
  u8 version = 1;
  u32 ppqn = 480;
  std::vector<TrackLayout> tracks;
};

struct Tone {
  u16 bank = 0;
  u8 program = 0;
  u8 index = 0;
  u8 keyLow = 0;
  u8 keyHigh = 127;
  u8 flags = 0;
  u8 bendDown = 2;
  u8 bendUp = 2;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  u16 originalAdsr1 = 0;
  u16 originalAdsr2 = 0;
  bool dynamicAdsr = false;
};

[[nodiscard]] std::optional<SequenceLayout> readKonamiPs1SequenceLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<SequenceLayout> findKonamiPs1Sequences(core::ByteReader reader);
[[nodiscard]] std::optional<u16> findKonamiPs1RootCounterTarget(core::ByteReader reader);
[[nodiscard]] std::vector<Tone> readKonamiPs1Tones(core::ByteReader reader);
[[nodiscard]] core::SequenceProgram parseKonamiPs1Sequence(core::ByteReader reader, core::AssetId id,
                                                           const SequenceLayout& layout, u16 rootCounterTarget,
                                                           std::vector<Tone> tones = {},
                                                           core::SourceMapBuilder* sourceMap = nullptr,
                                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& konamiPs1SequenceConfig();
[[nodiscard]] std::vector<core::DesiredCollection> resolveKonamiPs1Collections(
    const core::CollectionDiscoveryContext& context);
[[nodiscard]] core::FormatModule konamiPs1Module();

}  // namespace vgmtrans::formats::konami_ps1
