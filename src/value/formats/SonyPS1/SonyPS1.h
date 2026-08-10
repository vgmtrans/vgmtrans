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
#include "value/synth/PsxAdpcm.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

inline constexpr std::string_view kSonyPs1FormatName = "SonyPS1";
inline constexpr std::string_view kSonyPs1CollectionResolver = "sony-ps1";
inline constexpr std::string_view kSonyPs1InstrumentDomain = "sony-ps1.instrument";
inline constexpr std::string_view kSonyPs1DialectId = "sony-ps1:sequence";

[[nodiscard]] inline core::InstrumentIdentity sonyPs1InstrumentIdentity(u16 bank, u8 program) {
  return core::InstrumentIdentity{
      .domain = std::string(kSonyPs1InstrumentDomain),
      .key = (static_cast<u32>(bank) << 8) | program,
  };
}

struct SonyPs1EventLayout {
  u32 offset = 0;
  u32 end = 0;
  u32 delta = 0;
  u8 deltaSize = 0;
  u8 status = 0;
  bool explicitStatus = false;
  u8 data1 = 0;
  u8 data2 = 0;
  u8 dataBytes = 0;
  bool implicitEnd = false;
  std::optional<u32> loopDestination;
  u8 loopCount = 0;
};

struct SonyPs1SequenceLayout {
  u32 offset = 0;
  u32 length = 0;
  u32 dataOffset = 0;
  u32 dataEnd = 0;
  u16 ppqn = 0;
  u32 initialTempo = 0;
  u8 rhythmNumerator = 0;
  u8 rhythmDenominatorPower = 0;
  u16 sequenceId = 0;
  bool sep = false;
  bool sepFirst = false;
  std::vector<SonyPs1EventLayout> events;
};

struct SonyPs1BankLayout {
  u32 offset = 0;
  u32 length = 0;
  u32 headerSize = 0;
  u32 sampleDataOffset = 0;
  u32 expectedSampleBytes = 0;
  u32 declaredFileSize = 0;
  u32 version = 0;
  u32 id = 0;
  u16 programCount = 0;
  u16 toneCount = 0;
  u16 sampleCount = 0;
  u16 programSlots = 0;
  u8 sampleSizeShift = 0;
  u8 masterVolume = 0;
  u8 masterPan = 0;
  bool hasSampleBody = false;
  std::vector<u32> sampleSizes;
};

struct SonyPs1SampleLayout {
  u32 offset = 0;
  u32 storageLength = 0;
  core::PsxAdpcmStream stream;
};

struct SonyPs1SampleBodyLayout {
  u32 offset = 0;
  u32 length = 0;
  std::vector<SonyPs1SampleLayout> samples;
};

struct SonyPs1ScannedBank {
  core::ScanInstrumentSetRef instruments;
  std::optional<core::ScanSampleCollectionRef> samples;
};

[[nodiscard]] std::optional<SonyPs1SequenceLayout> readSonyPs1SequenceLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<SonyPs1SequenceLayout> findSonyPs1Sequences(core::ByteReader reader);
[[nodiscard]] std::optional<SonyPs1BankLayout> readSonyPs1BankLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<SonyPs1BankLayout> findSonyPs1Banks(core::ByteReader reader);
[[nodiscard]] std::vector<SonyPs1SampleBodyLayout> findSonyPs1SampleBodies(core::ByteReader reader);
[[nodiscard]] std::optional<u32> matchSonyPs1SampleBody(core::ByteReader reader, u32 preferredOffset,
                                                        const std::vector<u32>& sampleSizes, bool forceSingle = false);

[[nodiscard]] SonyPs1ScannedBank addSonyPs1Bank(core::ScanResultBuilder& result, const SonyPs1BankLayout& layout,
                                                u16 bank);
[[nodiscard]] std::optional<core::ScanSampleCollectionRef> addSonyPs1RawSampleBody(core::ScanResultBuilder& result);
[[nodiscard]] core::SequenceProgram parseSonyPs1Sequence(core::ByteReader reader, core::AssetId id,
                                                         const SonyPs1SequenceLayout& layout,
                                                         core::SourceMapBuilder* sourceMap = nullptr,
                                                         std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceDialect& sonyPs1SequenceDialect();
[[nodiscard]] std::vector<core::DesiredCollection> resolveSonyPs1Collections(const core::MatchContext& context);
[[nodiscard]] core::FormatDefinition sonyPs1Definition();

}  // namespace vgmtrans::formats::sony_ps1
