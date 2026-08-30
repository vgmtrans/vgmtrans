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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::ohori_aka_ps1 {

inline constexpr std::string_view kOhoriAkaPs1FormatName = "OhoriAkaPS1";
inline constexpr std::string_view kOhoriAkaPs1InstrumentDomain = "ohori-aka-ps1.instrument";
inline constexpr std::string_view kOhoriAkaPs1CommandKindPrefix = "ohori-aka-ps1:sequence";

[[nodiscard]] inline core::InstrumentIdentity ohoriAkaPs1InstrumentIdentity(u8 program) {
  return core::InstrumentIdentity{.domain = std::string(kOhoriAkaPs1InstrumentDomain), .key = program};
}

struct OhoriAkaPs1SequenceLayout {
  u32 offset = 0;
  u32 length = 0;
  u8 version = 0;
  u8 trackCount = 0;
  u8 reverbMode = 0;
  u16 reverbDepth = 0;
  u8 reverbDelay = 0;
  u8 reverbFeedback = 0;
  u16 leftGain = 0;
  u16 rightGain = 0;
  std::vector<u16> durations;
  std::vector<u32> trackAddresses;
  std::vector<u32> trackEnds;
};

struct OhoriAkaPs1Region {
  u32 offset = 0;
  u32 sampleOffset = 0;
  u8 volume = 0;
  u8 keyLow = 0;
  u8 keyHigh = 0;
  double unityKey = 60.0;
  std::optional<u8> panOverride;
  bool reverb = false;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  core::SourceRecord source;
};

struct OhoriAkaPs1Instrument {
  u8 program = 0;
  std::vector<OhoriAkaPs1Region> regions;
  core::SourceRecord source;
};

struct OhoriAkaPs1BankLayout {
  u32 offset = 0;
  u32 length = 0;
  u32 containerOffset = 0;
  u32 sampleDataOffset = 0;
  u32 sampleDataLength = 0;
  u32 instrumentCount = 0;
  std::vector<u32> instrumentAddresses;
};

struct OhoriAkaPs1ScannedBank {
  core::ScanSoundBankDraft bank;
  std::vector<OhoriAkaPs1Instrument> instruments;
};

[[nodiscard]] std::optional<OhoriAkaPs1SequenceLayout> readOhoriAkaPs1SequenceLayout(core::ByteReader reader,
                                                                                    u32 offset);
[[nodiscard]] std::vector<OhoriAkaPs1SequenceLayout> findOhoriAkaPs1Sequences(core::ByteReader reader);
[[nodiscard]] std::optional<OhoriAkaPs1BankLayout> findOhoriAkaPs1Bank(core::ByteReader reader,
                                                                      const OhoriAkaPs1SequenceLayout& sequence);
[[nodiscard]] std::optional<OhoriAkaPs1ScannedBank> addOhoriAkaPs1Bank(core::ScanResultBuilder& result,
                                                                      const OhoriAkaPs1BankLayout& layout);
[[nodiscard]] core::SequenceProgram parseOhoriAkaPs1Sequence(
    core::ByteReader reader, core::AssetId id, const OhoriAkaPs1SequenceLayout& layout,
    const std::vector<OhoriAkaPs1Instrument>& instruments = {}, core::SourceMapBuilder* sourceMap = nullptr,
    std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& ohoriAkaPs1SequenceConfig();
[[nodiscard]] core::FormatModule ohoriAkaPs1Module();

}  // namespace vgmtrans::formats::ohori_aka_ps1
