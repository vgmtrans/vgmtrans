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
#include <span>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::segsat {

inline constexpr std::string_view kSegSatFormatName = "SegSat";
inline constexpr std::string_view kSegSatCollectionResolver = "SegSat";
inline constexpr std::string_view kSegSatInstrumentDomain = "segsat";

[[nodiscard]] inline core::InstrumentIdentity segSatInstrumentIdentity(u8 sourceBank, u8 program) {
  return core::InstrumentIdentity{
      .domain = std::string(kSegSatInstrumentDomain),
      .key = (static_cast<u32>(sourceBank) << 8) | program,
  };
}

struct SegSatInstrumentAddress {
  u8 sourceBank = 0;
  u8 program = 0;
};

[[nodiscard]] inline std::optional<SegSatInstrumentAddress> decodeSegSatInstrumentIdentity(
    const core::InstrumentIdentity& identity) {
  if (identity.domain != kSegSatInstrumentDomain || identity.key > 0xffff) {
    return std::nullopt;
  }
  return SegSatInstrumentAddress{
      .sourceBank = static_cast<u8>(identity.key >> 8),
      .program = static_cast<u8>(identity.key),
  };
}

enum class SegSatDriverVersion : u8 {
  V1_28,
  V2_08,
  V2_20,
};

// The IRQ signatures used for LFO timing do not identify the driver's volume
// arithmetic reliably. Mega Man 8, for example, matches the V2_08 IRQ pattern
// but identifies its sound driver as version 1.33.
enum class SegSatVolumeModel : u8 {
  V1_28,
  V1_33,
  V2_20,
  V3_1,
};

struct SegSatVlTable {
  u8 rate0 = 0;
  u8 point0 = 0;
  u8 level0 = 0;
  u8 rate1 = 0;
  u8 point1 = 0;
  u8 level1 = 0;
  u8 rate2 = 0;
  u8 point2 = 0;
  u8 level2 = 0;
  u8 rate3 = 0;
};

// Voices store (region count - 1) in one byte. Mega Man 8 rejects the voice
// when that byte is negative; 0xff is the commonly observed empty-program
// sentinel (handle_note_event around mm8audio.bin 0x371e).
[[nodiscard]] constexpr u8 segSatRegionCount(u8 encoded) {
  return (encoded & 0x80) != 0 ? 0 : static_cast<u8>(encoded + 1);
}

struct SegSatBankLayout {
  u32 offset = 0;
  u32 instrumentDataEnd = 0;
  u16 velocityTables = 0;
  u16 pegTables = 0;
  u16 plfoTables = 0;
  u16 firstInstrument = 0;
  u16 instrumentCount = 0;
  std::optional<u8> sourceBank;
};

struct SegSatSequenceLayout {
  u32 offset = 0;
  u32 end = 0;
  u32 tableIndex = 0;
  u32 sequenceIndex = 0;
  u16 ppqn = 0;
  u16 tempoEventCount = 0;
  u16 normalTrack = 0;
  u32 normalTrackEnd = 0;
  u16 tempoLoop = 0;
  std::vector<u8> referencedBanks;
};

struct SegSatScannedBank {
  core::ScanInstrumentSetRef instruments;
  core::ScanSampleCollectionRef samples;
};

struct SegSatVelocityRegion {
  u8 keyLow = 0;
  u8 keyHigh = 127;
  u8 table = 0;
  u8 totalLevel = 0;
  double referenceGain = 1.0;
};

struct SegSatVelocityInstrument {
  s8 volumeBias = 0;
  std::vector<SegSatVelocityRegion> regions;
};

struct SegSatVelocityBank {
  u8 sourceBank = 0;
  std::vector<SegSatVlTable> tables;
  std::vector<SegSatVelocityInstrument> instruments;
};

struct SegSatControllerChange {
  u32 command = 0;
  u8 controller = 0;
  u8 value = 0;
};

// Mega Man 8's driver converts a VL-table result into the MIDI velocity used
// by the legacy exporter. This is public so the velocity math can be tested on
// its own.
[[nodiscard]] u8 segSatMidiVelocity(u8 velocity, const SegSatVlTable& table, u8 totalLevel, s8 volumeBias);
[[nodiscard]] double segSatLinearGain(SegSatVolumeModel model, u8 velocity, const SegSatVlTable& table, u8 totalLevel,
                                      s8 volumeBias, u8 volume, u8 expression);
[[nodiscard]] double segSatRegionReferenceGain(SegSatVolumeModel model, const SegSatVlTable& table, u8 totalLevel,
                                               s8 volumeBias);

[[nodiscard]] std::optional<SegSatBankLayout> readSegSatBankLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<SegSatBankLayout> findSegSatBanks(core::ByteReader reader);
[[nodiscard]] std::vector<SegSatSequenceLayout> findSegSatSequences(core::ByteReader reader);
[[nodiscard]] SegSatDriverVersion determineSegSatDriverVersion(core::ByteReader reader);
[[nodiscard]] SegSatVolumeModel determineSegSatVolumeModel(core::ByteReader reader);
[[nodiscard]] std::optional<SegSatScannedBank> addSegSatBank(core::ScanResultBuilder& builder,
                                                             const SegSatBankLayout& layout,
                                                             SegSatDriverVersion version, SegSatVolumeModel volumeModel,
                                                             u8 exportBank);
[[nodiscard]] SegSatVelocityBank readSegSatVelocityBank(core::ByteReader reader, const SegSatBankLayout& layout,
                                                        u8 sourceBank, SegSatVolumeModel volumeModel);
[[nodiscard]] std::vector<u8> segSatSequenceBanks(const core::SequenceProgram& program);
[[nodiscard]] std::vector<SegSatControllerChange> segSatControllerChanges(const core::SequenceProgram& program);
void finalizeSegSatPerformance(core::PerformanceSequence& performance, std::span<const SegSatVelocityBank> banks,
                               SegSatVolumeModel model, std::span<const SegSatControllerChange> controllerChanges);

[[nodiscard]] core::SequenceProgram parseSegSatSequenceProgram(core::ByteReader reader, core::AssetId id,
                                                               const SegSatSequenceLayout& layout,
                                                               core::SourceMapBuilder* sourceMap = nullptr,
                                                               std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& segSatSequenceConfig();
[[nodiscard]] core::FormatModule segSatModule();

}  // namespace vgmtrans::formats::segsat
