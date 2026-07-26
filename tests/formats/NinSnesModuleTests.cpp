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
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::nin_snes;

namespace {

constexpr std::array kProfileIds{
    ProfileId::Earlier,   ProfileId::Standard,  ProfileId::Rd1,         ProfileId::Rd2,          ProfileId::Hal,
    ProfileId::Konami,    ProfileId::Lemmings,  ProfileId::IntelliFe3,  ProfileId::IntelliTa,    ProfileId::IntelliFe4,
    ProfileId::Human,     ProfileId::Tose,      ProfileId::QuintetActR, ProfileId::QuintetActR2, ProfileId::QuintetIog,
    ProfileId::QuintetTs, ProfileId::FalcomYs4,
};

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

double ninSnesLevelGain(u8 raw) {
  const double normalized = raw / 255.0;
  return normalized * normalized;
}

struct LevelOpcodes {
  u8 master;
  u8 channel;
  u8 rest;
};

struct ModulationOpcodes {
  u8 vibrato;
  u8 tremolo;
  u8 tremoloOff;
  u8 tempo;
  u8 rest;
};

LevelOpcodes levelOpcodes(const Profile& driver) {
  if (driver.base == BaseProfile::Earlier) {
    return {.master = 0xe0, .channel = 0xe7, .rest = 0xc7};
  }
  if (driver.intelli == IntelliMode::Fe3) {
    return {.master = 0xdb, .channel = 0xe3, .rest = 0xc9};
  }
  if (driver.intelli == IntelliMode::Ta || driver.intelli == IntelliMode::Fe4) {
    return {.master = 0xdf, .channel = 0xe7, .rest = 0xc9};
  }
  const bool hasMasterVolume = driver.id != ProfileId::Konami && driver.id != ProfileId::Lemmings;
  return {.master = static_cast<u8>(hasMasterVolume ? 0xe5 : 0), .channel = 0xed, .rest = 0xc9};
}

ModulationOpcodes modulationOpcodes(const Profile& driver) {
  if (driver.base == BaseProfile::Earlier) {
    return {.vibrato = 0xde, .tremolo = 0xe5, .tremoloOff = 0xe6, .tempo = 0xe2, .rest = 0xc7};
  }
  if (driver.intelli == IntelliMode::Fe3) {
    return {.vibrato = 0xd9, .tremolo = 0xe1, .tremoloOff = 0xe2, .tempo = 0xdd, .rest = 0xc9};
  }
  if (driver.intelli == IntelliMode::Ta || driver.intelli == IntelliMode::Fe4) {
    return {.vibrato = 0xdd, .tremolo = 0xe5, .tremoloOff = 0xe6, .tempo = 0xe1, .rest = 0xc9};
  }
  return {.vibrato = 0xe3, .tremolo = 0xeb, .tremoloOff = 0xec, .tempo = 0xe7, .rest = 0xc9};
}

u8 pitchSlideOpcode(const Profile& driver) {
  if (driver.base == BaseProfile::Earlier) {
    return 0xdd;
  }
  if (driver.intelli == IntelliMode::Fe3) {
    return 0xef;
  }
  if (driver.intelli == IntelliMode::Ta || driver.intelli == IntelliMode::Fe4) {
    return 0xf3;
  }
  return 0xf9;
}

}  // namespace

