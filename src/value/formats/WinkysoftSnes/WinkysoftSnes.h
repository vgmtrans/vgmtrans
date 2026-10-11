/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <array>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::winkysoft_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 10;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "winkysoft-snes.instrument";

[[nodiscard]] constexpr u32 noiseInstrumentKey(u8 instrument, u8 rate) {
  return 256u + instrument * 32u + rate;
}

enum class Version { Early, Jojo, Standard, Extended };

struct Layout {
  Version version = Version::Standard;
  u16 commandMap = 0;
  u16 commandTable = 0;
  u16 instrumentTable = 0;
  u16 directory = 0;
  u16 pitchTable = 0;
  u16 panTableLeft = 0;
  u16 panTableRight = 0;
  u16 panSequenceTable = 0;
  u16 songHeader = 0;
  u8 songHeaderSize = 2;
  u16 songStart = 0;
  u8 firstTrack = 0;
  u8 initialTempo = 125;
  u8 initialVolume = 255;
  u8 hostCondition = 0;
  u32 frameMicroseconds = 10000;
  bool stereo = true;
  bool resetTempoAtLoop = true;
  std::array<u8, 128> initialDsp{};
};

struct Instrument {
  core::SourceRange range;
  u8 sample = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u16 pitch = 0;
  u8 volume = 0;
  u8 pan = 0x40;
  u8 transpose = 0;
};

struct DriverData {
  std::array<Instrument, 256> instruments;
  std::array<u16, 13> pitches{};
  std::array<u8, 16> panLeft{};
  std::array<u8, 16> panRight{};
};

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> instruments;
  std::set<u8> noiseRates;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] DriverData readDriverData(core::ByteReader reader, const Layout& layout);
[[nodiscard]] double unityKey(const Instrument& instrument, const DriverData& data);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, const DriverData& data,
                                           core::AssetId id, core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                              const DriverData& data, const std::set<u8>& programs,
                                                              const std::set<u8>& noiseRates,
                                                              std::string_view name);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::winkysoft_snes
