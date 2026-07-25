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

namespace vgmtrans::formats::nin_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr std::string_view kInstrumentDomain = "nin-snes.instrument";

enum class Signature : u8 {
  None,
  Standard,
  Earlier,
  Konami,
  Intelligent,
  Human,
  Tose,
  Quintet,
  FalcomYs4,
};

enum class ProfileId : u8 {
  Unknown,
  Earlier,
  Standard,
  Rd1,
  Rd2,
  Hal,
  Konami,
  Lemmings,
  IntelliFe3,
  IntelliTa,
  IntelliFe4,
  Human,
  Tose,
  QuintetActR,
  QuintetActR2,
  QuintetIog,
  QuintetTs,
  FalcomYs4,
};

enum class BaseProfile : u8 {
  Unknown,
  Earlier,
  Standard,
  Intelli,
};

enum class AddressModel : u8 {
  Direct,
  KonamiBase,
  FalcomBaseOffset,
};

enum class PlaylistModel : u8 {
  Unknown,
  Standard,
  Tose,
};

enum class NoteParameterModel : u8 {
  Standard,
  Lemmings,
  IntelliTable,
};

enum class ProgramResolver : u8 {
  Direct,
  StandardPercussion,
  QuintetActRBase,
  QuintetLookup,
  IntelliTaOverride,
};

enum class PanModel : u8 {
  StandardTable,
  HalTable,
  ToseLinear,
};

enum class InstrumentLayout : u8 {
  Earlier5Byte,
  Standard6Byte,
  KonamiTuningTable,
};

enum class InstrumentTableAddressModel : u8 {
  Standard,
  Human,
  Tose,
};

enum class IntelliMode : u8 {
  None,
  Fe3,
  Ta,
  Fe4,
};

struct Profile {
  ProfileId id = ProfileId::Unknown;
  std::string_view name = "Unknown";
  // Most variants are small deviations from the standard N-SPC driver.
  // Keeping those defaults here makes each profile declaration describe only
  // what that variant actually changes.
  BaseProfile base = BaseProfile::Standard;
  AddressModel addresses = AddressModel::Direct;
  PlaylistModel playlist = PlaylistModel::Standard;
  NoteParameterModel noteParameters = NoteParameterModel::Standard;
  ProgramResolver programs = ProgramResolver::StandardPercussion;
  PanModel pan = PanModel::StandardTable;
  InstrumentLayout instruments = InstrumentLayout::Standard6Byte;
  InstrumentTableAddressModel instrumentTable = InstrumentTableAddressModel::Standard;
  IntelliMode intelli = IntelliMode::None;
};

struct Layout {
  Signature signature = Signature::None;
  ProfileId profile = ProfileId::Unknown;
  u8 songIndex = 0xff;
  u32 songListAddress = 0;
  u32 playlistAddress = 0;
  u8 sectionPointerAddress = 0;

  std::optional<u32> instrumentTableAddress;
  std::optional<u16> spcDirAddress;
  u16 konamiBaseAddress = 0;
  u16 falcomBaseOffset = 0;
  u8 quintetBgmInstrumentBase = 0;
  u16 quintetInstrumentLookupAddress = 0;
  u16 konamiTuningTableAddress = 0;
  u8 konamiTuningTableSize = 0;

  std::vector<u8> volumeTable;
  std::vector<u8> durationRateTable;

  [[nodiscard]] u16 resolveAddress(u16 rawAddress) const;
};

struct InstrumentOverride {
  u32 program = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  u8 pitchHigh = 0;
  u8 pitchLow = 0;
  core::SourceRange source;
};

struct DrumSlot {
  u8 key = 0;
  u32 sourceProgram = 0;
  u8 sourceNote = 0x3c;
  s8 globalTranspose = 0;

  friend bool operator==(const DrumSlot&, const DrumSlot&) = default;
};

enum class DrumPitchModel : u8 {
  StandardMapping,
  IntelliPlayedNote,
};

struct DrumKit {
  u8 program = 0;
  DrumPitchModel pitchModel = DrumPitchModel::StandardMapping;
  std::vector<DrumSlot> slots;

  friend bool operator==(const DrumKit&, const DrumKit&) = default;
};

struct SequenceRecipes {
  std::vector<InstrumentOverride> overrides;
  std::vector<DrumKit> drumKits;
  double maxVibratoDepthCents = 0.0;
  double maxVibratoRateHertz = 0.0;
};

struct SequenceParse {
  core::SequenceProgram program;
  SequenceRecipes recipes;
};

[[nodiscard]] const Profile& profile(ProfileId id);
[[nodiscard]] u32 instrumentHeaderSize(const Profile& profile);
[[nodiscard]] u16 instrumentSlotCount(const Profile& profile);

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] const core::SequenceDialect& sequenceDialect();
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] bool addSynth(core::ScanResultBuilder& builder, core::ScanInstrumentSetRef instrumentSet,
                            core::ScanSampleCollectionRef sampleCollection, const Layout& layout,
                            const SequenceRecipes& recipes, std::string_view displayName);

[[nodiscard]] core::FormatDefinition definition();

}  // namespace vgmtrans::formats::nin_snes