void ninSnesProfilesDescribeEverySupportedDriverFamily() {
  std::set<std::string_view> names;
  for (const ProfileId id : kProfileIds) {
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

void ninSnesProfilesShareSquaredLevelCurve() {
  constexpr u8 kMasterLevel = 120;
  constexpr u8 kChannelLevel = 140;

  for (const ProfileId id : kProfileIds) {
    std::vector<u8> bytes(kAramSize);
    writeLe16(bytes, 0x100, 0x200);
    writeLe16(bytes, 0x102, 0);
    writeSection(bytes, 0x200, {{0, 0x300}});

    const Profile& driver = profile(id);
    const LevelOpcodes opcodes = levelOpcodes(driver);
    size_t offset = 0x300;
    if (opcodes.master != 0) {
      bytes[offset++] = opcodes.master;
      bytes[offset++] = kMasterLevel;
    }
    bytes[offset++] = opcodes.channel;
    bytes[offset++] = kChannelLevel;
    bytes[offset++] = 1;
    bytes[offset++] = opcodes.rest;
    bytes[offset] = 0;

    Layout layout = standardLayout();
    layout.profile = id;
    if (driver.base == BaseProfile::Intelli) {
      layout.signature = Signature::Intelligent;
    } else if (driver.base == BaseProfile::Earlier) {
      layout.signature = Signature::Earlier;
    }

    const PerformanceSequence performance = render(std::move(bytes), layout);
    const auto& events = performance.tracks[0].events;
    const auto level = std::ranges::find_if(
        events, [](const PerformanceEvent& event) { return std::holds_alternative<LevelPerformanceEvent>(event); });
    const auto master = std::ranges::find_if(events, [](const PerformanceEvent& event) {
      return std::holds_alternative<MasterLevelPerformanceEvent>(event);
    });
    const std::string label(driver.name);
    expect(level != events.end(), label + " should emit channel gain");
    expect(std::abs(std::get<LevelPerformanceEvent>(*level).linearGain - ninSnesLevelGain(kChannelLevel)) < 0.0001,
           label + " should expose squared channel gain, got " +
               std::to_string(std::get<LevelPerformanceEvent>(*level).linearGain));
    if (opcodes.master != 0) {
      expect(master != events.end(), label + " should emit master gain");
      expect(
          std::abs(std::get<MasterLevelPerformanceEvent>(*master).linearGain - ninSnesLevelGain(kMasterLevel)) < 0.0001,
          label + " should expose squared master gain, got " +
              std::to_string(std::get<MasterLevelPerformanceEvent>(*master).linearGain));
    }

    const MidiSequence midi = renderMidiSequence(performance);
    const auto volume = std::ranges::find_if(
        midi.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<Volume14>(event); });
    const auto masterVolume = std::ranges::find_if(
        midi.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<MasterVolume>(event); });
    expect(volume != midi.tracks[0].events.end() &&
               (std::get<Volume14>(*volume).value >> 7) == static_cast<u16>(kChannelLevel / 2),
           label + " should retain the legacy channel-controller MSB");
    if (opcodes.master != 0) {
      expect(masterVolume != midi.tracks[0].events.end() &&
                 (std::get<MasterVolume>(*masterVolume).value >> 7) == static_cast<u16>(kMasterLevel / 2),
             label + " should retain the legacy master-volume MSB");
    }
  }
}

void ninSnesProfilesShareTempoRelativeVibratoClock() {
  for (const ProfileId id : kProfileIds) {
    std::vector<u8> bytes(kAramSize);
    writeLe16(bytes, 0x100, 0x200);
    writeLe16(bytes, 0x102, 0);
    writeSection(bytes, 0x200, {{0, 0x300}, {1, 0x320}});

    const Profile& driver = profile(id);
    const ModulationOpcodes opcodes = modulationOpcodes(driver);
    size_t first = 0x300;
    bytes[first++] = opcodes.vibrato;
    bytes[first++] = 3;
    bytes[first++] = 0x20;
    bytes[first++] = 0x40;
    bytes[first++] = 4;
    bytes[first++] = opcodes.rest;
    bytes[first++] = 4;
    bytes[first++] = opcodes.rest;
    bytes[first] = 0;

    size_t second = 0x320;
    bytes[second++] = 4;
    bytes[second++] = opcodes.rest;
    bytes[second++] = opcodes.tempo;
    bytes[second++] = 0x40;
    bytes[second++] = 4;
    bytes[second++] = opcodes.rest;
    bytes[second] = 0;

    Layout layout = standardLayout();
    layout.profile = id;
    if (driver.base == BaseProfile::Intelli) {
      layout.signature = Signature::Intelligent;
    } else if (driver.base == BaseProfile::Earlier) {
      layout.signature = Signature::Earlier;
    }

    const PerformanceSequence performance = render(std::move(bytes), layout);
    std::vector<const ModulationPerformanceEvent*> rates;
    for (const PerformanceEvent& event : performance.tracks[0].events) {
      const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
      if (modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoRate) {
        rates.push_back(modulation);
      }
    }
    const std::string label(driver.name);
    expect(rates.size() == 2 && rates[0]->cyclesPerTick && rates[1]->cyclesPerTick && rates[0]->frequencyHz &&
               rates[1]->frequencyHz && std::abs(*rates[0]->cyclesPerTick - 0.125) < 0.0001 &&
               std::abs(*rates[1]->cyclesPerTick - 0.125) < 0.0001 &&
               std::abs(*rates[0]->frequencyHz - 7.8125) < 0.0001 && std::abs(*rates[1]->frequencyHz - 15.625) < 0.0001,
           label + " should share the sequence-clocked N-SPC vibrato behavior");
  }
}

