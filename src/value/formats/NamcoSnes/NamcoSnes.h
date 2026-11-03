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
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::namco_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr u32 kCommandLimit = 65536;
inline constexpr std::string_view kInstrumentDomain = "namco-snes.instrument";
inline constexpr u32 kNoiseInstrumentKey = 0x100;
inline constexpr u8 kNoiseOutputKeyBase = 35;
inline constexpr u8 kRest = 0x54;

enum class Version : u8 {
  WagyanParadise,
  YuuYuuHakushoTokubetsuHen,
  BlueCrystalRod,
};

struct Layout {
  Version version;
  u16 sequenceAddress;
  u16 sequenceReferenceAddress;
  u8 sequenceReferenceSize;
  u16 dataPointerBlockAddress;
  u16 tuningTableAddress;
  u16 spcDirAddress;
  bool mono;

  [[nodiscard]] u16 amplitudeEnvelopePointerTable(core::ByteReader reader) const {
    return reader.le16(dataPointerBlockAddress);
  }
  [[nodiscard]] u16 pitchEnvelopePointerTable(core::ByteReader reader) const {
    return reader.le16(dataPointerBlockAddress + 2);
  }
  [[nodiscard]] u16 percussionTable(core::ByteReader reader) const {
    return reader.le16(dataPointerBlockAddress + 4);
  }
  [[nodiscard]] u16 echoFilterTable(core::ByteReader reader) const {
    return reader.le16(dataPointerBlockAddress + 6);
  }
};

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> srcns;
  std::set<u8> percussion;
  std::set<u8> noiseRates;
};

[[nodiscard]] const char* versionName(Version version);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] SequenceParse decodeSequence(core::RetainedSource source, const Layout& layout,
                                           core::AssetId sequenceId, core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::Envelope driverAmplitudeEnvelope(core::ByteReader reader, const Layout& layout, u8 index);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const std::set<u8>& srcns,
                                                               const std::set<u8>& percussion,
                                                               const std::set<u8>& noiseRates,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::namco_snes
