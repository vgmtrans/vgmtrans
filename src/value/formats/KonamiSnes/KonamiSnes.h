/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/base/Source.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceDialect.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::core {
class FormatRegistry;
}

namespace vgmtrans::formats::konami_snes {

enum KonamiSnesVersion : u8 {
  KONAMISNES_NONE = 0,
  KONAMISNES_V1,
  KONAMISNES_V2,
  KONAMISNES_V3,
  KONAMISNES_V4,
  KONAMISNES_V5,
  KONAMISNES_V6,
};

inline constexpr u32 kKonamiSnesAramSize = 0x10000;
inline constexpr u32 kKonamiSnesMaxTracks = 8;
inline constexpr u16 kKonamiSnesPpqn = 48;
inline constexpr u16 kKonamiSnesDefaultPitchBendRangeCents = 200;
inline constexpr u8 kKonamiSnesDefaultTempo = 0xff;
inline constexpr double kKonamiSnesTimerHz = 250.0;
inline constexpr u8 kDefaultVibratoMaxDepth = 0xff;
inline constexpr u8 kDefaultLegacyVibratoMaxRateStep = 0x7f;
inline constexpr u8 kMinVibratoMaxDepth = 0x10;
inline constexpr u8 kMinVibratoMaxRateStep = 0x09;
inline constexpr u8 kLateEraVibratoFadeThreshold = 0xc8;

// Versions 1-3 store an extra GAIN byte in each instrument row. Later
// versions reuse the ADSR2 byte as GAIN when ADSR is disabled.
[[nodiscard]] constexpr bool usesLegacyInstrumentLayout(KonamiSnesVersion version) {
  return version >= KONAMISNES_V1 && version <= KONAMISNES_V3;
}

[[nodiscard]] constexpr u32 instrumentHeaderSize(KonamiSnesVersion version) {
  return usesLegacyInstrumentLayout(version) ? 8 : 7;
}

[[nodiscard]] constexpr u8 noteDurationRateMax(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 ? 100 : 127;
}

// The first engine runs its music timer at half the rate used by later games.
// Tempo conversion must account for that before producing MIDI timing.
[[nodiscard]] constexpr u8 timerFrequency(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 ? 0x20 : 0x40;
}

struct KonamiVibratoSpec {
  double maxDepthCents = 0.0;
  double minHertz = 0.0;
  double maxHertz = 0.0;
  double minDelaySeconds = 0.0;
  double maxDelaySeconds = 0.0;
};

namespace vibrato {

// Versions 1-2 advance vibrato with a tempo-dependent counter. Versions 3-6
// use a fixed-rate counter, so the same bytes need different conversions.
[[nodiscard]] constexpr bool usesLegacy(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 || version == KONAMISNES_V2;
}

[[nodiscard]] constexpr u8 legacyRateStep(u8 rate) {
  // Values above 0x80 run the same triangle wave in the opposite direction.
  // Folding them around 0x80 gives the actual speed in either direction.
  return (rate == 0 || rate == 0x80) ? 0 : static_cast<u8>((rate < 0x80) ? rate : (0x100 - rate));
}

[[nodiscard]] constexpr u8 lateEraRateStep(u8 rate) {
  // Later drivers take larger counter steps as the rate enters each higher
  // range. This is why the frequency is not simply proportional to `rate`.
  return (rate == 0xff) ? 16 : (rate >= 0x80) ? 8 : (rate >= 0x40) ? 4 : (rate >= 0x20) ? 2 : 1;
}

[[nodiscard]] constexpr bool isActive(KonamiSnesVersion version, u8 rate, u8 depth) {
  return depth != 0 && (usesLegacy(version) ? (legacyRateStep(rate) != 0) : (rate != 0));
}

[[nodiscard]] inline double maxDepthCents(KonamiSnesVersion version, u8 depth) {
  // Both engines deliberately change scale at 0x80. Keeping the two pieces
  // explicit mirrors the driver and avoids smoothing over that discontinuity.
  if (usesLegacy(version)) {
    return (depth < 0x80) ? (depth * (100.0 / 32.0)) : (depth * (100.0 / 8.0));
  }
  return (depth < 0x80) ? (depth * (100.0 / 128.0)) : ((depth - 126.0) * 50.0);
}

[[nodiscard]] inline double currentDepthCents(KonamiSnesVersion version, u8 targetDepth, u16 currentDepth) {
  // Fades keep eight extra bits for fractions, while the command stores only
  // the target byte. Convert the live value using the target's scale.
  if (usesLegacy(version)) {
    return (targetDepth < 0x80) ? (currentDepth * (100.0 / (32.0 * 256.0))) : (currentDepth * (100.0 / (8.0 * 256.0)));
  }
  return (currentDepth < 0x8000) ? (currentDepth * (100.0 / (128.0 * 256.0)))
                                 : ((currentDepth - (126.0 * 256.0)) * (50.0 / 256.0));
}

[[nodiscard]] constexpr u16 defaultMaxRateFactor(KonamiSnesVersion version) {
  return usesLegacy(version) ? static_cast<u16>(kDefaultLegacyVibratoMaxRateStep * 0xff)
                             : static_cast<u16>(0xff * lateEraRateStep(0xff));
}

[[nodiscard]] constexpr u16 minMaxRateFactor(KonamiSnesVersion version) {
  return usesLegacy(version) ? static_cast<u16>(kMinVibratoMaxRateStep * 0xff) : kMinVibratoMaxRateStep;
}

[[nodiscard]] inline double baseHz(KonamiSnesVersion version) {
  return usesLegacy(version) ? (kKonamiSnesTimerHz / 65536.0) : (kKonamiSnesTimerHz / 16384.0);
}

[[nodiscard]] inline u16 rateFactor(KonamiSnesVersion version, u8 rate, u8 tempo) {
  // Early vibrato speeds up and slows down with song tempo. Later vibrato uses
  // only its rate byte and the driver's range-dependent step size.
  if (usesLegacy(version)) {
    return static_cast<u16>(legacyRateStep(rate) * ((tempo == 0) ? 1 : tempo));
  }
  return (rate == 0) ? 0 : static_cast<u16>(rate * lateEraRateStep(rate));
}

[[nodiscard]] inline double delaySeconds(KonamiSnesVersion version, u8 delay, u8 tempo) {
  // Early delay counts music ticks and therefore changes with tempo. Later
  // delay counts the fixed 250 Hz timer directly.
  if (usesLegacy(version)) {
    return ((256.0 / kKonamiSnesTimerHz) * (delay + 1.0)) / ((tempo == 0) ? 1 : tempo);
  }
  return (delay + 1.0) / kKonamiSnesTimerHz;
}

[[nodiscard]] inline double minDelaySeconds(KonamiSnesVersion version) {
  return usesLegacy(version) ? delaySeconds(version, 0, 0xff) : delaySeconds(version, 0, 0);
}

[[nodiscard]] inline double maxDelaySeconds(KonamiSnesVersion version) {
  return usesLegacy(version) ? delaySeconds(version, 0xff, 1) : delaySeconds(version, 0xc7, 0);
}

[[nodiscard]] constexpr u8 delayFromArg1(KonamiSnesVersion version, u8 arg1) {
  // Later engines overload values 0xc8-0xff: they request a depth fade instead
  // of a start delay. In that form vibrato begins immediately.
  return (!usesLegacy(version) && arg1 >= kLateEraVibratoFadeThreshold) ? 0 : arg1;
}

[[nodiscard]] constexpr u8 inlineFadeLength(KonamiSnesVersion version, u8 arg1) {
  return (!usesLegacy(version) && arg1 >= kLateEraVibratoFadeThreshold)
             ? static_cast<u8>(arg1 - (kLateEraVibratoFadeThreshold - 1))
             : 0;
}

[[nodiscard]] inline KonamiVibratoSpec modulationSpec(KonamiSnesVersion version, u8 maxDepth = kDefaultVibratoMaxDepth,
                                                      u16 maxRateFactor = 0) {
  // Exporters need stable controller ranges before playback begins. Clamp very
  // small songs to a useful minimum so those ranges never collapse to zero.
  const u8 clampedMaxDepth = std::max(maxDepth, kMinVibratoMaxDepth);
  const u16 effectiveMaxRateFactor = maxRateFactor != 0 ? maxRateFactor : defaultMaxRateFactor(version);
  const u16 clampedMaxRateFactor = std::max(effectiveMaxRateFactor, minMaxRateFactor(version));
  const double minHertz = baseHz(version);
  return KonamiVibratoSpec{
      .maxDepthCents = maxDepthCents(version, clampedMaxDepth),
      .minHertz = minHertz,
      .maxHertz = minHertz * clampedMaxRateFactor,
      .minDelaySeconds = minDelaySeconds(version),
      .maxDelaySeconds = maxDelaySeconds(version),
  };
}

}  // namespace vibrato

struct KonamiSnesLayout {
  // Only addresses needed after discovery are kept here. A missing optional
  // table still permits sequence export, but prevents synth construction.
  KonamiSnesVersion version = KONAMISNES_NONE;
  u32 sequenceHeaderAddress = 0;
  std::optional<u32> spcDirAddress;
  std::optional<u32> commonInstrumentTableAddress;
  std::optional<u32> bankedInstrumentTableAddress;
  u8 firstBankedInstrument = 0;
  std::optional<u32> percussionInstrumentTableAddress;
};

struct KonamiSnesInstrumentInfo {
  u32 index = 0;
  u32 address = 0;
  u8 srcn = 0;
  s8 key = 0;
  s8 tuning = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  u8 pan = 0;
  u8 volume = 0;
  bool percussion = false;
  u8 percussionNote = 0;
};

[[nodiscard]] std::optional<KonamiSnesLayout> findKonamiSnesLayout(core::ByteReader reader);
[[nodiscard]] const char* konamiSnesVersionName(KonamiSnesVersion version);

[[nodiscard]] const core::SequenceDialect& konamiSnesSequenceDialect(KonamiSnesVersion version);
void registerKonamiSnesSequenceDialects(core::SequenceDialectRegistry& registry);
[[nodiscard]] core::TrackProgram decodeKonamiSnesSourceTrack(
    core::ByteReader reader, KonamiSnesVersion version, u32 sourceTrackNumber, u32 startAddress,
    core::SourceMapBuilder* sourceMap = nullptr, std::vector<core::Diagnostic>* diagnostics = nullptr,
    std::optional<core::SourceAnnotationId> parent = std::nullopt,
    std::optional<core::AssetId> sequenceAsset = std::nullopt);
[[nodiscard]] core::SourceRange konamiSnesSequenceHeaderRange(core::ByteReader reader, const KonamiSnesLayout& layout);
[[nodiscard]] core::SequenceProgram decodeKonamiSnesSequence(core::ByteReader reader, const KonamiSnesLayout& layout,
                                                             core::AssetId sequenceId,
                                                             core::SourceMapBuilder* sourceMap = nullptr,
                                                             std::vector<core::Diagnostic>* diagnostics = nullptr);

[[nodiscard]] std::vector<KonamiSnesInstrumentInfo> parseKonamiSnesInstrumentInfos(core::ByteReader reader,
                                                                                   const KonamiSnesLayout& layout);
[[nodiscard]] core::SnesBrrCatalog parseKonamiSnesSampleInfos(core::ByteReader reader, u32 spcDirAddress,
                                                              const std::vector<KonamiSnesInstrumentInfo>& instruments);
[[nodiscard]] bool addKonamiSnesSynth(core::ScanResultBuilder& builder, core::ScanInstrumentSetRef instrumentSet,
                                      core::ScanSampleCollectionRef sampleCollection, const KonamiSnesLayout& layout,
                                      std::string_view displayName);

void registerKonamiSnesModule(core::FormatRegistry& registry);

}  // namespace vgmtrans::formats::konami_snes
