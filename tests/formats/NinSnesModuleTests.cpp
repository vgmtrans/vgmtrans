/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnes.h"

#include "value/formats/NinSnes/NinSnesPatterns.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SnesDsp.h"

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

ScanResult scanSynth(std::vector<u8> bytes, const Layout& layout, std::string_view name,
                     const SequenceRecipes& recipes = {}) {
  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = std::string(name) + ".spc"}, std::move(bytes));
  ScanIdAllocator ids;
  ScanResultBuilder result(
      ScanInput{
          .source = sources.source(source),
          .reader = sources.reader(source),
          .ids = ids,
      },
      "NinSnes");
  expect(addSynth(result, layout, recipes, name).has_value(), "NinSnes synth fixture should produce assets");
  return result.finish();
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

void ninSnesKonamiClockControlsTempo() {
  std::vector<u8> direct(32);
  std::ranges::copy(
      std::initializer_list<u8>{0xe8, 0xf0, 0xc4, 0xf1, 0xe8, 0x40, 0xc4, 0xfa, 0xe8, 0x01, 0xc4, 0xf1},
      direct.begin());
  std::vector<u8> absolute(32);
  std::ranges::copy(std::initializer_list<u8>{0xe8, 0xf0, 0xc5, 0xf1, 0x00, 0xe8, 0x20, 0xc5, 0xfa, 0x00, 0xe8,
                                              0x01, 0xc5, 0xf1, 0x00},
                    absolute.begin());
  expect(detectKonamiTempoTimerTarget(ByteReader(SourceId{1}, direct)) == 0x40 &&
             detectKonamiTempoTimerTarget(ByteReader(SourceId{1}, absolute)) == 0x20,
         "Konami timer detection should cover the Parodius and Gradius register-write forms");

  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});
  std::ranges::copy(std::initializer_list<u8>{0xe7, 0x40, 4, 0x7f, 0xc9, 0}, bytes.begin() + 0x300);

  Layout layout = standardLayout();
  layout.profile = ProfileId::Konami;
  layout.tempoTimerTarget = 0x40;
  const PerformanceSequence performance = render(std::move(bytes), layout);
  const auto tempo = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<TempoPerformanceEvent>(event);
  });
  expect(profile(ProfileId::Konami).tempoCommandMultiplier == 2 &&
             performance.initialTempoMicrosecondsPerQuarter == 3'072'000 &&
             tempo != performance.tracks[0].events.end() &&
             std::get<TempoPerformanceEvent>(*tempo).microsecondsPerQuarter == 768'000,
         "Konami's tempo command doubling should partially offset its slower timer");
}

