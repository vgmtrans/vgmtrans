/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/model/EnvelopeModel.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::hudson_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr u32 kDrumKitKey = 0x7f << 7;
inline constexpr u8 kDrumKeyBias = 60;
inline constexpr std::string_view kInstrumentDomain = "hudson-snes.instrument";

// Hudson's public driver revisions fall into three sequence-compatible
// families. Minor builds within a family share the command layout.
enum class Version : u8 {
  Early,
  V1,
  V2,
};

struct Layout {
  Version version = Version::Early;
  u16 sequenceHeaderAddress = 0;
  u16 noteLengthTableAddress = 0;
  u16 spcDirAddress = 0;
  u16 tuningTableAddress = 0;
  u16 activeInstrumentTableAddress = 0;
  u16 activeDrumTableAddress = 0;
  u16 activePitchTableAddress = 0;
  u16 activeWaveformTableAddress = 0;
  u16 activeVolumeTableAddress = 0;
};

struct InstrumentRow {
  u8 program = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  u16 pitchScale = 0x0100;
  s8 coarseTuning = 0;
  s8 fineTuning = 0;
  core::SourceRange source;
};

struct DrumSlot {
  u8 note = 0;
  u8 sourceProgram = 0;
  u8 sourceKey = 0;
  u8 volume = 0;
  u8 pan = 15;
  core::SourceRange source;
};

struct CustomWaveform {
  u8 index = 0;
  std::vector<s8> samples;
  core::SourceRange source;
};

struct PitchScriptStep {
  u8 duration = 0;
  s8 target = 0;
};

struct PitchScript {
  u8 index = 0;
  std::vector<PitchScriptStep> steps;
  core::SourceRange source;
};

struct VolumeCurve {
  u8 index = 0;
  std::vector<s8> offsets;
  core::SourceRange source;
};

struct SequenceRecipes {
  std::vector<InstrumentRow> instruments;
  std::vector<DrumSlot> drums;
  std::vector<CustomWaveform> customWaveforms;
  std::vector<PitchScript> pitchScripts;
  std::vector<VolumeCurve> volumeCurves;
};

struct SequenceReferences {
  std::set<u8> programs{0};
  std::set<u8> pitchScripts;
  std::set<u8> customWaveforms;
  std::set<u8> volumeCurves;
};

struct ParsedHeader {
  u8 timebaseShift = 2;
  bool noteVelocity = false;
  s8 initialEchoLeft = 0;
  s8 initialEchoRight = 0;
  u8 initialEchoDelay = 0;
  s8 initialEchoFeedback = 0;
  u8 initialEchoFilter = 0;
  u8 initialEchoMask = 0;
  std::vector<std::pair<u8, u16>> tracks;
  SequenceRecipes recipes;
  core::SourceRange range;
};

struct SequenceParse {
  core::SequenceProgram program;
  SequenceRecipes recipes;
  core::SourceRange headerRange;
};

[[nodiscard]] const char* versionName(Version version);
[[nodiscard]] core::Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain);
[[nodiscard]] double driverPseudoReleaseSeconds(u8 gain);
[[nodiscard]] std::optional<ParsedHeader> parseHeader(core::ByteReader reader, Version version, u32 address);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
void supplementLiveRecipes(core::ByteReader reader, const Layout& layout, SequenceReferences references,
                           SequenceRecipes& recipes);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, Version version, u8 timebaseShift,
                                                   bool noteVelocity, u32 trackNumber, u32 startAddress,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] core::SequenceRuntime sequenceRuntime(Version version, ParsedHeader header);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const SequenceRecipes& recipes,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::hudson_snes