void ninSnesProfilesEmitSubtractiveTremolo() {
  constexpr u8 kDelay = 3;
  constexpr u8 kRate = 0x20;
  constexpr u8 kDepth = 0x40;

  for (const ProfileId id : kProfileIds) {
    std::vector<u8> bytes(kAramSize);
    writeLe16(bytes, 0x100, 0x200);
    writeLe16(bytes, 0x102, 0);
    writeSection(bytes, 0x200, {{0, 0x300}});

    const Profile& driver = profile(id);
    const ModulationOpcodes opcodes = modulationOpcodes(driver);
    size_t cursor = 0x300;
    bytes[cursor++] = opcodes.tremolo;
    bytes[cursor++] = kDelay;
    bytes[cursor++] = kRate;
    bytes[cursor++] = kDepth;
    bytes[cursor++] = opcodes.tremoloOff;
    bytes[cursor++] = 4;
    bytes[cursor++] = opcodes.rest;
    bytes[cursor] = 0;

    Layout layout = standardLayout();
    layout.profile = id;
    if (driver.base == BaseProfile::Intelli) {
      layout.signature = Signature::Intelligent;
    } else if (driver.base == BaseProfile::Earlier) {
      layout.signature = Signature::Earlier;
    }

    const PerformanceSequence performance = render(std::move(bytes), layout);
    std::vector<const ModulationPerformanceEvent*> depths;
    const ModulationPerformanceEvent* rate = nullptr;
    const TremoloDelayPerformanceEvent* delay = nullptr;
    for (const PerformanceEvent& event : performance.tracks[0].events) {
      if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
        if (modulation->target == ModulationPerformanceTarget::TremoloDepth) {
          depths.push_back(modulation);
        } else if (modulation->target == ModulationPerformanceTarget::TremoloRate) {
          rate = modulation;
        }
      } else if (const auto* tremoloDelay = std::get_if<TremoloDelayPerformanceEvent>(&event)) {
        delay = tremoloDelay;
      }
    }

    const int trough = driver.base == BaseProfile::Earlier ? 255 - ((255 * ((255 * kDepth) >> 8)) >> 8) : 255 - kDepth;
    const double expectedDepth = 20.0 * std::log10(255.0 / trough);
    const std::string label(driver.name);
    expect(depths.size() == 2, label + " should emit tremolo-on and tremolo-off depth events");
    expect(depths[0]->volumeDepthDecibels && std::abs(*depths[0]->volumeDepthDecibels - expectedDepth) < 0.0001,
           label + " should convert N-SPC tremolo depth to physical decibels");
    expect(depths[0]->waveform == LfoWaveform::Triangle && depths[0]->initialPhaseCycles == 0.25 &&
               !depths[0]->phaseRunsAtZeroDepth && depths[0]->tremoloGainMode == TremoloGainMode::NoBoost,
           label + " should emit a subtractive triangle beginning at nominal gain");
    expect(depths[1]->volumeDepthDecibels == 0.0, label + " should disable tremolo by clearing its depth");
    expect(rate != nullptr && rate->cyclesPerTick && rate->frequencyHz &&
               std::abs(*rate->cyclesPerTick - 0.125) < 0.0001 && std::abs(*rate->frequencyHz - 7.8125) < 0.0001 &&
               rate->initialPhaseCycles == 0.25,
           label + " should use the sequence-clocked N-SPC tremolo rate");
    expect(delay != nullptr && delay->delayTicks == kDelay && delay->milliseconds &&
               std::abs(*delay->milliseconds - 48.0) < 0.0001 && delay->tempoRelative,
           label + " should resolve the N-SPC tremolo delay against tempo");
  }
}

void ninSnesNoteVelocityPreservesLegacyCurve() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});

  bytes[0x300] = 3;
  bytes[0x301] = 0x7f;
  bytes[0x302] = 0x80;
  bytes[0x303] = 3;
  bytes[0x304] = 0x77;
  bytes[0x305] = 0x80;
  bytes[0x306] = 0;

  Layout layout = standardLayout();
  layout.volumeTable.assign(16, 0);
  layout.volumeTable[7] = 152;
  layout.volumeTable[15] = 252;
  const PerformanceSequence performance = render(std::move(bytes), layout);
  const MidiSequence midi = renderMidiSequence(performance);
  std::vector<u8> velocities;
  for (const MidiEvent& event : midi.tracks[0].events) {
    if (const auto* note = std::get_if<NoteDuration>(&event)) {
      velocities.push_back(note->velocity);
    }
  }

  expect(velocities == std::vector<u8>{126, 76},
         "NinSnes note velocity should preserve the legacy full/echo pair instead of compressing it to 126/98");
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
    return change != nullptr && std::abs(change->linearGain - ninSnesLevelGain(128)) < 0.0001;
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
  expect(levels.size() >= 3 && std::abs(levels[0]->linearGain - ninSnesLevelGain(128)) < 0.0001 &&
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
                                      level->linearGain > ninSnesLevelGain(128) && level->linearGain < 1.0;
                             }),
         "a fade in the next section should continue from the preceding channel level");
}