void ninSnesScannerFindsRequestedSongAcrossSparseTable() {
  std::vector<u8> bytes(kAramSize);

  // Earlier driver signatures and its command tables. Pilotwings uses this
  // profile while retaining the later five-bit song request protocol.
  std::ranges::copy(
      std::initializer_list<u8>{0x8d, 0x00, 0xf7, 0x40, 0x3a, 0x40, 0x2d, 0xf7, 0x40, 0x3a, 0x40, 0xfd, 0xae},
      bytes.begin() + 0x500);
  std::ranges::copy(std::initializer_list<u8>{0xf5, 0x01, 0x20, 0xfd, 0xf5, 0x00, 0x20, 0xda, 0x40},
                    bytes.begin() + 0x520);
  std::ranges::copy(std::initializer_list<u8>{0x68, 0xe0, 0x90, 0x05, 0x3f, 0x00, 0x00, 0x2f, 0x00},
                    bytes.begin() + 0x540);
  std::ranges::copy(std::initializer_list<u8>{0x1c, 0x5d, 0xe8, 0x00, 0x1f, 0x76, 0x0f}, bytes.begin() + 0x560);
  std::ranges::copy(std::initializer_list<u8>{0xe4, 0x00, 0x68, 0xff, 0xf0, 0xe8, 0x28, 0x1f, 0xd0, 0x8f},
                    bytes.begin() + 0x580);
  std::ranges::copy(
      std::initializer_list<u8>{0x68, 0xda, 0x90, 0x0a, 0x6d, 0xfd, 0xae, 0x60, 0x96, 0x56, 0x0f, 0xfd, 0x2f, 0xe3},
      bytes.begin() + 0x5a0);
  std::ranges::copy(
      std::initializer_list<u8>{0x80, 0xa8, 0xd0, 0x8d, 0x06, 0x8f, 0x00, 0x14, 0x8f, 0x30, 0x15, 0x3f, 0x56, 0x0d},
      bytes.begin() + 0x5c0);
  // Song 2 is an unloaded hole between two valid resident playlists.
  writeLe16(bytes, 0x2002, 0x2100);
  writeLe16(bytes, 0x2004, 0x2200);
  writeLe16(bytes, 0x2006, 0x2300);
  writeLe16(bytes, 0x40, 0x2102);
  bytes[0xf4] = 3;

  writeLe16(bytes, 0x2100, 0x2400);
  writeLe16(bytes, 0x2102, 0xff);
  writeLe16(bytes, 0x2104, 0x2100);
  writeLe16(bytes, 0x2106, 0xffff);
  writeSection(bytes, 0x2400, {{0, 0x2500}});
  bytes[0x2500] = 0;

  writeLe16(bytes, 0x2200, 0x2600);
  writeLe16(bytes, 0x2202, 0);

  writeLe16(bytes, 0x2300, 0x2700);
  writeLe16(bytes, 0x2302, 0);
  writeSection(bytes, 0x2700, {{0, 0x2800}});
  bytes[0x2800] = 0;

  writeLe16(bytes, 0x2040, 0x2900);
  writeLe16(bytes, 0x2900, 0x2a00);
  writeLe16(bytes, 0x2902, 0);
  writeSection(bytes, 0x2a00, {{0, 0x2b00}});
  bytes[0x2b00] = 0;

  const auto requested = findLayout(ByteReader(SourceId{7}, bytes));
  expect(requested && requested->profile == ProfileId::Earlier && requested->songIndex == 3 &&
             requested->playlistAddress == 0x2300 && requested->percussionTableAddress == 0x3000,
         "a pending N-SPC request should select a valid song beyond an unloaded table hole");

  bytes[0xf4] = 0;
  writeLe16(bytes, 0x40, 0x2302);
  const auto playing = findLayout(ByteReader(SourceId{7}, bytes));
  expect(playing && playing->songIndex == 3 && playing->playlistAddress == 0x2300,
         "the live playlist cursor should select the playing song when no request is pending");

  writeLe16(bytes, 0x40, 0x2902);
  const auto later = findLayout(ByteReader(SourceId{7}, bytes));
  expect(later && later->songIndex == 32 && later->playlistAddress == 0x2900,
         "the live playlist cursor should select songs beyond the five-bit request range");

  expect(
      isValidPlaylist(ByteReader(SourceId{7}, bytes), Layout{.profile = ProfileId::Earlier, .playlistAddress = 0x2100}),
      "an infinite repeat should not make adjacent data part of the playlist");
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

void ninSnesStandardEchoUsesMaskLevelAndDisable() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}, {1, 0x320}});

  std::ranges::copy(
      std::initializer_list<u8>{
          0xf5, 0x03, 0x40, 0x20,  // EON 0/1, EVOL +64/+32
          0xf7, 0x03, 0xc0, 0x02,  // 48 ms, EFB -0.5, FIR 2
          0xf8, 0x04, 0x00, 0xe0,  // fade EVOL to 0/-32
          4,    0xc9, 0xf6,        // finish the fade, then disable echo
          4,    0xc9, 0,
      },
      bytes.begin() + 0x300);
  std::ranges::copy(std::initializer_list<u8>{8, 0x7f, 0x80, 0}, bytes.begin() + 0x320);

  const PerformanceSequence performance = render(std::move(bytes));
  const MidiSequence midi = renderMidiSequence(performance);
  expect(performance.tracks.size() == 8, "standard echo fixture should retain all eight DSP voices");
  for (size_t index = 0; index < midi.tracks.size(); ++index) {
    const bool enabled = std::ranges::any_of(midi.tracks[index].events, [](const MidiEvent& event) {
      const auto* reverb = std::get_if<Reverb>(&event);
      return reverb != nullptr && reverb->tick == 0 && reverb->value != 0;
    });
    const bool disabled = std::ranges::any_of(midi.tracks[index].events, [](const MidiEvent& event) {
      const auto* reverb = std::get_if<Reverb>(&event);
      return reverb != nullptr && reverb->tick == 4 && reverb->value == 0;
    });
    expect(enabled == (index < 2) && (index >= 2 || disabled),
           "standard echo should apply EON globally and honor echo-off on track " + std::to_string(index));
  }

  std::vector<const ReverbPerformanceEvent*> changes;
  for (const PerformanceEvent& event : performance.tracks[0].events) {
    if (const auto* reverb = std::get_if<ReverbPerformanceEvent>(&event);
        reverb != nullptr && reverb->leftGain && reverb->rightGain) {
      changes.push_back(reverb);
    }
  }

  expect(changes.size() >= 6, "standard N-SPC echo should retain setup, parameters, and fade samples");
  expect(changes[1]->delayMilliseconds && *changes[1]->delayMilliseconds == 48.0 && changes[1]->feedback &&
             std::abs(*changes[1]->feedback + 0.5) < 0.0001 && changes[1]->filterIndex == 2,
         "standard N-SPC F7 should preserve EDL, signed EFB, and FIR-table state");
  const bool reachedFadeTarget = std::ranges::any_of(changes, [](const ReverbPerformanceEvent* change) {
    return std::abs(*change->leftGain) < 0.0001 && std::abs(*change->rightGain + (32.0 / 127.0)) < 0.0001 &&
           std::abs(change->send - (32.0 / 127.0)) < 0.0001;
  });
  expect(reachedFadeTarget,
         "standard N-SPC F8 should reach its signed stereo target over the requested duration");
}

