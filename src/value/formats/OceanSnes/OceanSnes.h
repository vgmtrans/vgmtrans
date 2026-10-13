/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"

#include <array>
#include <bitset>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::ocean_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr double kTickSeconds = 0.02025;
inline constexpr std::string_view kInstrumentDomain = "ocean-snes.instrument";

enum class Version { Waterworld, JurassicPark2 };

struct Layout {
  Version version = Version::Waterworld;
  u16 header = 0;
  u16 orders = 0;
  u16 instruments = 0;
  u16 samples = 0;
  u16 pitchTable = 0;
  u16 sineTable = 0;
  u16 randomSeed = 0;
  u16 bendPattern = 0;
  std::vector<u16> orderBlocks;
};

struct InstrumentRegion {
  core::SourceRange range;
  u8 low = 0, high = 0, sample = 0;
  u8 volume = 0, transpose = 0;
  u16 pitchScale = 0;  // Combined sample and region tuning, in 8.8 fixed point.
  u8 pan = 0;
  u16 vibratoDelay = 0;
  u8 vibratoDepth = 0, vibratoRate = 0, bendScale = 0, lfo = 0;
  u8 pitchModulation = 0, surround = 0;
  double unityKey = 60.0;
  core::Envelope envelope;
  std::optional<u64> gateTicks;  // Base ticks until automatic release; empty means indefinite.
};

struct InstrumentData {
  core::SourceRange range;
  u8 volume = 0, transpose = 0;
  std::vector<InstrumentRegion> regions;
};

struct SampleData {
  core::SourceRange header;
  core::SourceRange brr;
  u16 tuning = 0;
  core::Loop loop;
};

struct DriverData {
  std::array<std::optional<InstrumentData>, 128> instruments;
  std::bitset<128> retainedInstruments;
  std::array<std::optional<SampleData>, 128> samples;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] DriverData readDriverData(core::ByteReader reader, const Layout& layout,
                                        std::vector<core::Diagnostic>& diagnostics);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const DriverData& data, std::string_view name);
[[nodiscard]] core::SequenceProgram decodeSequence(core::RetainedSource source, const Layout& layout,
                                                   std::shared_ptr<const DriverData> data, core::AssetId id,
                                                   core::SourceMapBuilder* sourceMap = nullptr,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::ocean_snes
