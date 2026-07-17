/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/CapcomSnes/CapcomSnesLayout.h"
#include "value/sequence/SequenceDialect.h"
#include "value/base/Source.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/scan/ScanTypes.h"
#include "value/formats/CapcomSnes/CapcomSnesTypes.h"

#include <optional>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

enum class CapcomSnesCommandKind : u32 {
  Note = 1,
  Rest,
  ToggleTriplet,
  ToggleSlur,
  DottedNote,
  ToggleOctaveUp,
  NoteAttributes,
  Tempo,
  DurationRate,
  Volume,
  Program,
  Octave,
  GlobalTranspose,
  Transpose,
  Tuning,
  PortamentoTime,
  RepeatUntil,
  RepeatBreak,
  Jump,
  End,
  Pan,
  MasterVolume,
  Lfo,
  EchoParam,
  EchoOnOff,
  ReleaseRate,
  UnknownOneByte,
  NoOperation,
  Unsupported,
};

enum class CapcomSnesOperand : u32 {
  KeyIndex = 1,
  DurationIndex,
  Raw,
  Attributes,
  Rate,
  Bank,
  Program,
  Octave,
  Semitones,
  Tuning,
  Time,
  Slot,
  Count,
  Destination,
  Type,
  Value,
  Argument,
  Preset,
};

struct CapcomSnesSequenceDescriptor {
  core::SequenceDialect dialect;
  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::none;
  u32 profile = 0;
};

[[nodiscard]] const CapcomSnesSequenceDescriptor& capcomSnesSequenceDescriptor(CapcomSnesEngineVersion version);
void registerCapcomSnesSequenceDialects(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeCapcomSnesSourceTrack(
    core::ByteReader reader, const CapcomSnesSequenceDescriptor& descriptor, u32 sourceTrackNumber, u32 startAddress,
    core::SourceMapBuilder* sourceMap = nullptr, std::vector<core::Diagnostic>* diagnostics = nullptr,
    std::optional<core::SourceAnnotationId> parent = std::nullopt,
    std::optional<core::AssetId> sequenceAsset = std::nullopt);

[[nodiscard]] core::SequenceProgramAsset parseCapcomSnesSequence(const core::ScanInput& input,
                                                                 const CapcomSnesLayout& layout,
                                                                 core::AssetId sequenceId, std::string_view displayName,
                                                                 core::SourceMapBuilder* sourceMap = nullptr,
                                                                 std::vector<core::Diagnostic>* diagnostics = nullptr);

}  // namespace vgmtrans::formats::capcom_snes
