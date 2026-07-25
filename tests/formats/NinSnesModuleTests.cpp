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

void writeSection(std::vector<u8>& bytes, u16 address, std::initializer_list<std::pair<u8, u16>> tracks) {
  for (const auto [track, start] : tracks) {
    writeLe16(bytes, address + static_cast<size_t>(track) * 2, start);
  }
}

PerformanceSequence render(std::vector<u8> bytes, const Layout& layout = standardLayout()) {
  SequenceParse parsed = decodeSequence(ByteReader(SourceId{7}, bytes), layout, AssetId{1});
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program, sequenceDialect());
}

}  // namespace

void ninSnesProfilesDescribeEverySupportedDriverFamily() {
  constexpr std::array ids{
      ProfileId::Earlier,     ProfileId::Standard,     ProfileId::Rd1,        ProfileId::Rd2,
      ProfileId::Hal,         ProfileId::Konami,       ProfileId::Lemmings,   ProfileId::IntelliFe3,
      ProfileId::IntelliTa,   ProfileId::IntelliFe4,   ProfileId::Human,      ProfileId::Tose,
      ProfileId::QuintetActR, ProfileId::QuintetActR2, ProfileId::QuintetIog, ProfileId::QuintetTs,
      ProfileId::FalcomYs4,
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

  expect(profile(ProfileId::Earlier).base == BaseProfile::Earlier &&
             profile(ProfileId::Earlier).instruments == InstrumentLayout::Earlier5Byte,
         "the early driver should retain its original note and instrument layout");
  expect(profile(ProfileId::Hal).pan == PanModel::HalTable, "HAL should select its reversed pan table");
  expect(profile(ProfileId::Konami).addresses == AddressModel::KonamiBase &&
             profile(ProfileId::Konami).instruments == InstrumentLayout::KonamiTuningTable,
         "Konami should declare both of its independent driver deviations");
  expect(profile(ProfileId::Lemmings).noteParameters == NoteParameterModel::Lemmings,
         "Lemmings should select its packed note-parameter model");
  expect(profile(ProfileId::IntelliFe3).base == BaseProfile::Intelli &&
             profile(ProfileId::IntelliFe3).noteParameters == NoteParameterModel::IntelliTable &&
             profile(ProfileId::IntelliFe3).intelli == IntelliMode::Fe3,
         "FE3 should select its Intelligent Systems command and note tables");
  expect(profile(ProfileId::IntelliTa).programs == ProgramResolver::IntelliTaOverride &&
             profile(ProfileId::IntelliTa).intelli == IntelliMode::Ta,
         "TA should select dynamic instrument overrides");
  expect(profile(ProfileId::IntelliFe4).noteParameters == NoteParameterModel::IntelliTable &&
             profile(ProfileId::IntelliFe4).intelli == IntelliMode::Fe4,
         "FE4 should select its Intelligent Systems note table");
  expect(profile(ProfileId::Human).programs == ProgramResolver::Direct &&
             profile(ProfileId::Human).instrumentTable == InstrumentTableAddressModel::Human,
         "Human Entertainment should use direct programs and its table locator");
  expect(profile(ProfileId::Tose).playlist == PlaylistModel::Tose &&
             profile(ProfileId::Tose).pan == PanModel::ToseLinear &&
             profile(ProfileId::Tose).instrumentTable == InstrumentTableAddressModel::Tose,
         "TOSE should declare its playlist, pan, and instrument-table variants");
  expect(profile(ProfileId::QuintetActR).programs == ProgramResolver::QuintetActRBase &&
             profile(ProfileId::QuintetActR2).programs == ProgramResolver::QuintetLookup &&
             profile(ProfileId::QuintetIog).programs == ProgramResolver::QuintetLookup &&
             profile(ProfileId::QuintetTs).programs == ProgramResolver::QuintetLookup,
         "the four Quintet profiles should retain their two program-resolution models");
  expect(profile(ProfileId::FalcomYs4).addresses == AddressModel::FalcomBaseOffset,
         "Ys IV should select relocated Falcom addresses");

  expect(Layout{.profile = ProfileId::Konami, .konamiBaseAddress = 0x3000}.resolveAddress(0x20) == 0x3020,
         "Konami profile addresses should be relative to the detected driver base");
  expect(Layout{.profile = ProfileId::FalcomYs4, .falcomBaseOffset = 0x4000}.resolveAddress(0x20) == 0x4020,
         "Falcom profile addresses should include the relocated sequence offset");
  expect(
      instrumentHeaderSize(profile(ProfileId::Earlier)) == 5 && instrumentHeaderSize(profile(ProfileId::Standard)) == 6,
      "early and standard drivers should retain their distinct instrument layouts");
}

void ninSnesIntelligentVoiceTablesUseTypedPlaybackState() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});

  bytes[0x300] = 0xfa;  // define one inline voice record
  bytes[0x301] = 1;
  bytes[0x302] = 5;
  bytes[0x303] = 0x80;
  bytes[0x304] = 10;
  bytes[0x305] = 0;
  bytes[0x306] = 0xfb;  // load that record
  bytes[0x307] = 0;
  bytes[0x308] = 3;
  bytes[0x309] = 0xc9;
  bytes[0x30a] = 0;

  Layout layout = standardLayout();
  layout.signature = Signature::Intelligent;
  layout.profile = ProfileId::IntelliFe3;
  const PerformanceSequence performance = render(std::move(bytes), layout);
  const auto& events = performance.tracks[0].events;

  const auto instrument = std::ranges::find_if(events, [](const PerformanceEvent& event) {
    const auto* change = std::get_if<InstrumentPerformanceEvent>(&event);
    return change != nullptr && change->sourceInstrument && change->sourceInstrument->key == 5;
  });
  const auto level = std::ranges::find_if(events, [](const PerformanceEvent& event) {
    const auto* change = std::get_if<LevelPerformanceEvent>(&event);
    return change != nullptr && std::abs(change->linearGain - (128.0 / 255.0)) < 0.0001;
  });
  const auto balance = std::ranges::find_if(events, [](const PerformanceEvent& event) {
    return std::holds_alternative<StereoBalancePerformanceEvent>(event);
  });
  expect(instrument != events.end() && level != events.end() && balance != events.end(),
         "an inline Intelligent Systems voice record should execute from typed command state");
}

