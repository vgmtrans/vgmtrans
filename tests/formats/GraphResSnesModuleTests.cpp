/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/GraphResSnes/GraphResSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::graph_res_snes;

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
  DriverFixture() : data_(kAramSize) {
    write(0x0600, {0x3f, 0x24, 0x05, 0xe8, 0x00, 0xc4, 0x0d, 0xe8, 0x20, 0xc4, 0x0e, 0xe5, 0x01,
                   0x20, 0x80, 0xa8, 0x18, 0xc4, 0x0f, 0xe5, 0x02, 0x20, 0xa8, 0x00, 0xc4, 0x10});
    write(0x0700, {0x8d, 0x00, 0xf6, 0x01, 0x18, 0x5d, 0xf6, 0x00, 0x18,
                   0x68, 0xff, 0xf0, 0x07, 0x3f, 0x62, 0x0a, 0xfc, 0xfc});
    write(0x0800, {0xe5, 0x00, 0x19, 0xc4, 0x05, 0xe5, 0x01, 0x19, 0xc4, 0x06,
                   0xe5, 0x02, 0x19, 0xc4, 0x07, 0xe5, 0x03, 0x19, 0xc4, 0x08});
    write(0x0900, {0xe8, 0x00, 0xc4, 0xf1, 0xe8, 0x85, 0xc4, 0xfa, 0xe8, 0x01, 0xc4, 0xf1});

    write(0x1800, {0x6c, 0x20, 0x0c, 0x00, 0x1c, 0x00, 0x2c, 0x00, 0x3c, 0x00, 0x0d, 0x60,
                   0x4d, 0x00, 0x5d, 0x40, 0x7d, 0x02, 0x0f, 0xff, 0x1f, 0x08, 0x2f, 0x17,
                   0x3f, 0x24, 0x4f, 0x24, 0x5f, 0x17, 0x6f, 0x08, 0x7f, 0xff, 0xff});
    word(0x1900, 0x1a00);
    word(0x1902, 0x1a20);
    word(0x1904, 0x1b00);
    word(0x1906, 0x1c00);
    for (u8 value = 0; value < 16; ++value) {
      data_[0x1a00 + value] = value;
      data_[0x1a20 + value * 2u] = static_cast<u8>(100 - value * 100 / 15);
      data_[0x1a21 + value * 2u] = 100;
    }
    for (u32 note = 0; note < 48; ++note) {
      const double pitch = 0x4000 * std::exp2((static_cast<int>(note) - 21) / 12.0);
      word(0x1b00 + note * 2u, static_cast<u16>(std::clamp(std::lround(pitch), 1l, 0xffffl)));
    }
    word(0x1c00, 0x1c04);
    word(0x1c02, 0x1c08);
    write(0x1c04, {0xff, 0x00, 0x00, 0x00});
    write(0x1c08, {0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01,
                   0x01, 0x00, 0x00, 0xff, 0xfe, 0x00, 0xf8, 0x00});

    data_[0x2000] = 1;
    word(0x2001, 0x3000);
    for (u32 track = 1; track < kTrackCount; ++track) {
      word(0x2000 + track * 3u + 1, 0x3000);
    }

    word(0x4008, 0x4100);
    word(0x400a, 0x4100);
    write(0x4100, {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    commands({0xff});
  }

  DriverFixture& commands(std::initializer_list<u8> bytes) {
    write(0x2018, bytes);
    return *this;
  }

  [[nodiscard]] const std::vector<u8>& data() const { return data_; }

private:
  void word(u32 address, u16 value) {
    data_[address] = static_cast<u8>(value);
    data_[address + 1] = static_cast<u8>(value >> 8);
  }

  void write(u32 address, std::initializer_list<u8> bytes) {
    std::ranges::copy(bytes, data_.begin() + address);
  }

  std::vector<u8> data_;
};

SequenceParse parse(const DriverFixture& fixture) {
  const ByteReader reader(SourceId{401}, fixture.data());
  const auto layout = findLayout(reader);
  expect(layout.has_value(), "GraphResSnes fixture should be recognized");
  return decodeSequence(RetainedSource::copyOf(reader), *layout, AssetId{401});
}

PerformanceSequence render(const DriverFixture& fixture) {
  SequenceParse parsed = parse(fixture);
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
}

void layoutAndModuleBuildASequenceAndSynth() {
  DriverFixture fixture;
  fixture.commands({0xfc, 0x02, 0x10, 0x04, 0xff});
  const auto layout = findLayout(ByteReader(SourceId{402}, fixture.data()));
  expect(layout && layout->sequenceHeaderAddress == 0x2000 && layout->tracks.size() == 1 &&
             layout->tracks.front().startAddress == 0x2018 && layout->volumeTableAddress == 0x1a00 &&
             layout->panTableAddress == 0x1a20 && layout->pitchTableAddress == 0x1b00 &&
             layout->pitchEnvelopeListAddress == 0x1c00 && layout->pitchEnvelopeCount == 2 &&
             layout->spcDirAddress == 0x4000 && layout->timerTarget == 0x85 &&
             layout->dsp.masterLeft == 0x7f && layout->dsp.masterRight == 0x7f,
         "GraphResSnes should recover its relocated score, driver tables, DIR, and fixed timer");

  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "GraphResSnes fixture.aram"}, fixture.data());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.diagnostics().empty() && snapshot.collections().size() == 1,
         "GraphResSnes scanning should publish one explicit collection without diagnostics");
  const Collection& collection = snapshot.collections().front();
  const auto* bank = snapshot.asset<SoundBankAsset>(collection.members.soundBanks.front());
  expect(bank && bank->instruments.size() == 1 && bank->instruments.front().identity &&
             bank->instruments.front().identity->key == 2 &&
             std::abs(bank->instruments.front().regions.front().unityKey - 57.0) < 0.000001,
         "direct SRCN programs should resolve to BRR instruments at the audited $1000 unity key");
}

