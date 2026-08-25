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
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::nin_snes {

inline constexpr u32 kAramSize = 0x10000;
inline constexpr u32 kTrackCount = 8;
inline constexpr u16 kPpqn = 48;
inline constexpr u8 kStandardTimerTarget = 0x10;
inline constexpr std::string_view kInstrumentDomain = "nin-snes.instrument";
inline constexpr u32 kEarlierPercussionProgramBase = 0x100;

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

[[nodiscard]] constexpr bool isInfinitePlaylistRepeat(PlaylistModel model, u16 value) {
  return value <= 0xff && (model == PlaylistModel::Tose ? value == 0 || value == 0xff : value > 0x80);
}

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
  // Applied by the tempo command before it stores the driver's tempo value.
  u8 tempoCommandMultiplier = 1;
};

struct KonamiPercussionLayout {
  u16 tableAddress = 0;
  u8 slotCount = 0;
  u8 programBase = 0;
};

struct Layout {
  Signature signature = Signature::None;
  ProfileId profile = ProfileId::Unknown;
  u8 songIndex = 0xff;
  u32 songListAddress = 0;
  u32 playlistAddress = 0;
  u8 sectionPointerAddress = 0;

  std::optional<u32> instrumentTableAddress;
  std::optional<u32> percussionTableAddress;
  std::optional<u16> spcDirAddress;
  u16 konamiBaseAddress = 0;
  u16 falcomBaseOffset = 0;
  u8 quintetBgmInstrumentBase = 0;
  u16 quintetInstrumentLookupAddress = 0;
  std::optional<u8> fixedPercussionBase;
  std::optional<KonamiPercussionLayout> konamiPercussion;
  u16 konamiTuningTableAddress = 0;
  u8 konamiTuningTableSize = 0;
  // SPC timer targets are measured in 125-microsecond units. Most N-SPC
  // drivers use $10, but Konami's derivatives use slower driver ticks.
  u8 tempoTimerTarget = kStandardTimerTarget;

  std::vector<u8> volumeTable;
  std::vector<u8> durationRateTable;

  [[nodiscard]] u16 resolveAddress(u16 rawAddress) const;
};

struct InstrumentOverride {
  u32 program = 0;
  u8 tuningProgram = 0;
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
  s16 sourceKey = 0x3c;

  friend bool operator==(const DrumSlot&, const DrumSlot&) = default;
};

struct DrumKit {
  u8 program = 0;
  std::vector<DrumSlot> slots;
};

struct SequenceRecipes {
  std::vector<InstrumentOverride> overrides;
  std::vector<DrumKit> drumKits;
};

struct SequenceParse {
  core::SequenceProgram program;
  SequenceRecipes recipes;
};

[[nodiscard]] const Profile& profile(ProfileId id);
[[nodiscard]] u32 instrumentHeaderSize(const Profile& profile);
[[nodiscard]] u16 instrumentSlotCount(const Profile& profile);

[[nodiscard]] std::optional<Layout> findLayout(core::ByteReader reader);
[[nodiscard]] bool isValidPlaylist(core::ByteReader reader, const Layout& layout);
[[nodiscard]] const core::SequenceProgramConfig& sequenceConfig();
[[nodiscard]] SequenceParse decodeSequence(core::ByteReader reader, const Layout& layout, core::AssetId sequenceId,
                                           core::SourceMapBuilder* sourceMap = nullptr,
                                           std::vector<core::Diagnostic>* diagnostics = nullptr);
[[nodiscard]] std::optional<core::ScanSoundBankDraft> addSynth(core::ScanResultBuilder& builder, const Layout& layout,
                                                               const SequenceRecipes& recipes,
                                                               std::string_view displayName);

[[nodiscard]] core::FormatModule module();

}  // namespace vgmtrans::formats::nin_snes