void ninSnesControllerFadesRemainInTheSourceDomain() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});

  bytes[0x300] = 0xe1;  // pan hard right
  bytes[0x301] = 0;
  bytes[0x302] = 0xe2;  // fade through center to hard left
  bytes[0x303] = 2;
  bytes[0x304] = 20;
  bytes[0x305] = 0xed;  // half channel level
  bytes[0x306] = 0x80;
  bytes[0x307] = 0xee;  // fade to full channel level
  bytes[0x308] = 2;
  bytes[0x309] = 0xff;
  bytes[0x30a] = 3;
  bytes[0x30b] = 0xc9;  // wait while both fades advance
  bytes[0x30c] = 0;

  const PerformanceSequence performance = render(std::move(bytes));
  const auto& events = performance.tracks[0].events;
  std::vector<const StereoBalancePerformanceEvent*> balances;
  std::vector<const LevelPerformanceEvent*> levels;
  for (const PerformanceEvent& event : events) {
    if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event)) {
      balances.push_back(balance);
    }
    if (const auto* level = std::get_if<LevelPerformanceEvent>(&event)) {
      levels.push_back(level);
    }
  }

  expect(balances.size() >= 3 && balances[0]->header.tick == 0 && balances[0]->leftGain == 0.0 &&
             balances[1]->header.tick == 1 && balances[1]->leftGain > 0.0 && balances[1]->rightGain > 0.0 &&
             balances[2]->header.tick == 2 && balances[2]->rightGain == 0.0,
         "pan fades should interpolate the N-SPC pan value and emit its actual left/right gains");
  expect(levels.size() >= 3 && std::abs(levels[0]->linearGain - (128.0 / 255.0)) < 0.0001 &&
             levels[1]->linearGain > levels[0]->linearGain && std::abs(levels[2]->linearGain - 1.0) < 0.0001,
         "volume fades should interpolate the driver's eight-bit level instead of MIDI controller values");
}

