/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PrismSnes/PrismSnes.h"

#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::prism_snes;

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
  explicit DriverFixture(Version version) : data_(kAramSize) {
    write(0x0800,
          {0xf6, 0x00, 0x23, 0xc4, 0x06, 0xfc, 0xf6, 0x00, 0x23, 0xc4, 0x07, 0x8d, 0x00, 0xf7, 0x06, 0x30, 0xd4});
    write(0x0840, {0x8d, 0x00, 0xf7, 0x26, 0x10, 0x1f, 0x3a, 0x26, 0x68, 0xa0,
                   0x90, 0x08, 0x80, 0xa8, 0xc0, 0x1c, 0x5d, 0x1f, 0x00, 0x10});
    write(0x0880, {0x8d, 0x14, 0xe8, 0x00, 0xda, 0x00, 0x8d, 0x07, 0xcd, 0x7d, 0x3f, 0x00, 0x00});
    write(0x08c0, {0xf8, 0x24, 0xf7, 0x26, 0x3a, 0x26, 0xd5, 0x50, 0x03, 0xfd, 0xf6, 0x00, 0x40, 0xd5,
                   0xb0, 0x03, 0xf6, 0x00, 0x41, 0xd5, 0xc8, 0x03, 0x38, 0x7f, 0x28, 0x8f, 0xff, 0x39});
    write(0x0900, {0xf8, 0x42, 0xf5, 0x00, 0x42, 0x60, 0x84, 0x2a, 0xc4, 0x2a, 0xf5,
                   0x00, 0x43, 0x60, 0x84, 0x29, 0xc4, 0x29, 0x90, 0x02, 0xab, 0x2a});

    for (u32 command = 0; command < 64; ++command) {
      word(0x1000 + command * 2, 0x1300);
    }
    if (version == Version::CosmoGang) {
      for (u32 command = 0; command <= 16; ++command) {
        word(0x1000 + command * 2, 0x1200);
      }
    } else {
      for (u32 command = 0; command <= 4; ++command) {
        word(0x1000 + command * 2, 0x1100);
      }
      word(0x100a, version == Version::DualOrb ? 0x1100 : 0x1110);
      word(0x1020, 0x1200);
    }
    word(0x1022, 0x1220);
    panHandler(0x1200, 0x3000);
    panHandler(0x1220, 0x3020);
    write(0x3000, {0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x5a, 0x64, 0x6e, 0x78, 0x7f,
                   0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f});
    write(0x3020, {0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
                   0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f});

    write(0x1400, {0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xe0, 0x04, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    data_[0x4002] = 0x8f;
    data_[0x4102] = 0xe0;
    data_[0x4202] = 0;
    data_[0x4302] = 0x80;
    word(0x6008, 0x6200);
    word(0x600a, 0x6200);
    data_[0x6200] = 0x01;

    word(0x2300, 0x5000);
    word(0x2302, 0x5100);
    word(0x2304, 0);
    header(0x5000, 0x9000);
    header(0x5100, 0x5200);
    data_[0x9000] = 0xff;
    data_[0x0200] = 0x90;
    wordSplit(0, 0x5200);

    write(0x7000, {0x20, 0xe0, 0x80, 0x00});
    write(0x7100, {0x80, 0xf0, 0x01, 0x02, 0x00, 0x00, 0x00, 0x71});
    write(0x7200, {0xdf, 0x03, 0x8c, 0x10, 0x81, 0x01, 0x80, 0x05, 0xff, 0x02});
    write(0x7210, {0xbf, 0x02, 0xff, 0x00});
    write(0x7220, {0x9c, 0x02, 0xff, 0x00});
    write(0x7300, {0x20, 0xe0, 0x10, 0x02, 0xff, 0x00});
  }

  DriverFixture& commands(std::initializer_list<u8> values) {
    write(0x5200, values);
    return *this;
  }

  DriverFixture& bytes(u16 address, std::initializer_list<u8> values) {
    write(address, values);
    return *this;
  }

  DriverFixture& pointer(u16 address, u16 value) {
    word(address, value);
    return *this;
  }

  DriverFixture& instrumentEnvelope(u8 program, u8 adsr1, u8 adsr2) {
    data_[0x4000 + program] = adsr1;
    data_[0x4100 + program] = adsr2;
    return *this;
  }

  [[nodiscard]] const std::vector<u8>& data() const { return data_; }

private:
  void header(u16 address, u16 track) {
    write(address, {0x00, 0x10, static_cast<u8>(track), static_cast<u8>(track >> 8), 0xff});
  }

  void panHandler(u16 address, u16 table) {
    write(address, {0xf8, 0x24, 0xe8, static_cast<u8>(table), 0xd5, 0xf0, 0x05, 0xe8, static_cast<u8>(table >> 8), 0xd5,
                    0x08, 0x06});
  }

  void word(u32 offset, u16 value) {
    data_[offset] = static_cast<u8>(value);
    data_[offset + 1] = static_cast<u8>(value >> 8);
  }

  void wordSplit(u8 logical, u16 value) {
    data_[0x0230 + logical] = static_cast<u8>(value);
    data_[0x0248 + logical] = static_cast<u8>(value >> 8);
  }

  void write(u32 offset, std::initializer_list<u8> values) { std::ranges::copy(values, data_.begin() + offset); }

  std::vector<u8> data_;
};

void layoutProfilesAndLiveSongAreAudited() {
  for (const Version version : {Version::CosmoGang, Version::DualOrb, Version::Modern}) {
    DriverFixture fixture(version);
    fixture.commands({0xff});
    const auto layout = findLayout(ByteReader(SourceId{301}, fixture.data()));
    expect(layout && layout->version == version && layout->songIndex == 1 && layout->sequenceHeaderAddress == 0x5100 &&
               layout->tracks.size() == 1 && layout->tracks.front().startAddress == 0x5200 &&
               layout->spcDirAddress == 0x6000 && layout->adsr1TableAddress == 0x4000 &&
               layout->adsr2TableAddress == 0x4100 && layout->tuningHighTableAddress == 0x4200 &&
               layout->tuningLowTableAddress == 0x4300 && layout->alternatePanTableAddress == 0x3000 &&
               layout->defaultPanTableAddress == 0x3020,
           "PrismSnes should identify all dispatch profiles and select the header matching live logical tracks");
  }
}

void profileSpecificOperandLengthsRemainAligned() {
  DriverFixture cosmo(Version::CosmoGang);
  cosmo.commands({0xc0, 0xfb, 1, 2, 3, 4, 5, 0xff});
  TrackProgram cosmoTrack =
      decodeSourceTrack(ByteReader(SourceId{302}, cosmo.data()), Version::CosmoGang, 0, 0x5200, 0, 0x10);
  expect(cosmoTrack.commands.size() == 3 && cosmoTrack.commands[0].encodedSize == 1 &&
             cosmoTrack.commands[1].encodedSize == 6,
         "Cosmo Gang C0-D0 aliases and five-operand FB must follow its distinct dispatch table");

  DriverFixture dualOrb(Version::DualOrb);
  dualOrb.commands({0xc0, 0x03, 0x52, 0xff});
  TrackProgram dualOrbTrack =
      decodeSourceTrack(ByteReader(SourceId{303}, dualOrb.data()), Version::DualOrb, 0, 0x5200, 0, 0x10);
  expect(dualOrbTrack.commands.size() == 2 && dualOrbTrack.commands.front().encodedSize == 3,
         "Dual Orb C0-C5 must remain two-byte conditional jumps rather than later-driver tempo commands");

  DriverFixture modern(Version::Modern);
  modern.commands({0xc0, 0x82, 0xff});
  TrackProgram modernTrack =
      decodeSourceTrack(ByteReader(SourceId{304}, modern.data()), Version::Modern, 0, 0x5200, 0, 0x10);
  expect(modernTrack.commands.size() == 2 && modernTrack.commands.front().encodedSize == 2,
         "later Prism drivers must decode C0-C4 as one-operand timer-target tempo commands");
}

void dynamicDriverFeaturesRenderFromCapturedTables() {
  DriverFixture fixture(Version::Modern);
  fixture.commands({0xc4, 0x64, 0xfe, 0x02, 0xec, 0xc0, 0xeb, 0x0a, 0xe7, 0x01, 0x00, 0x70, 0xfd, 0x00, 0x72,
                    0xf6, 0x10, 0x72, 0xef, 0x20, 0x72, 0xf0, 0x01, 0x1c, 0xfc, 0x8f, 0xe0, 0xcf, 0x00, 0x71,
                    0xf7, 0x00, 0x73, 0xfa, 0xf3, 0x04, 0x3c, 0x08, 0xe9, 0x3c, 0x40, 0x08, 0xff});
  const ByteReader reader(SourceId{305}, fixture.data());
  const Layout layout = *findLayout(reader);
  SequenceParse parsed = decodeSequence(reader, layout, AssetId{305});
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program, sequenceDialect());
  const PerformanceTrack& track = performance.tracks.front();

  const auto tempo = events<TempoPerformanceEvent>(track);
  const auto notes = events<NotePerformanceEvent>(track);
  const auto pitch = events<PitchBendPerformanceEvent>(track);
  const auto levels = events<LevelPerformanceEvent>(track);
  const auto envelopes = events<EnvelopePerformanceEvent>(track);
  const auto reverb = events<ReverbPerformanceEvent>(track);
  expect(performance.diagnostics.empty() && !tempo.empty() && tempo.back()->microsecondsPerQuarter == 600000 &&
             notes.size() == 2 && !pitch.empty() && !levels.empty() && envelopes.size() >= 2 && !reverb.empty() &&
             !track.automations.empty(),
         "captured Prism tables should drive tempo, notes, vibrato, looping volume/GAIN envelopes, echo, and slides");
  expect(
      std::ranges::any_of(
          pitch, [](const PitchBendPerformanceEvent* event) { return std::abs(event->semitones - 0.125) < 0.000001; }),
      "table vibrato must preserve the SPC driver's signed 8.8-semitone samples");
  expect(std::ranges::any_of(reverb,
                             [](const ReverbPerformanceEvent* event) {
                               return event->voiceMask == 1 && event->leftGain && event->rightGain &&
                                      event->delayMilliseconds == 64.0;
                             }),
         "echo must retain voice masks, signed stereo gains, feedback, FIR identity, and EDL timing");
  expect(parsed.programs.contains(2), "instrument discovery should retain referenced SRCN identities");
}

