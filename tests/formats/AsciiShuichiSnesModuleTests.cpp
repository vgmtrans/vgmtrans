/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AsciiShuichiSnes/AsciiShuichiSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::ascii_shuichi_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Event>
std::vector<const Event*> events(const PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const PerformanceEvent& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

class DriverFixture {
public:
  explicit DriverFixture(Version version) : data_(kAramSize), version_(version) {
    write(0x0100, {0xe8, 0x05, 0x3f, 0, 0, 0xf5, 0x00, 0x12, 0xd5, 0, 0, 0xf5, 0x08, 0x12, 0xd4, 0,
                   0x1d, 0x10, 0xed});
    write(0x0200, {0x80, 0xa8, noteBase(), 0x10, 0x0b, 0x60, 0x88, commandCount(), 0x1c, 0x5d, 0xaa, 0, 0,
                   0x1f, 0x00, 0x11});
    write(0x0400, {0x8d, 0x5d, 0xe8, 0x40, 0x3f, 0, 0});
    if (version == Version::Early) {
      write(0x0300, {0xf8, 0, 0x1c, 0x1c, 0xc4, 0, 0xfd, 0xf7, 0xb0, 0xfd, 0xf5, 0, 0, 0x3f, 0, 0,
                     0xf7, 0xb2, 0xd5, 0, 0});
    } else {
      write(0x0300, {0xf8, 0, 0x1c, 0x1c, 0xc4, 0, 0xfd, 0xf4, 0, 0xc4, 0, 0xf7, 0xb0, 0xeb, 0, 0xdc,
                     0x3f, 0, 0, 0xfd, 0xf7, 0xb2, 0xd5, 0, 0});
    }
    word(0xb0, 0x3000);
    word(0xb2, 0x3100);
    word(0x1100 + (version == Version::Early ? 0x1e : 0x22) * 2, 0x0800);
    write(0x3000, {1, 0xef, 0xa7, 0xba});
    data_[0x3101] = 4;
    word(0x4004, 0x4100);
    word(0x4006, 0x4100);
    data_[0x4100] = 0x01;

    for (u32 track = 0; track < kTrackCount; ++track) {
      const u16 address = static_cast<u16>(0x2000 + track * 0x40);
      data_[0x1200 + track] = static_cast<u8>(address);
      data_[0x1208 + track] = static_cast<u8>(address >> 8);
      data_[address] = 0x80;
    }
  }

  DriverFixture& track(std::initializer_list<u8> bytes) {
    write(0x2000, bytes);
    return *this;
  }

  [[nodiscard]] const std::vector<u8>& data() const { return data_; }

private:
  [[nodiscard]] u8 noteBase() const { return version_ == Version::Early ? 0xa0 : 0xac; }
  [[nodiscard]] u8 commandCount() const { return version_ == Version::Early ? 0x20 : 0x2c; }

  void word(u32 offset, u16 value) {
    data_[offset] = static_cast<u8>(value);
    data_[offset + 1] = static_cast<u8>(value >> 8);
  }

  void write(u32 offset, std::initializer_list<u8> values) {
    std::ranges::copy(values, data_.begin() + offset);
  }

  std::vector<u8> data_;
  Version version_;
};

PerformanceSequence render(const DriverFixture& fixture) {
  const ByteReader reader(SourceId{401}, fixture.data());
  const auto layout = findLayout(reader);
  expect(layout.has_value(), "fixture driver signatures should produce a layout");
  SequenceParse parsed = decodeSequence(reader, *layout, AssetId{401});
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
}

void layoutsRecognizeBothAuditedDrivers() {
  for (const Version version : {Version::Early, Version::Later}) {
    const DriverFixture fixture(version);
    const auto layout = findLayout(ByteReader(SourceId{402}, fixture.data()));
    expect(layout && layout->version == version && layout->sequenceHeaderAddress == 0x1200 &&
               layout->instrumentTableAddress == 0x3000 && layout->tuningTableAddress == 0x3100 &&
               layout->spcDirAddress == 0x4000 && layout->hasEchoFirCommand,
           "layout discovery should recover tables and distinguish the A0 and AC command sets");
  }
}

void laterCommandsKeepAuditedOperandLengthsAndEffects() {
  DriverFixture fixture(Version::Later);
  fixture.track({0x92, 0xc0, 15, 0x8a, 0xb8, 0x9d, 1, 2, 3, 8, 0xac, 8, 0x8f, 1, 4, 0xb1, 0xff,
                 0xb0, 8, 0x98, 0, 0, 1, 0x99, 0, 1, 0xff, 3, 0x9a, 0x40, 0xc0, 0x9b, 5, 0x80, 0,
                 0x9c, 1, 0xa2, 0x7f, 0, 0, 0, 0, 0, 0, 0, 0x9f, 8, 0x80});
  const PerformanceSequence performance = render(fixture);
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto envelopes = events<EnvelopePerformanceEvent>(track);
  const auto modulation = events<ModulationPerformanceEvent>(track);
  const auto reverb = events<ReverbPerformanceEvent>(track);
  expect(performance.diagnostics.empty() && notes.size() == 2 && !track.automations.empty(),
         "three-operand inline pitch slides and ties should decode without desynchronizing the track");
  expect(notes[0]->durationTicks == 8 && notes[1]->durationTicks == 6,
         "slurred and ordinary length-8 notes should sound for 8 and 6 ticks (got " +
             std::to_string(notes[0]->durationTicks) + " and " + std::to_string(notes[1]->durationTicks) + ")");
  const double expectedRelease = snesDspGainEnvelopeSeconds(0xb8, 0x5ff, 0);
  expect(envelopes.size() == 1 && envelopes.front()->update.values &&
             std::abs(*envelopes.front()->update.values->releaseSeconds - expectedRelease) < 0.000001 &&
             modulation.size() >= 2 && modulation.front()->context.cyclesPerTick &&
             std::abs(*modulation.front()->context.cyclesPerTick - 1.0 / 32.0) < 0.000001 &&
             modulation.front()->context.delayTicks == 3,
         "release GAIN and the driver triangle vibrato should emit physical performance state");
  expect(reverb.size() == 5, "initial echo state and all four echo commands should emit reverb state");
  expect(reverb.back()->filterIndex == 0 && reverb.back()->voiceMask == 1,
         "echo voice mask and custom FIR identity should survive conversion");
  expect(events<LevelPerformanceEvent>(track).size() >= 5 && events<StereoBalancePerformanceEvent>(track).size() >= 2,
         "volume and pan fades should execute at their driver tick intervals");
}

void earlyCommandsUseTheirDistinctTable() {
  DriverFixture fixture(Version::Early);
  fixture.track({0x89, 0, 0x8a, 0x18, 0x8f, 0x60, 0x90, 0xd0, 15, 0x98, 1, 2, 3, 8, 0xa0, 8,
                 0x9a, 0, 4, 0xa4, 0x9b, 8, 0x9e, 0x7f, 0, 0, 0, 0, 0, 0, 0, 0x9f});
  const PerformanceSequence performance = render(fixture);
  const PerformanceTrack& track = performance.tracks.front();
  expect(performance.diagnostics.empty() && events<NotePerformanceEvent>(track).size() == 1 &&
             !track.automations.empty(),
         "the early A0-note driver should decode its three-operand slide without losing synchronization");
  expect(events<MasterLevelPerformanceEvent>(track).size() == 2 && events<ReverbPerformanceEvent>(track).size() == 2,
         "the early driver should retain its distinct master-volume and FIR opcodes");
}

void scannerPublishesSequenceAndReferencedSynth() {
  const DriverFixture fixture(Version::Later);
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "AsciiShuichiSnes fixture.aram"}, fixture.data());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1 && snapshot.collections().front().members.sequence &&
             snapshot.collections().front().members.soundBanks.size() == 1,
         "format scanning should publish the sequence and its self-contained BRR instrument bank");
}

}  // namespace

void runAsciiShuichiSnesModuleTests() {
  layoutsRecognizeBothAuditedDrivers();
  laterCommandsKeepAuditedOperandLengthsAndEffects();
  earlyCommandsUseTheirDistinctTable();
  scannerPublishesSequenceAndReferencedSynth();
}