void ninSnesKonamiLoopAppliesAndClearsReplayDeltas() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});

  std::ranges::copy(
      std::initializer_list<u8>{
          0xe5,  // loop start
          4,
          0x7f,
          0x80,  // packed parameters and C note
          0xe6,
          3,
          0xf6,
          0x01,  // three plays; -10 velocity and +1/16 semitone per replay
          4,
          0x7f,
          0x80,  // loop deltas must be clear after the final pass
          0,
      },
      bytes.begin() + 0x300);

  Layout layout = standardLayout();
  layout.profile = ProfileId::Konami;
  layout.volumeTable.assign(16, 0);
  layout.volumeTable[15] = 100;
  const PerformanceSequence performance = render(std::move(bytes), layout);

  std::vector<const NotePerformanceEvent*> notes;
  for (const PerformanceEvent& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 4, "Konami N-SPC loop should play three passes plus the following note");
  constexpr std::array expectedVelocities{100, 90, 80, 100};
  constexpr std::array expectedKeys{24.0, 24.0625, 24.125, 24.0};
  for (size_t index = 0; index < notes.size(); ++index) {
    expect(std::abs(notes[index]->linearVelocity - ninSnesLevelGain(expectedVelocities[index])) < 0.0001,
           "Konami N-SPC loop should apply its accumulated per-note volume delta");
    expect(std::abs(notes[index]->key - expectedKeys[index]) < 0.0001,
           "Konami N-SPC loop should apply its accumulated 1/16-semitone pitch delta");
  }
}

void ninSnesKonamiAdsrGainEmitsNeutralEnvelopeState() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});

  constexpr u8 attackDecay = 0x96;
  constexpr u8 sustain = 0xd2;
  constexpr u8 gain = 0;
  constexpr u8 gainMode = 0xa0;
  constexpr u8 directGain = 0x7f;
  std::ranges::copy(
      std::initializer_list<u8>{
          0xfb,
          attackDecay,
          sustain,
          gain,
          0xfb,
          gainMode,
          sustain,
          directGain,
          0xe0,
          1,
          4,
          0x7f,
          0x80,
          0,
      },
      bytes.begin() + 0x300);

  Layout layout = standardLayout();
  layout.profile = ProfileId::Konami;
  const PerformanceSequence performance = render(std::move(bytes), layout);
  std::vector<const EnvelopePerformanceEvent*> envelopes;
  std::vector<const InstrumentPerformanceEvent*> instruments;
  for (const PerformanceEvent& event : performance.tracks[0].events) {
    if (const auto* envelope = std::get_if<EnvelopePerformanceEvent>(&event)) {
      envelopes.push_back(envelope);
    } else if (const auto* instrument = std::get_if<InstrumentPerformanceEvent>(&event)) {
      instruments.push_back(instrument);
    }
  }
  expect(envelopes.size() == 2, "Konami FB commands should emit two envelope events");
  expect(envelopes[0]->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             envelopes[0]->update.fields == EnvelopeFields::All &&
             envelopes[0]->update.values == snesDspEnvelope(0x8f, 0xe2, gain),
         "Konami FB should expand its packed attack, decay, and sustain parameters to DSP ADSR values");
  expect(envelopes[1]->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             envelopes[1]->update.fields == EnvelopeFields::All &&
             envelopes[1]->update.values == snesDspEnvelope(0, 0xe2, directGain),
         "Konami FB attack/decay parameters at or above A0 should select direct GAIN mode");
  expect(!instruments.empty() && instruments.back()->envelopeMode == InstrumentEnvelopeMode::UseInstrumentEnvelope,
         "a later Konami program change should select the instrument's native envelope");
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