void gainTablesControlNoteAmplitude() {
  DriverFixture fixture(Version::Modern);
  fixture.commands({0xc4, 0x85, 0xfe, 0x02, 0xfd, 0x00, 0x72, 0xec, 0xc0, 0x3c, 0x20, 0xff});
  const ByteReader reader(SourceId{307}, fixture.data());
  const Layout layout = *findLayout(reader);
  SequenceParse parsed = decodeSequence(reader, layout, AssetId{307});
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program, sequenceDialect());
  const PerformanceTrack& track = performance.tracks.front();
  const auto expression = events<ExpressionPerformanceEvent>(track);
  expect(
      performance.diagnostics.empty() && expression.size() > 3 &&
          std::ranges::any_of(expression,
                              [](const ExpressionPerformanceEvent* event) { return event->linearGain == 0.0; }) &&
          std::ranges::any_of(expression,
                              [](const ExpressionPerformanceEvent* event) {
                                return event->linearGain > 0.0 && event->linearGain < 1.0;
                              }) &&
          std::ranges::any_of(expression,
                              [](const ExpressionPerformanceEvent* event) {
                                return event->header.tick >= 5 && event->linearGain < 0.99;
                              }) &&
          std::ranges::any_of(track.automations,
                              [](const PerformanceAutomation& automation) {
                                const auto* intent = std::get_if<ScalarPerformanceAutomationIntent>(&automation.intent);
                                return intent != nullptr && intent->target == PerformanceAutomationTarget::Expression &&
                                       intent->restartsOnNote;
                              }),
      "FD must execute its GAIN table as a note-local amplitude envelope, including the delayed decay phase");
}

