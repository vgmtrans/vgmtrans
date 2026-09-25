/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/Source.h"
#include "value/scan/FormatModule.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceProgram.h"

#include <array>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::pandora_box_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u32 kSequenceHeaderSize = 0x2c;
inline constexpr char kFormatName[] = "PandoraBoxSnes";
inline constexpr char kFormatId[] = "pandora-box-snes";
inline constexpr char kInstrumentDomain[] = "pandora-box-snes.instrument";

enum class Version : u8 {
  Standard,
  Traverse,
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
  u8 localInstrumentTableOffset;
  u16 globalInstrumentTableAddress;
  u8 globalInstrumentCount;
  u16 spcDirAddress;
  u8 initialTempo;
  u8 timebase;
  EchoState echo;
  std::array<std::optional<u16>, kTrackCount> tracks;
};

// The SPC700 adds in eight bits before indexing the song-relative table.
[[nodiscard]] constexpr u16 localInstrumentAddress(const Layout& layout, u8 program) {
  return static_cast<u16>(layout.sequenceHeaderAddress +
                          static_cast<u8>(layout.localInstrumentTableOffset + program));
}

struct SequenceParse {
  core::SequenceProgram program;
  std::set<u8> programs;
};

struct DynamicAdsr {
  u8 adsr1;
  u8 adsr2;
};

[[nodiscard]] const char* versionName(Version version);
[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] u8 programSrcn(core::ByteReader reader, const Layout& layout, u8 program);
[[nodiscard]] u8 decodedVolume(Version version, u8 raw);
[[nodiscard]] DynamicAdsr dynamicAdsr(u8 attack, u8 decay, u8 sustainRate, u8 sustainLevel);
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const std::set<u8>& programs,
                                                               std::string_view displayName);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::pandora_box_snes