void ninSnesFe3ConditionalJumpUsesCapturedDriverState() {
  const auto endTick = [](u8 mask) {
    std::vector<u8> bytes(kAramSize);
    writeLe16(bytes, 0x100, 0x200);
    writeLe16(bytes, 0x102, 0);
    writeSection(bytes, 0x200, {{0, 0x300}});
    std::ranges::copy(std::initializer_list<u8>{0xf7, 3, 3, 0xc9, 0, 1, 0xc9, 0}, bytes.begin() + 0x300);
    bytes[0xb9] = mask;
    Layout layout = standardLayout();
    layout.signature = Signature::Intelligent;
    layout.profile = ProfileId::IntelliFe3;
    return render(std::move(bytes), layout).tracks[0].endTick;
  };

  expect(endTick(0) == 1 && endTick(1) == 3,
         "FE3 F7 should branch only when the captured per-channel driver condition bit is clear");
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
    if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event);
        balance != nullptr && balance->header.sourceCommand.valid()) {
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

void ninSnesPrepassClearsMasterVolumeAutomationBinding() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});

  // The scheduled recipe prepass leaves the completed fade bound to its
  // discarded performance track. The real render must start with fresh
  // performance bindings while retaining the reset source-domain value.
  std::ranges::copy(
      std::initializer_list<u8>{
          0xe5, 0xff,        // master volume
          0xe6, 4, 0x80,    // master volume fade
          4,    0xc9, 0,    // wait for the fade, then end
      },
      bytes.begin() + 0x300);

  const PerformanceSequence performance = render(std::move(bytes));
  expect(performance.tracks[0].automations.size() == 1,
         "the real render should replace the discarded prepass master-volume binding");
  const auto* automation =
      std::get_if<ScalarPerformanceAutomationIntent>(&performance.tracks[0].automations[0].intent);
  expect(automation != nullptr && automation->target == PerformanceAutomationTarget::MasterLevel &&
             automation->durationTicks == 4,
         "the real render should retain the structured master-volume fade");
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

