/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatDefinition.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceDialect.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::suzuki_ps1 {

inline constexpr std::string_view kSuzukiPs1FormatName = "SuzukiPS1";
inline constexpr std::string_view kSuzukiPs1InstrumentDomain = "suzuki-ps1.instrument";
inline constexpr std::string_view kSuzukiPs1DialectId = "suzuki-ps1:sequence";

[[nodiscard]] inline core::InstrumentIdentity suzukiPs1InstrumentIdentity(u16 bank, u8 program) {
  return core::InstrumentIdentity{
      .domain = std::string(kSuzukiPs1InstrumentDomain),
      .key = (static_cast<u32>(bank) << 8) | program,
  };
}

enum class SuzukiPs1BankKind : u8 {
  Dwds,
  Wds,
};

struct SuzukiPs1SequenceLayout {
  u32 offset = 0;
  u32 length = 0;
  u8 trackCount = 0;
  u8 percussionCount = 0;
  u16 defaultBank = 0;
  u16 titleOffset = 0;
  u16 percussionOffset = 0;
  std::string title;
  std::vector<u32> trackAddresses;
};

struct SuzukiPs1BankLayout {
  u32 offset = 0;
  u32 length = 0;
  u32 headerSize = 0;
  u32 sampleSize = 0;
  u16 bank = 0;
  u16 highestProgram = 0;
  SuzukiPs1BankKind kind = SuzukiPs1BankKind::Dwds;
};

// Native register state is retained so sequence ADSR commands can modify the
// exact selected instrument state during playback.
struct SuzukiPs1EnvelopeRegisters {
  u16 bank = 0;
  u8 program = 0;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
};

struct SuzukiPs1ScannedBank {
  core::ScanInstrumentSetRef instruments;
  core::ScanSampleCollectionRef samples;
  std::vector<SuzukiPs1EnvelopeRegisters> envelopes;
};

[[nodiscard]] std::optional<SuzukiPs1SequenceLayout> readSuzukiPs1SequenceLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<SuzukiPs1SequenceLayout> findSuzukiPs1Sequences(core::ByteReader reader);
[[nodiscard]] std::optional<SuzukiPs1BankLayout> readSuzukiPs1BankLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<SuzukiPs1BankLayout> findSuzukiPs1Banks(core::ByteReader reader);

[[nodiscard]] std::optional<SuzukiPs1ScannedBank> addSuzukiPs1Bank(core::ScanResultBuilder& result,
                                                                   const SuzukiPs1BankLayout& layout);
[[nodiscard]] core::SequenceProgram parseSuzukiPs1Sequence(
    core::ByteReader reader, core::AssetId id, const SuzukiPs1SequenceLayout& layout,
    const std::vector<SuzukiPs1EnvelopeRegisters>& envelopes = {}, core::SourceMapBuilder* sourceMap = nullptr,
    std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& suzukiPs1SequenceDialect();
[[nodiscard]] core::FormatDefinition suzukiPs1Definition();

}  // namespace vgmtrans::formats::suzuki_ps1