void dynamicDriverFeaturesRenderAtTheirTrueTiming() {
  DriverFixture fixture;
  fixture.commands({0xfc, 0x02, 0xf7, 0xe4, 0x9f, 0xfb, 0x01, 0xec, 0x04, 0xfd, 0x08,
                    0xf1, 0x64, 0xf4, 0x05, 0xe6, 0x40, 0xed, 0x6c, 0x00, 0xed, 0x4d,
                    0x01, 0xf3, 0x10, 0x10, 0x08, 0xfe, 0x12, 0x08, 0xef, 0xb0, 0x02,
                    0x10, 0x08, 0xff});
  SequenceParse parsed = parse(fixture);
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto envelopes = events<EnvelopePerformanceEvent>(track);
  const auto bends = events<PitchBendPerformanceEvent>(track);
  const auto reverbs = events<ReverbPerformanceEvent>(track);
  const auto levels = events<LevelPerformanceEvent>(track);
  const auto balances = events<StereoBalancePerformanceEvent>(track);

  expect(performance.diagnostics.empty() && notes.size() == 3 && notes[0]->durationTicks == 8 &&
             notes[1]->header.tick == 8 && notes[1]->durationTicks == 4 && !notes[1]->restartsEnvelope &&
             notes[2]->header.tick == 16,
         "FE ties should continue pitch-changing voices while EC gates untied notes in eighths");
  expect(envelopes.size() == 1 && envelopes.front()->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             envelopes.front()->header.tick == 0,
         "F7 should apply ADSR2/ADSR1 shadows when the following note writes the DSP registers");
  expect(std::ranges::any_of(bends, [](const PitchBendPerformanceEvent* bend) {
           return std::abs(bend->semitones) > 0.05;
         }),
         "FB table vibrato and EF static DSP-pitch offsets should produce exact pitch bends");
  expect(std::ranges::any_of(reverbs, [](const ReverbPerformanceEvent* reverb) {
           return reverb->voiceMask == 1 && reverb->send > 0.0 && reverb->delayMilliseconds == 32.0 &&
                  reverb->feedback == 0.75;
         }),
         "echo DSP writes should preserve EON, EVOL, EDL, feedback, and FIR state");
  expect(performance.initialTempoMicrosecondsPerQuarter == 798000,
         "timer target 85 should produce the audited 798 ms quarter-note tempo");
  expect(std::ranges::any_of(levels, [](const LevelPerformanceEvent* level) {
           return std::abs(level->linearGain - 100.0 / 128.0) < 0.000001;
         }),
         "direct channel volume should remain a linear signed DSP gain");
  expect(balances.size() >= 2, "pan changes should retain the driver's exact stereo balance table");
  expect(parsed.programs.contains(2), "program discovery should retain referenced SRCNs");
}

void nestedRepeatsAndBreaksFollowDriverState() {
  DriverFixture fixture;
  fixture.commands({0xfd, 0x04, 0xec, 0x04, 0xea, 0x00, 0xe9, 0xeb, 0x03, 0xfe, 0xff, 0xff});
  const PerformanceSequence performance = render(fixture);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 3 && notes[0]->header.tick == 0 &&
             notes[1]->header.tick == 4 && notes[2]->header.tick == 8 && notes[0]->durationTicks == 2,
         "EA/EB nesting and E9 last-pass breaks should preserve repeat counts and EC gate timing");
}

void auditedOperandWidthsStayAligned() {
  DriverFixture fixture;
  fixture.commands({0xe5, 0x40, 0xe6, 0x20, 0xef, 0x34, 0x12, 0xfb, 0x01, 0xff});
  const ByteReader reader(SourceId{403}, fixture.data());
  const Layout layout = *findLayout(reader);
  const TrackProgram track = decodeSourceTrack(reader, layout, 0, 0x2018);
  expect(track.commands.size() == 5 && track.commands[0].range.size == 2 && track.commands[1].range.size == 2 &&
             track.commands[2].range.size == 3 && track.commands[3].range.size == 2,
         "E5/E6 one-byte values, EF signed pitch, and FB table selection must use driver-audited widths");
}

void startupShadowsMatchClearedDriverRam() {
  DriverFixture fixture;
  fixture.commands({0x00, 0xff});
  const PerformanceSequence performance = render(fixture);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto masters = events<MasterLevelPerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->durationTicks == 255 &&
             masters.size() == 1 && std::abs(masters.front()->linearGain - 127.0 / 128.0) < 0.000001,
         "cleared FD length should mean 256 ticks while the driver's MVOL shadow starts at $7F");

  DriverFixture staccato;
  staccato.commands({0xec, 0x00, 0x10, 0x08, 0xff});
  const auto staccatoNotes = events<NotePerformanceEvent>(render(staccato).tracks.front());
  expect(staccatoNotes.size() == 1 && staccatoNotes.front()->durationTicks == 1,
         "duration rate zero should key off when the decremented counter falls below its threshold");
}

}  // namespace

void runGraphResSnesModuleTests() {
  layoutAndModuleBuildASequenceAndSynth();
  dynamicDriverFeaturesRenderAtTheirTrueTiming();
  nestedRepeatsAndBreaksFollowDriverState();
  auditedOperandWidthsStayAligned();
  startupShadowsMatchClearedDriverRam();
}