void ninSnesKonamiZeroDurationRateContinuesHeldVoice() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0x220);
  writeLe16(bytes, 0x104, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});
  writeSection(bytes, 0x220, {{0, 0x320}});

  // A normal note precedes the zero-rate run. The first zero-rate note still
  // attacks, then leaves the driver's voice-hold bit set for the next section.
  std::ranges::copy(std::initializer_list<u8>{4, 0x6f, 0x80, 4, 0x0f, 0x84, 0}, bytes.begin() + 0x300);

  // The next note changes pitch without a new attack. A zero-rate rest keeps
  // that voice sounding, and the first later gated note is still part of the
  // held voice before it clears the hold bit.
  std::ranges::copy(std::initializer_list<u8>{4, 0x0f, 0x87, 0xc9, 4, 0x6f, 0x89, 0}, bytes.begin() + 0x320);

  Layout layout = standardLayout();
  layout.profile = ProfileId::Konami;
  layout.durationRateTable = {0x00, 0xe6, 0xf0, 0xf5, 0xfa, 0xfc, 0xfe, 0xff};
  const PerformanceSequence performance = render(bytes, layout);
  expect(performance.diagnostics.empty(), "Konami zero-rate hold fixture should render without diagnostics");

  std::vector<const NotePerformanceEvent*> notes;
  std::vector<const PitchTransitionIntent*> transitions;
  std::vector<const LegatoPedalPerformanceEvent*> pedals;
  for (const PerformanceEvent& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    } else if (const auto* pedal = std::get_if<LegatoPedalPerformanceEvent>(&event)) {
      pedals.push_back(pedal);
    }
  }
  for (const PerformanceAutomation& automation : performance.tracks[0].automations) {
    if (const auto* transition = pitchTransitionIntent(automation)) {
      transitions.push_back(transition);
    }
  }

  expect(notes.size() == 4 && notes[0]->header.tick == 0 && notes[0]->durationTicks == 2 &&
             notes[1]->header.tick == 4 && notes[1]->durationTicks == 4 && notes[2]->header.tick == 8 &&
             notes[2]->durationTicks == 8 && notes[3]->header.tick == 16 && notes[3]->durationTicks == 2,
         "zero-rate notes and rests should preserve the driver's full held-voice timeline");
  expect(transitions.size() == 2 && transitions[0]->previousNote == std::optional{notes[1]->note} &&
             transitions[0]->note == notes[2]->note && transitions[0]->startKey == 28.0 &&
             transitions[0]->targetKey == 31.0 && transitions[0]->timing.timelineTicks == 0 &&
             transitions[1]->previousNote == std::optional{notes[2]->note} && transitions[1]->note == notes[3]->note &&
             transitions[1]->startKey == 31.0 && transitions[1]->targetKey == 33.0 &&
             transitions[1]->timing.timelineTicks == 0,
         "middle and terminal notes should continue the held voice with instant pitch changes");
  expect(pedals.size() == 2 && pedals[0]->header.tick == 4 && pedals[0]->enabled && pedals[1]->header.tick == 16 &&
             !pedals[1]->enabled,
         "CC68 intent should bracket the exact zero-rate held-note run");

  const MidiSequence midi = renderMidiSequence(performance);
  std::vector<NoteDuration> attacks;
  for (const MidiEvent& event : midi.tracks[0].events) {
    if (const auto* note = std::get_if<NoteDuration>(&event)) {
      attacks.push_back(*note);
    }
  }
  expect(attacks.size() == 2 && attacks[0].tick == 0 && attacks[0].key == 24 && attacks[0].duration == 2 &&
             attacks[1].tick == 4 && attacks[1].key == 28 && attacks[1].duration == 14,
         "pitch-bend lowering should render the zero-rate run as one sustained physical attack");

  Layout standard = standardLayout();
  standard.durationRateTable = layout.durationRateTable;
  const PerformanceSequence standardPerformance = render(std::move(bytes), standard);
  std::vector<u32> standardDurations;
  bool standardHasHold = false;
  for (const PerformanceEvent& event : standardPerformance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      standardDurations.push_back(note->durationTicks);
    }
    standardHasHold |= std::holds_alternative<LegatoPedalPerformanceEvent>(event);
  }
  const bool standardHasContinuation =
      std::ranges::any_of(standardPerformance.tracks[0].automations, [](const PerformanceAutomation& automation) {
        return pitchTransitionIntent(automation) != nullptr;
      });
  expect(standardDurations == std::vector<u32>{2, 1, 1, 2} && !standardHasHold && !standardHasContinuation,
         "zero duration rate should retain ordinary one-tick gates outside the Konami driver");
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

