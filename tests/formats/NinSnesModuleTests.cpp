/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnes.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::nin_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

Layout standardLayout(u16 playlist = 0x100) {
  return Layout{
      .signature = Signature::Standard,
      .profile = ProfileId::Standard,
      .playlistAddress = playlist,
  };
}

void writeSection(std::vector<u8>& bytes, u16 address,
                  std::initializer_list<std::pair<u8, u16>> tracks) {
  for (const auto [track, start] : tracks) {
    writeLe16(bytes, address + static_cast<size_t>(track) * 2, start);
  }
}

PerformanceSequence render(std::vector<u8> bytes, const Layout& layout = standardLayout()) {
  SequenceParse parsed = decodeSequence(ByteReader(SourceId{7}, bytes), layout, AssetId{1});
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program, sequenceDialect());
}

const NotePerformanceEvent* firstNote(const PerformanceTrack& track) {
  for (const PerformanceEvent& event : track.events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      return note;
    }
  }
  return nullptr;
}

}  // namespace

void ninSnesProfilesDescribeEverySupportedDriverFamily() {
  constexpr std::array ids{
      ProfileId::Earlier,     ProfileId::Standard,    ProfileId::Rd1,
      ProfileId::Rd2,        ProfileId::Hal,         ProfileId::Konami,
      ProfileId::Lemmings,   ProfileId::IntelliFe3,  ProfileId::IntelliTa,
      ProfileId::IntelliFe4, ProfileId::Human,       ProfileId::Tose,
      ProfileId::QuintetActR, ProfileId::QuintetActR2, ProfileId::QuintetIog,
      ProfileId::QuintetTs,  ProfileId::FalcomYs4,
  };
  std::set<std::string_view> names;
  for (const ProfileId id : ids) {
    const Profile& driver = profile(id);
    expect(driver.id == id && driver.base != BaseProfile::Unknown,
           "every public NinSnes profile should resolve to a complete driver description");
    expect(names.emplace(driver.name).second, "NinSnes profile names should be unique");
  }
  expect(profile(ProfileId::Unknown).id == ProfileId::Unknown,
         "the unknown profile should remain an explicit safe fallback");

  expect(convertAddress(profile(ProfileId::Konami), 0x20, 0x3000, 0) == 0x3020,
         "Konami profile addresses should be relative to the detected driver base");
  expect(convertAddress(profile(ProfileId::FalcomYs4), 0x20, 0, 0x4000) == 0x4020,
         "Falcom profile addresses should include the relocated sequence offset");
  expect(instrumentHeaderSize(profile(ProfileId::Earlier)) == 5 &&
             instrumentHeaderSize(profile(ProfileId::Standard)) == 6,
         "early and standard drivers should retain their distinct instrument layouts");
}

void ninSnesPlaylistCarriesTiesAcrossSectionParserResets() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0x220);
  writeLe16(bytes, 0x104, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});
  writeSection(bytes, 0x220, {{0, 0x320}});

  // Standard note parameters: duration 24, full quantize, then note C.
  bytes[0x300] = 24;
  bytes[0x301] = 0x7f;
  bytes[0x302] = 0x80;
  bytes[0x303] = 0;

  // A new source-track parser starts here, but legacy MIDI state retains the
  // prior duration note so this leading tie extends it across the boundary.
  bytes[0x320] = 12;
  bytes[0x321] = 0x7f;
  bytes[0x322] = 0xc8;
  bytes[0x323] = 0xc9;
  bytes[0x324] = 0;

  const PerformanceSequence performance = render(std::move(bytes));
  expect(performance.diagnostics.empty(), "cross-section tie fixture should render without diagnostics");
  const MidiSequence midi = renderMidiSequence(performance);
  const auto note = std::ranges::find_if(midi.tracks[0].events, [](const MidiEvent& event) {
    return std::holds_alternative<NoteDuration>(event);
  });
  expect(note != midi.tracks[0].events.end() &&
             std::get<NoteDuration>(*note).tick == 0 &&
             std::get<NoteDuration>(*note).duration == 34,
         "a leading tie in the next section should extend the previous duration note");
}

void ninSnesSectionEndUsesEachChannelsScheduledNoteLimit() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}, {1, 0x340}});

  // Track zero ends the section after twelve ticks.
  bytes[0x300] = 12;
  bytes[0x301] = 0x7f;
  bytes[0x302] = 0xc9;
  bytes[0x303] = 0;

  // Track one is still sleeping until tick 24. Legacy caps its note at that
  // channel-local wake-up, not at track zero's shared section boundary.
  bytes[0x340] = 24;
  bytes[0x341] = 0x7f;
  bytes[0x342] = 0x80;
  bytes[0x343] = 0;

  const PerformanceSequence performance = render(std::move(bytes));
  const auto* note = firstNote(performance.tracks[1]);
  expect(note != nullptr && note->durationTicks == 22,
         "parallel section shutdown should preserve the waiting channel's full note tail");
}

void ninSnesSameTickControllerFadeSurvivesSectionBoundary() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}, {1, 0x340}});

  // Standard E5/E6 are master volume and master-volume fade. Both commands
  // execute at tick zero before the later channel exposes the zero-tick end.
  bytes[0x300] = 0xe5;
  bytes[0x301] = 70;
  bytes[0x302] = 0xe6;
  bytes[0x303] = 16;
  bytes[0x304] = 114;
  bytes[0x305] = 0;
  bytes[0x340] = 0;

  const PerformanceSequence performance = render(std::move(bytes));
  const size_t futureMasterLevels =
      std::ranges::count_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
        const auto* level = std::get_if<MasterLevelPerformanceEvent>(&event);
        return level != nullptr && level->header.tick > 0;
      });
  expect(futureMasterLevels != 0,
         "a controller fade committed at the boundary tick should retain its scheduled samples");
}
