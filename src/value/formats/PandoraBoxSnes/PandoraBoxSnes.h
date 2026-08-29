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

#include <array>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::pandora_box_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u32 kCommandLimit = 131072;
inline constexpr std::string_view kInstrumentDomain = "pandora-box-snes.instrument";

enum class Version : u8 {
  Standard,
  Traverse,
};

struct TrackPointer {
  u16 address;
  core::SourceRange source;
};

struct EchoState {
  s8 masterVolume = 0x60;
  s8 volume = 0x30;
  u8 delay = 0;
  s8 feedback = -0x60;
  std::array<s8, 8> fir{0x7f, 0, 0, 0, 0, 0, 0, 0};
};

struct Layout {
  Version version;
  u16 sequenceHeaderAddress;
  core::SourceRange sequenceHeaderRange;
  u16 localInstrumentTableAddress;
  u16 globalInstrumentTableAddress;
  u8 globalInstrumentCount;
  u16 spcDirAddress;
  u8 initialTempo;
  u8 timebase;
  EchoState echo;
  std::array<std::optional<TrackPointer>, kTrackCount> tracks;
};

struct SequenceReferences {
  std::set<u8> programs;
};

struct SequenceParse {
  core::SequenceProgram program;
  SequenceReferences references;
  core::SourceRange headerRange;
};

struct DynamicAdsr {
  u8 adsr1;
  u8 adsr2;
};

[[nodiscard]] const char* versionName(Version version);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] std::optional<u8> programSrcn(core::ByteReader reader, const Layout& layout, u8 program);
[[nodiscard]] u8 decodedVolume(Version version, u8 raw);
[[nodiscard]] DynamicAdsr dynamicAdsr(u8 attack, u8 decay, u8 sustainRate, u8 sustainLevel);
[[nodiscard]] core::TrackProgram decodeSourceTrack(core::ByteReader reader, const Layout& layout, u32 trackNumber,
                                                   u32 startAddress,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] core::SequenceProgramConfig sequenceConfig(const Layout& layout);
[[nodiscard]] core::SequenceRuntime sequenceRuntime(const Layout& layout);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const SequenceReferences& references,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::pandora_box_snes