void ninSnesFixedPercussionBaseIgnoresFaOperand() {
  std::vector<u8> driverBytes(0x100);
  std::ranges::copy(std::initializer_list<u8>{0xd5, 0x11, 0x02, 0xfd, 0x10, 0x03, 0x80, 0xa8, 0xca, 0x8d, 0x06, 0xcf},
                    driverBytes.begin() + 0x20);
  expect(detectFixedPercussionBase(ByteReader(SourceId{7}, driverBytes), 0xca) == 0,
         "the Vegas Stakes loader form should detect fixed percussion base zero");

  std::ranges::fill(driverBytes, 0);
  std::ranges::copy(std::initializer_list<u8>{0x68, 0xca, 0x90, 0x07, 0xa8, 0xa7, 0x3f, 0x11, 0x0b, 0x8d, 0xa4},
                    driverBytes.begin() + 0x20);
  expect(detectFixedPercussionBase(ByteReader(SourceId{7}, driverBytes), 0xca) == 0x23,
         "the Kirby's Dream Land 3 dispatch form should expose its hard-coded percussion base");

  std::ranges::fill(driverBytes, 0);
  std::ranges::copy(std::initializer_list<u8>{0xd5, 0x11, 0x02, 0xfd, 0x10, 0x06, 0x80, 0xa8, 0xca, 0x60, 0x84, 0x5f,
                                              0x8d, 0x06, 0xcf},
                    driverBytes.begin() + 0x20);
  expect(!detectFixedPercussionBase(ByteReader(SourceId{7}, driverBytes), 0xca),
         "the normal FA-controlled loader should remain dynamic");

  std::vector<u8> sequenceBytes(kAramSize);
  writeLe16(sequenceBytes, 0x100, 0x200);
  writeLe16(sequenceBytes, 0x102, 0);
  writeSection(sequenceBytes, 0x200, {{0, 0x300}});
  std::ranges::copy(std::initializer_list<u8>{3, 0x7f, 0xfa, 5, 0xca, 0}, sequenceBytes.begin() + 0x300);

  const auto sourceProgram = [&](std::optional<u8> fixedBase) {
    Layout layout = standardLayout();
    layout.fixedPercussionBase = fixedBase;
    const SequenceParse parsed = decodeSequence(ByteReader(SourceId{7}, sequenceBytes), layout, AssetId{1});
    const auto command = std::ranges::find(parsed.program.tracks[0].commands, u8{0xfa}, &SourceCommand::opcode);
    expect(command != parsed.program.tracks[0].commands.end() && command->encodedSize == 2,
           "FA should still consume its operand in fixed-base drivers");
    expect(parsed.recipes.drumKits.size() == 1 && parsed.recipes.drumKits[0].slots.size() == 1,
           "the percussion fixture should produce one drum mapping");
    return parsed.recipes.drumKits[0].slots[0].sourceProgram;
  };

  expect(sourceProgram(std::nullopt) == 5, "normal drivers should continue applying the FA percussion base");
  expect(sourceProgram(0) == 0, "fixed-base drivers should ignore FA while retaining the detected base");
  expect(sourceProgram(0x23) == 0x23, "a nonzero fixed percussion base should override the FA operand");
}

void ninSnesKonamiPercussionUsesDriverMapAndNeutralTuning() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});

  // Konami FA consumes no operands. The following 04 7F is a normal note
  // parameter command, followed by percussion slot CA.
  std::ranges::copy(std::initializer_list<u8>{0xfa, 4, 0x7f, 0xca, 0}, bytes.begin() + 0x300);
  bytes[0x3702] = 0xb0;

  // Program 20 deliberately uses SRCN 1. Tuning must be selected by program
  // for melodic playback, while the percussion branch clears it entirely.
  std::ranges::copy(std::initializer_list<u8>{1, 0xff, 0xe0, 0, 0, 0}, bytes.begin() + 0x4000 + 20 * 6);
  bytes[0x3800 + 1] = 0xf9;
  bytes[0x3800 + 20] = 5;
  writeLe16(bytes, 0x5000 + 4, 0x6000);
  writeLe16(bytes, 0x5000 + 6, 0x6000);
  bytes[0x6000] = 0x01;

  Layout layout = standardLayout();
  layout.profile = ProfileId::Konami;
  layout.fixedPercussionBase = 20;
  layout.konamiPercussion = KonamiPercussionLayout{
      .tableAddress = 0x3700,
      .slotCount = 1,
      .programBase = 20,
  };
  layout.konamiTuningTableAddress = 0x3800;
  layout.konamiTuningTableSize = 21;
  layout.instrumentTableAddress = 0x4000;
  layout.spcDirAddress = 0x5000;

  const SequenceParse parsed = decodeSequence(ByteReader(SourceId{7}, bytes), layout, AssetId{1});
  const auto fa = std::ranges::find(parsed.program.tracks[0].commands, u8{0xfa}, &SourceCommand::opcode);
  const auto parameters = std::ranges::find(parsed.program.tracks[0].commands, u8{4}, &SourceCommand::opcode);
  expect(fa != parsed.program.tracks[0].commands.end() && fa->encodedSize == 1 &&
             parameters != parsed.program.tracks[0].commands.end() && parameters->address.value == 0x301,
         "Konami FA should remain a zero-operand NOP without swallowing the following note parameters");
  expect(parsed.recipes.drumKits.size() == 1 && parsed.recipes.drumKits[0].slots.size() == 1 &&
             parsed.recipes.drumKits[0].slots[0] == DrumSlot{.key = 36, .sourceProgram = 20, .sourceKey = 72},
         "Konami percussion should use its fixed program base and per-slot played note");

  const ScanResult scan = scanSynth(std::move(bytes), layout, "Konami percussion", parsed.recipes);
  const auto* instruments = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  expect(instruments != nullptr, "Konami synth fixture should produce an instrument set");
  const auto melodic = std::ranges::find_if(instruments->instruments, [](const Instrument& instrument) {
    return instrument.explicitAddress == InstrumentAddress{.bank = 0, .program = 20};
  });
  const auto drums = std::ranges::find_if(instruments->instruments, [](const Instrument& instrument) {
    return instrument.explicitAddress == InstrumentAddress{.bank = 0x7f, .program = 0};
  });
  expect(melodic != instruments->instruments.end() && melodic->regions.size() == 1 &&
             std::abs(melodic->regions[0].unityKey - 66.21) < 0.001,
         "Konami melodic tuning should be indexed by program rather than SRCN");
  expect(drums != instruments->instruments.end() && drums->regions.size() == 1 &&
             std::abs(drums->regions[0].unityKey - 35.21) < 0.001,
         "Konami drums should combine the table note with neutralized melodic tuning");
}