void instrumentChangesWaitForTheNextAttack() {
  DriverFixture fixture(Version::Modern);
  fixture.instrumentEnvelope(3, 0x9f, 0xe4)
      .commands({0xfe, 0x02, 0xfd, 0x00, 0x72, 0xef, 0x20, 0x72, 0x3c, 0x08, 0xfe, 0x03, 0xee, 0x08, 0x3e, 0x08, 0xff});
  const ByteReader reader(SourceId{308}, fixture.data());
  const Layout layout = *findLayout(reader);
  SequenceParse parsed = decodeSequence(reader, layout, AssetId{308});
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program, sequenceDialect());
  const PerformanceTrack& track = performance.tracks.front();
  const auto instruments = events<InstrumentPerformanceEvent>(track);
  const auto envelopes = events<EnvelopePerformanceEvent>(track);
  const auto expression = events<ExpressionPerformanceEvent>(track);

  expect(performance.diagnostics.empty() && instruments.size() == 3 && instruments[1]->sourceInstrument &&
             instruments[1]->sourceInstrument->key == 2 && instruments[1]->header.tick == 0 &&
             instruments[2]->sourceInstrument && instruments[2]->sourceInstrument->key == 3 &&
             instruments[2]->header.tick == 16 &&
             std::ranges::none_of(instruments,
                                  [](const InstrumentPerformanceEvent* event) { return event->header.tick == 8; }),
         "FE must defer its MIDI-facing instrument selection across ties until the next real key-on");
  expect(std::ranges::count_if(envelopes,
                               [](const EnvelopePerformanceEvent* event) {
                                 return event->header.tick == 8 && event->scope == VoiceEnvelopeScope::ActiveVoices;
                               }) == 2,
         "FE must reload the ADSR shadows before the same-tick tie supersedes them with GAIN mode");
  expect(std::ranges::none_of(expression,
                              [](const ExpressionPerformanceEvent* event) {
                                return event->header.tick == 8 && event->linearGain == 1.0;
                              }),
         "FE must not interrupt the current GAIN automation before the following tie replaces its table");
}

