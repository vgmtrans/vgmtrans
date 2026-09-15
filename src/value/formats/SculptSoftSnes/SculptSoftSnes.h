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

namespace vgmtrans::formats::sculpt_soft_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kCommandLimit = 131072;
inline constexpr std::string_view kInstrumentDomain = "sculpt-soft-snes.sample";

enum class Revision { Standard, Extended };

struct Layout {
  Revision revision = Revision::Standard;
  u16 tables = 0;
  u16 directory = 0;
  u16 song = 0;
  u16 pitchTable = 0;
  u16 deltaTable = 0;
  u32 frameMicroseconds = 20000;
  u8 tracks = 0;
};

// All four automation lanes use the same eight-byte header and interpreter.
struct Curve {
  core::SourceRange range;
  u8 loopStart = 0;
  u8 loopEnd = 0xff;
  u8 releaseLead = 0;
  u8 speed = 1;
  bool interpolate = false;
  bool alternate = false;
  u16 pointCount = 0;
  std::vector<u16> points;
};

struct CurvePlayer {
  const Curve* curve = nullptr;
  u16 value = 0;
  s16 increment = 0;
  u16 hold = 0;
  u8 index = 0;
  u8 countdown = 0;
  bool active = false;

  void start(const Curve& data, u16 duration);
  void tick();
};

struct Patch {
  core::SourceRange range;
  u8 flags = 0;
  u8 gain = 0;
  u8 sample = 0;
  u8 pitch = 0;
  u8 pan = 50;
  u8 echo = 0;
};

struct DriverData {
  std::array<std::optional<Patch>, 256> patches;
  // gain, pitch, pan, sample; indexed by the patch's corresponding byte.
  std::array<std::array<std::optional<Curve>, 256>, 4> curves;
  std::array<std::optional<std::array<u8, 12>>, 256> echoes;
  std::array<s16, 256> sampleTuning{};
  std::array<u8, 256> sampleFlags{};
  // MIDI key of the pitch table's first entry, before octave shifts.
  double pitchBaseKey = 0;
  std::array<s16, 32> deltas{};
  std::set<u8> samples;
};

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] DriverData readDriverData(core::ByteReader reader, const Layout& layout);
[[nodiscard]] core::SequenceProgram decodeSequence(core::ByteReader reader, const Layout& layout,
                                                   const DriverData& data, core::AssetId id,
                                                   core::SourceMapBuilder* sourceMap = nullptr,
                                                   std::vector<core::Diagnostic>* diagnostics = nullptr,
                                                   std::set<u8>* referencedPrograms = nullptr);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const DriverData& data, const std::set<u8>& programs,
                                                               std::string_view name);
[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::sculpt_soft_snes