void ninSnesEarlierPercussionUsesSeparateSixByteTable() {
  std::vector<u8> bytes(kAramSize);
  writeLe16(bytes, 0x100, 0x200);
  writeLe16(bytes, 0x102, 0);
  writeSection(bytes, 0x200, {{0, 0x300}});
  std::ranges::copy(std::initializer_list<u8>{3, 0x7f, 0xd0, 0}, bytes.begin() + 0x300);

  // Prototype melodic instruments have five-byte rows. Percussion lives in a
  // separate six-byte table whose final byte is the note used to pitch the hit.
  std::ranges::copy(std::initializer_list<u8>{0, 0x8f, 0xe0, 0, 1}, bytes.begin() + 0x4000);
  std::ranges::copy(std::initializer_list<u8>{1, 0x8f, 0xe0, 0, 1, 0xa8}, bytes.begin() + 0x4005);
  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  writeLe16(bytes, 0x5004, 0x6009);
  writeLe16(bytes, 0x5006, 0x6009);
  bytes[0x6000] = 0x01;
  bytes[0x6009] = 0x01;

  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "earlier-percussion.spc"}, std::move(bytes));
  Layout layout{
      .signature = Signature::Earlier,
      .profile = ProfileId::Earlier,
      .playlistAddress = 0x100,
      .instrumentTableAddress = 0x4000,
      .percussionTableAddress = 0x4005,
      .spcDirAddress = 0x5000,
  };
  const SequenceParse parsed = decodeSequence(sources.reader(source), layout, AssetId{1});
  expect(parsed.recipes.drumKits.size() == 1 && parsed.recipes.drumKits[0].slots.size() == 1 &&
             parsed.recipes.drumKits[0].slots[0].sourceProgram == kEarlierPercussionProgramBase &&
             parsed.recipes.drumKits[0].slots[0].sourceKey == 0x40,
         "prototype percussion should resolve its separate row and source pitch");

  ScanIdAllocator ids;
  ScanResultBuilder result(
      ScanInput{
          .source = sources.source(source),
          .reader = sources.reader(source),
          .ids = ids,
      },
      "NinSnes");
  const auto synth = addSynth(result, layout, parsed.recipes, "Earlier");
  expect(synth.has_value(), "prototype percussion should produce an exportable drum kit");

  const ScanResult scan = result.finish();
  const auto* instruments = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  expect(instruments != nullptr && instruments->instruments.size() == 2,
         "the separate percussion row should remain an internal drum source");
  const auto melodic = std::ranges::find_if(instruments->instruments, [](const Instrument& instrument) {
    return instrument.explicitAddress == InstrumentAddress{.bank = 0, .program = 0};
  });
  const auto drum = std::ranges::find_if(instruments->instruments, [](const Instrument& instrument) {
    return instrument.explicitAddress == InstrumentAddress{.bank = 0x7f, .program = 0};
  });
  expect(melodic != instruments->instruments.end() && drum != instruments->instruments.end() &&
             melodic->regions.size() == 1 && drum->regions.size() == 1 && drum->regions[0].keyRange.low == 0x24 &&
             drum->regions[0].keyRange.high == 0x24 &&
             drum->regions[0].sample.index != melodic->regions[0].sample.index,
         "the drum kit should use the percussion sample on its MIDI drum key");
  expect(std::abs(drum->regions[0].unityKey - (melodic->regions[0].unityKey - 28.0)) < 0.0001,
         "the percussion row's sixth byte should determine the exported drum pitch");

  const auto drumSource = std::ranges::find_if(scan.sourceMap.annotations(), [](const SourceAnnotation& annotation) {
    return annotation.localKind == "nin-snes-drum-region";
  });
  expect(drumSource != scan.sourceMap.annotations().end() && drumSource->fieldsAsChildren,
         "NinSnes drum records should opt their exact fields into TreeView child projection");
  const auto sourceBackedFields = std::ranges::count_if(
      drumSource->fields, [](const SourceField& field) { return field.range.valid(); });
  expect(sourceBackedFields == 6 && drumSource->fields[0].name == "srcn" &&
             drumSource->fields[5].name == "note" && drumSource->fields[5].range.offset == 0x400a,
         "a prototype drum record should retain all six individually selectable source fields");
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

  Layout layout = standardLayout();
  layout.instrumentTableAddress = 0x4000;
  layout.spcDirAddress = 0x5000;

  const ScanResult scan = scanSynth(std::move(bytes), layout, "GAIN");
  const auto* instruments = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  expect(
      instruments != nullptr && instruments->instruments.size() == 1 && instruments->instruments[0].regions.size() == 1,
      "direct-GAIN fixture should produce one instrument region");
  const Envelope& envelope = instruments->instruments[0].regions[0].envelope;
  expect(envelope.sustainAmplitude && *envelope.sustainAmplitude > 0.99 && *envelope.sustainAmplitude < 1.0,
         "direct GAIN should become the DSP's fixed sustain level instead of an unspecified envelope");
}