void leadingTiesAreSilentDelays() {
  DriverFixture fixture(Version::Modern);
  fixture.commands({0xef, 0x20, 0x72, 0xee, 0x02, 0x3c, 0x08, 0xff});
  const ByteReader reader(SourceId{309}, fixture.data());
  const auto parsed = decodeSequence(reader, *findLayout(reader), AssetId{309});
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program, sequenceDialect());
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);

  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->header.tick == 2 &&
             !notes.front()->extendsPrevious && events<EnvelopePerformanceEvent>(track).empty(),
         "a leading tie must consume time without inventing a note or active-voice envelope update");
}

void moduleBuildsTunedSnesSynth() {
  DriverFixture fixture(Version::Modern);
  fixture.commands({0xfe, 0x02, 0xec, 0xff, 0x3c, 0x08, 0xff});
  Session session;
  session.registerFormat(definition());
  session.addSource(SourceFile{.name = "PrismSnes fixture.aram"}, fixture.data());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const Collection* collection = snapshot.collections().empty() ? nullptr : &snapshot.collections().front();
  expect(snapshot.collections().size() == 1 && collection->members.sequence &&
             collection->members.instrumentSets.size() == 1 && collection->members.sampleCollections.size() == 1,
         "PrismSnes scanning should publish one sequence, SRCN instrument set, BRR samples, and explicit collection");
  const auto* set = snapshot.asset<InstrumentSetAsset>(collection->members.instrumentSets.front());
  const Region* region = set != nullptr && !set->instruments.empty() && !set->instruments.front().regions.empty()
                             ? &set->instruments.front().regions.front()
                             : nullptr;
  expect(region != nullptr && std::abs(region->unityKey - 92.5) < 0.000001,
         "signed 8.8 instrument tuning should shift the audited source unity key of 93 exactly");
}

void subtrackTriggersRunTheirChildScore() {
  DriverFixture fixture(Version::Modern);
  fixture.commands({0xf2, 0xed, 0x09, 0x00, 0x74, 0x00, 0x08, 0xff})
      .pointer(0x7400, 0x7500)
      .bytes(0x7500, {0xfe, 0x02, 0xec, 0xc0, 0xeb, 0x0a, 0x3c, 0x04, 0xff});
  const ByteReader reader(SourceId{306}, fixture.data());
  const Layout layout = *findLayout(reader);
  SequenceParse parsed = decodeSequence(reader, layout, AssetId{306});
  expect(
      parsed.program.tracks.front().commands.size() == 4 && parsed.program.tracks.front().commands[2].encodedSize == 2,
      "ED must reset manual duration before decoding its trigger notes");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(parsed.program, sequenceDialect());
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->key == 60.0 &&
             std::ranges::any_of(instruments,
                                 [](const InstrumentPerformanceEvent* event) {
                                   return event->sourceInstrument && event->sourceInstrument->key == 2;
                                 }),
         "subtrack triggers should execute the selected child score after the driver's two-tick voice reset");
}

}  // namespace

void runPrismSnesModuleTests() {
  layoutProfilesAndLiveSongAreAudited();
  profileSpecificOperandLengthsRemainAligned();
  dynamicDriverFeaturesRenderFromCapturedTables();
  gainTablesControlNoteAmplitude();
  instrumentChangesWaitForTheNextAttack();
  leadingTiesAreSilentDelays();
  moduleBuildsTunedSnesSynth();
  subtrackTriggersRunTheirChildScore();
}