void ninSnesF9UsesSharedPitchTransitions() {
  const auto commandBytes = [](std::initializer_list<u8> commands) {
    std::vector<u8> bytes(kAramSize);
    writeLe16(bytes, 0x100, 0x200);
    writeLe16(bytes, 0x102, 0);
    writeSection(bytes, 0x200, {{0, 0x300}});
    std::ranges::copy(commands, bytes.begin() + 0x300);
    return bytes;
  };
  const auto renderCommands = [&](std::initializer_list<u8> commands) { return render(commandBytes(commands)); };

  // C glides to C-sharp after a two-tick delay. Three source steps divide one
  // semitone as 85/256, 85/256, and the exact final target.
  const PerformanceSequence performance = renderCommands({12, 0x7f, 0x80, 0xf9, 2, 3, 1, 0});
  expect(performance.diagnostics.empty() && performance.tracks[0].automations.size() == 1,
         "NinSnes F9 should produce one shared pitch transition");
  const PerformanceAutomation& automation = performance.tracks[0].automations.front();
  const auto* transition = pitchTransitionIntent(automation);
  expect(transition != nullptr && automation.realization.startTick == 2 && automation.realization.endTick == 5 &&
             transition->startKey == 24.0 && transition->targetKey == 25.0 && transition->timing.timelineTicks == 3,
         "F9 should retain its note anchor, delay, duration, and destination");
  const SequenceParse parsed = decodeSequence(ByteReader(SourceId{7}, commandBytes({12, 0x7f, 0x80, 0xf9, 2, 3, 1, 0})),
                                              standardLayout(), AssetId{1});
  const auto& commands = parsed.program.tracks[0].commands;
  const auto decodedSlide = std::ranges::find(commands, u8{0xf9}, &SourceCommand::opcode);
  expect(decodedSlide != commands.end() && decodedSlide->encodedSize == 4 && decodedSlide->address.value == 0x303 &&
             decodedSlide->execution.duringWait.valid(),
         "F9 should remain an independent source command with generic during-wait eligibility");
  const auto* sampled = transition == nullptr ? nullptr : std::get_if<SampledAutomationCurve>(&transition->curve);
  expect(sampled != nullptr && sampled->samples.size() == 4 && sampled->samples[0].tickOffset == 0 &&
             sampled->samples[0].value == 24.0 && sampled->samples[1].tickOffset == 1 &&
             std::abs(sampled->samples[1].value - (24.0 + 85.0 / 256.0)) < 0.000001 &&
             sampled->samples[2].tickOffset == 2 &&
             std::abs(sampled->samples[2].value - (24.0 + 170.0 / 256.0)) < 0.000001 &&
             sampled->samples[3].tickOffset == 3 && sampled->samples[3].value == 25.0,
         "F9 should retain the driver's exact fixed-point pitch staircase");
  expect(std::ranges::none_of(
             performance.tracks[0].events,
             [](const PerformanceEvent& event) { return std::holds_alternative<PitchBendPerformanceEvent>(event); }),
         "F9 format playback should not choose a MIDI pitch representation");

  const MidiSequence pitchBend = renderMidiSequence(performance);
  expect(
      std::ranges::any_of(pitchBend.tracks[0].events,
                          [](const MidiEvent& event) {
                            const auto* bend = std::get_if<PitchBend>(&event);
                            return bend != nullptr && bend->value != 0;
                          }) &&
          std::ranges::none_of(pitchBend.tracks[0].events,
                               [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }),
      "NinSnes should retain exact F9 pitch bends by default");

  const MidiSequence portamento =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(std::ranges::any_of(portamento.tracks[0].events,
                             [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) &&
             std::ranges::none_of(portamento.tracks[0].events,
                                  [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }),
         "an explicit portamento export should lower F9 as native portamento");

  // A note pitch envelope owns the same driver motion first. F9 begins only
  // after that motion finishes and retains the pitch the envelope established.
  const PerformanceSequence afterEnvelope = renderCommands({0xf1, 0, 2, 2, 10, 0x7f, 0x80, 0xf9, 1, 3, 5, 0});
  const auto* envelopeTransition = pitchTransitionIntent(afterEnvelope.tracks[0].automations.front());
  const auto* envelopeSamples =
      envelopeTransition == nullptr ? nullptr : std::get_if<SampledAutomationCurve>(&envelopeTransition->curve);
  expect(envelopeTransition != nullptr && afterEnvelope.tracks[0].automations.front().realization.startTick == 3 &&
             envelopeTransition->startKey == 26.0 && envelopeTransition->targetKey == 29.0 &&
             envelopeSamples != nullptr && envelopeSamples->samples.size() == 4,
         "F9 should start from a completed pitch envelope without losing its queued timing");

  const MidiSequence envelopePitchBend = renderMidiSequence(afterEnvelope);
  expect(std::ranges::none_of(envelopePitchBend.tracks[0].events,
                              [](const MidiEvent& event) {
                                const auto* bend = std::get_if<PitchBend>(&event);
                                return bend != nullptr && bend->tick == 0 && bend->value != 0;
                              }) &&
             std::ranges::any_of(envelopePitchBend.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* bend = std::get_if<PitchBend>(&event);
                                   return bend != nullptr && bend->tick == 1 && bend->value != 0;
                                 }),
         "pitch-bend lowering should not apply F9's post-envelope starting pitch at note attack");

  const MidiSequence envelopePortamento = renderMidiSequence(
      afterEnvelope, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(std::ranges::any_of(envelopePortamento.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* note = std::get_if<NoteDuration>(&event);
                               return note != nullptr && note->tick == 0 && note->key == 24;
                             }) &&
             std::ranges::any_of(envelopePortamento.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* bend = std::get_if<PitchBend>(&event);
                                   return bend != nullptr && bend->tick == 3 && bend->value == 0;
                                 }),
         "native portamento should preserve the preceding envelope and center its bend at the F9 handoff");

  const PerformanceSequence consecutive =
      renderCommands({5, 0x7f, 0x80, 0xf9, 0, 3, 3, 0xf9, 0, 3, 6, 10, 0x7f, 0xc8, 0});
  const MidiSequence consecutivePitchBend = renderMidiSequence(consecutive);
  expect(std::ranges::none_of(consecutivePitchBend.tracks[0].events,
                              [](const MidiEvent& event) {
                                const auto* bend = std::get_if<PitchBend>(&event);
                                return bend != nullptr && bend->tick == 0 && bend->value != 0;
                              }),
         "a later F9 should inherit the preceding transition instead of pre-bending the note attack");

  // Adjacent F9 commands are queued by the driver. If the wait expires before
  // the first delayed slide starts, the last command replaces it at that tick.
  const PerformanceSequence queued = renderCommands({5, 0x7f, 0x80, 0xf9, 8, 3, 4, 0xf9, 2, 3, 6, 10, 0x7f, 0xc8, 0});
  expect(queued.tracks[0].automations.size() == 2 &&
             queued.tracks[0].automations[0].realization.endReason == PerformanceAutomationEndReason::Continued &&
             queued.tracks[0].automations[0].realization.endTick ==
                 queued.tracks[0].automations[0].realization.startTick &&
             queued.tracks[0].automations[1].realization.startTick == 7,
         "queued F9 replacement should retain the source driver's execution timing");
  const MidiSequence queuedPortamento =
      renderMidiSequence(queued, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(std::ranges::count_if(
             queuedPortamento.tracks[0].events,
             [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) == 1,
         "a canceled delayed F9 should not leave a zero-length portamento behind");

  for (const ProfileId id : kProfileIds) {
    std::vector<u8> bytes(kAramSize);
    writeLe16(bytes, 0x100, 0x200);
    writeLe16(bytes, 0x102, 0);
    writeSection(bytes, 0x200, {{0, 0x300}});
    const u8 slide = pitchSlideOpcode(profile(id));
    std::ranges::copy(std::initializer_list<u8>{12, 0x7f, 0x80, slide, 0, 3, 1, 0}, bytes.begin() + 0x300);

    Layout layout = standardLayout();
    layout.profile = id;
    if (profile(id).base == BaseProfile::Intelli) {
      layout.signature = Signature::Intelligent;
    } else if (profile(id).base == BaseProfile::Earlier) {
      layout.signature = Signature::Earlier;
    }
    const PerformanceSequence variant = render(std::move(bytes), layout);
    expect(std::ranges::any_of(
               variant.tracks[0].automations,
               [](const PerformanceAutomation& candidate) { return pitchTransitionIntent(candidate) != nullptr; }),
           std::string(profile(id).name) + " should use the shared pitch-transition path");
  }
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