void ninSnesPlaylistCarriesTiesAcrossSectionParserResets() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0x220);
  writeLe16(bytes, 0x104, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});
  writeSection(bytes, 0x220, {{0, 0x320}});

  // Establish channel state, then play standard note C for 24 ticks.
  bytes[0x300] = 0xed;
  bytes[0x301] = 0x80;
  bytes[0x302] = 24;
  bytes[0x303] = 0x7f;
  bytes[0x304] = 0x80;
  bytes[0x305] = 0;

  // Section changes reset per-section control flow, but the driver's musical
  // state persists. The fade continues from half volume, and a leading tie
  // extends the preceding note.
  bytes[0x320] = 0xee;
  bytes[0x321] = 2;
  bytes[0x322] = 0xff;
  bytes[0x323] = 12;
  bytes[0x324] = 0x7f;
  bytes[0x325] = 0xc8;
  bytes[0x326] = 0xc9;
  bytes[0x327] = 0;

  const PerformanceSequence performance = render(std::move(bytes));
  expect(performance.diagnostics.empty(), "cross-section tie fixture should render without diagnostics");
  const MidiSequence midi = renderMidiSequence(performance);
  const auto note = std::ranges::find_if(
      midi.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); });
  expect(note != midi.tracks[0].events.end() && std::get<NoteDuration>(*note).tick == 0 &&
             std::get<NoteDuration>(*note).duration == 34,
         "a leading tie in the next section should extend the previous duration note");
  expect(std::ranges::any_of(performance.tracks[0].events,
                             [](const PerformanceEvent& event) {
                               const auto* level = std::get_if<LevelPerformanceEvent>(&event);
                               return level != nullptr && level->header.tick == 25 &&
                                      level->linearGain > (128.0 / 255.0) && level->linearGain < 1.0;
                             }),
         "a fade in the next section should continue from the preceding channel level");
}

void ninSnesPercussionStartsPerNoteVibratoFade() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});

  // Configure vibrato and its reusable per-note fade, then play percussion.
  // Percussion reaches the same voice pitch/vibrato path as melodic notes in
  // the driver; only its instrument and output key are different.
  bytes[0x300] = 0xe3;
  bytes[0x301] = 0;
  bytes[0x302] = 0x20;
  bytes[0x303] = 0x80;
  bytes[0x304] = 0xf0;
  bytes[0x305] = 8;
  bytes[0x306] = 24;
  bytes[0x307] = 0x7f;
  bytes[0x308] = 0xca;
  bytes[0x309] = 0;

  const PerformanceSequence performance = render(std::move(bytes));
  const size_t vibratoFadeSamples =
      std::ranges::count_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
        const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
        return modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoDepth &&
               modulation->header.tick != 0;
      });
  expect(vibratoFadeSamples != 0, "percussion notes should advance a configured per-note vibrato fade");
}

void ninSnesGainModeInstrumentsUseDspEnvelope() {
  std::vector<u8> bytes(kAramSize);

  // One direct-GAIN instrument followed by a structurally invalid header that
  // terminates the table. Its sample is a single non-looping BRR block.
  bytes[0x4000] = 0;
  bytes[0x4001] = 0;
  bytes[0x4002] = 0;
  bytes[0x4003] = 0x7f;
  bytes[0x4004] = 1;
  bytes[0x4005] = 0;
  bytes[0x4006] = 0x80;
  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x6000] = 0x01;

  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "gain-mode.spc"}, std::move(bytes));
  ScanIdAllocator ids;
  ScanResultBuilder result(
      ScanInput{
          .source = sources.source(source),
          .reader = sources.reader(source),
          .ids = ids,
      },
      "NinSnes");
  const auto instrumentSet = result.reserveInstrumentSet();
  const auto sampleCollection = result.reserveSampleCollection();
  Layout layout = standardLayout();
  layout.instrumentTableAddress = 0x4000;
  layout.spcDirAddress = 0x5000;

  expect(addSynth(result, instrumentSet, sampleCollection, layout, {}, "GAIN"),
         "NinSnes synth builder should accept a direct-GAIN instrument");
  const ScanResult scan = result.finish();
  const auto* instruments = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  expect(
      instruments != nullptr && instruments->instruments.size() == 1 && instruments->instruments[0].regions.size() == 1,
      "direct-GAIN fixture should produce one instrument region");
  const Envelope& envelope = instruments->instruments[0].regions[0].envelope;
  expect(envelope.sustainAmplitude && *envelope.sustainAmplitude > 0.99 && *envelope.sustainAmplitude < 1.0,
         "direct GAIN should become the DSP's fixed sustain level instead of an unspecified envelope");
}
