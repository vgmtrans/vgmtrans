/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/LevelScale.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgramConfig.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::sony_ps2 {

inline constexpr std::string_view kFormatName = "SonyPS2";
inline constexpr std::string_view kCollectionResolver = "sony-ps2";
inline constexpr std::string_view kInstrumentDomain = "sony-ps2.instrument";
inline constexpr std::string_view kSetbInstrumentDomain = "sony-ps2.setb-instrument";
inline constexpr std::string_view kCommandKindPrefix = "sony-ps2:sequence";

// modhsyn multiplies the 1..127 note velocity directly into SPU2 voice gain.
// MIDI targets conventionally square velocity, so sequence notes and synth
// velocity zones must share this conversion to stay in the same domain.
[[nodiscard]] constexpr double velocityGain(u8 velocity) noexcept {
  return static_cast<double>(std::min<u8>(velocity, 127)) / 128.0;
}

[[nodiscard]] inline u8 midiVelocity(u8 velocity) {
  return core::LevelScale::midi7FromLinear(velocityGain(velocity));
}

[[nodiscard]] inline u8 rawVelocityFromMidi(u8 velocity) {
  if (velocity == 0) {
    return 0;
  }
  return static_cast<u8>(
      std::clamp<int>(static_cast<int>(std::lround(core::LevelScale::linearFromMidi7(velocity) * 128.0)), 1, 127));
}

[[nodiscard]] inline core::InstrumentIdentity instrumentIdentity(u16 bank, u8 program) {
  return core::InstrumentIdentity{
      .domain = std::string(kInstrumentDomain),
      .key = (static_cast<u32>(bank) << 8) | program,
  };
}

[[nodiscard]] inline core::InstrumentIdentity setbInstrumentIdentity(u8 set, u8 timbre) {
  return core::InstrumentIdentity{
      .domain = std::string(kSetbInstrumentDomain),
      .key = (static_cast<u32>(set) << 8) | timbre,
  };
}

struct SparseChunkLayout {
  u32 offset = 0;
  u32 size = 0;
  std::vector<std::optional<u32>> entries;
};

struct MidiBlockLayout {
  u32 index = 0;
  u32 offset = 0;
  u32 dataOffset = 0;
  u32 dataEnd = 0;
  u16 ppqn = 480;
  u16 compression = 0;
  std::vector<u8> noteDictionary;
};

struct SeSequenceLayout {
  u32 offset = 0;
  u32 dataOffset = 0;
  u32 dataEnd = 0;
  u16 ppqn = 480;
  u8 set = 0;
  u8 sequence = 0;
  u8 volume = 127;
  s8 pan = 0;
  u16 timeScale = 0x100;
};

struct SequenceLayout {
  u32 offset = 0;
  u32 length = 0;
  u8 majorVersion = 0;
  u8 minorVersion = 0;
  std::optional<SparseChunkLayout> midi;
  std::optional<SparseChunkLayout> songs;
  std::optional<SparseChunkLayout> seSequences;
  std::optional<SparseChunkLayout> seSongs;
  std::vector<MidiBlockLayout> midiBlocks;
  std::vector<SeSequenceLayout> seSequenceBlocks;
};

struct VagInfo {
  u32 bodyOffset = 0;
  u16 sampleRate = 48000;
  bool loops = false;
};

struct SampleBodyData {
  u32 bytes = 0;
  core::RetainedSource source;
  struct Entry {
    u32 bodyOffset = 0;
    u32 sampleIndex = 0;
  };
  std::vector<Entry> entries;
};

struct PitchBendZone {
  u8 keyLow = 0;
  u8 keyHigh = 127;
  u16 negative = 256;
  u16 positive = 256;
};

struct ProgramRuntimeInfo {
  u8 bank = 0;
  u8 program = 0;
  s16 pitchDepthPositive = 0;
  s16 pitchDepthNegative = 0;
  s16 midiPitchDepthPositive = 0;
  s16 midiPitchDepthNegative = 0;
  u16 pitchBendPositive = 256;
  u16 pitchBendNegative = 256;
  std::vector<PitchBendZone> pitchBendZones;
  s8 ampDepthPositive = 0;
  s8 ampDepthNegative = 0;
  s8 midiAmpDepthPositive = 0;
  s8 midiAmpDepthNegative = 0;
};

struct SoundBankData {
  u32 expectedBodyBytes = 0;
  std::vector<std::optional<VagInfo>> vags;
  std::vector<ProgramRuntimeInfo> runtimePrograms;
};

struct SequenceData {};

struct RuntimeConfig {
  std::vector<ProgramRuntimeInfo> programs;
};

[[nodiscard]] std::optional<SequenceLayout> readSequenceLayout(core::ByteReader reader, u32 offset);
[[nodiscard]] std::vector<SequenceLayout> findSequenceLayouts(core::ByteReader reader);
[[nodiscard]] std::optional<SoundBankData> readSoundBankLayout(core::ByteReader reader, u32 offset);

void addSoundBank(core::ScanResultBuilder& result, u32 offset, SoundBankData layout);
[[nodiscard]] bool addSampleBody(core::ScanResultBuilder& result);
[[nodiscard]] core::SequenceProgram parseMidiSequence(core::ByteReader reader, core::AssetId id,
                                                      const MidiBlockLayout& layout,
                                                      core::SourceMapBuilder* sourceMap = nullptr,
                                                      std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] std::optional<core::SequenceProgram> parseSongSequence(
    core::ByteReader reader, core::AssetId id, const SequenceLayout& layout, u32 songOffset, u32 songEnd,
    core::SourceMapBuilder* sourceMap = nullptr, std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] std::optional<core::SequenceProgram> parseSeSequence(
    core::ByteReader reader, core::AssetId id, const SeSequenceLayout& layout,
    core::SourceMapBuilder* sourceMap = nullptr, std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] core::SequenceRuntime sequenceRuntime(RuntimeConfig config = {});
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();

[[nodiscard]] std::vector<core::DesiredCollection> resolveCollections(const core::CollectionDiscoveryContext& context);
void bindCollection(core::CollectionBindingContext& context);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::sony_ps2