void ninSnesIdentityMappedSilentSlotsAreSparse() {
  std::vector<u8> bytes(kAramSize);
  constexpr u32 kInstrumentTable = 0x4000;
  constexpr u16 kDirectory = 0x5000;
  constexpr u16 kSample = 0x6000;

  std::ranges::copy(
      std::initializer_list<u8>{
          0, 0xff, 0xe0, 0, 1, 0,  // valid
          1, 0,    0,    0, 0, 0,  // identity-mapped silent
          2, 0xff, 0xe0, 0, 1, 0,  // valid after the sparse slot
          3, 0xff, 0xe0, 0, 1, 0,  // valid-looking start of the next structure
      },
      bytes.begin() + kInstrumentTable);
  for (const u8 srcn : {u8{0}, u8{1}, u8{2}, u8{3}}) {
    writeLe16(bytes, kDirectory + srcn * 4, kSample);
    writeLe16(bytes, kDirectory + srcn * 4 + 2, kSample);
  }
  bytes[kSample] = 0x01;

  Layout layout = standardLayout();
  layout.songListAddress = kInstrumentTable + 3 * 6;
  layout.instrumentTableAddress = kInstrumentTable;
  layout.spcDirAddress = kDirectory;

  const ScanResult scan = scanSynth(std::move(bytes), layout, "Sparse");
  const auto* instruments = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  expect(instruments != nullptr && instruments->instruments.size() == 2 &&
             instruments->instruments[0].explicitAddress == InstrumentAddress{.bank = 0, .program = 0} &&
             instruments->instruments[1].explicitAddress == InstrumentAddress{.bank = 0, .program = 2},
         "identity-mapped silent slots should be skipped without scanning into the following known structure");
}
