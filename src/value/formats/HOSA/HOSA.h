/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/InstrumentIdentity.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::hosa {

inline constexpr std::string_view kHosaFormatName = "HOSA";
inline constexpr std::string_view kHosaInstrumentDomain = "hosa.instrument";
inline constexpr std::string_view kHosaCommandKindPrefix = "hosa:sequence";

[[nodiscard]] inline core::InstrumentIdentity instrumentIdentity(u8 program) {
  return core::InstrumentIdentity{.domain = std::string(kHosaInstrumentDomain), .key = program};
}

struct TrackLayout {
  u32 offset = 0;
  u32 end = 0;
};

struct ReverbConfig {
  u8 mode = 0;
  s16 depth = 0;
  u8 delay = 0;
  u8 feedback = 0;
};

struct SequenceLayout {
  u32 offset = 0;
  u32 length = 0;
  u8 version = 0;
  ReverbConfig reverb;
  s16 leftGain = 0;
  s16 rightGain = 0;
  std::array<u16, 32> durations{};
  std::vector<TrackLayout> tracks;
};

struct Region {
  u32 offset = 0;
  u32 sampleOffset = 0;
  u8 volume = 0;
  u8 keyLow = 0;
  u8 keyHigh = 127;
  double unityKey = 60.0;
  std::optional<u8> panOverride;
  bool reverb = false;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  core::SourceRecord source;
};

struct Instrument {
  std::vector<Region> regions;
  core::SourceRecord source;
};

struct BankLayout {
  u32 offset = 0;
  u32 length = 0;
  std::optional<u32> sampleDataOffset;
  std::vector<std::optional<u32>> instrumentAddresses;
};

struct ScannedBank {
  core::ScanSoundBankDraft bank;
  std::vector<Instrument> instruments;
};

[[nodiscard]] std::optional<SequenceLayout> readSequenceLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<SequenceLayout> findSequences(core::ByteReader reader);
[[nodiscard]] std::optional<BankLayout> findBank(core::ByteReader reader, const SequenceLayout& sequence);
[[nodiscard]] std::optional<ScannedBank> addBank(core::ScanResultBuilder& result, const BankLayout& layout);
[[nodiscard]] core::SequenceProgram parseSequence(core::ByteReader reader, core::AssetId id,
                                                  const SequenceLayout& layout,
                                                  const std::vector<Instrument>& instruments = {},
                                                  core::SourceMapBuilder* sourceMap = nullptr,
                                                  std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::hosa
