/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"

#include "value/export/midi/MidiExporter.h"

#include <array>
#include <limits>

using namespace vgmtrans::core;

namespace {

void midiExporterWritesStandardMidiFile() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track{.name = "Lead", .endTick = 24};
  track.events = {
      midi::meta(0, 0x51, {0x07, 0xa1, 0x20}),
      midi::meta(0, 0x21, {2}, -5),
      midi::programChange(0, 0, 5),
      midi::controller(0, 0, MidiController::ChannelVolume, 100),
      midi::note(0, 0, 60, 100, 24),
      midi::controller(12, 0, MidiController::Pan, 64),
  };
  midiSequence.tracks.push_back(std::move(track));

  const std::vector<u8> expected{
      'M',  'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 'M',  'T',  'r',
      'k',  0x00, 0x00, 0x00, 0x2b, 0x00, 0xff, 0x03, 0x04, 'L',  'e',  'a',  'd',  0x00, 0xff, 0x21, 0x01,
      0x02, 0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20, 0x00, 0xc0, 0x05, 0x00, 0xb0, 0x07, 0x64, 0x00, 0x90,
      0x3c, 0x64, 0x0c, 0xb0, 0x0a, 0x40, 0x0c, 0x80, 0x3c, 0x40, 0x00, 0xff, 0x2f, 0x00,
  };

  const auto exported = encodeMidiFile(midiSequence);
  expect(exported == expected, "MIDI exporter should write expected SMF bytes");
}

void midiExporterWritesVariableLengthBoundaries() {
  const std::vector<std::pair<u64, std::vector<u8>>> cases{
      {0, {0x00}},
      {127, {0x7f}},
      {128, {0x81, 0x00}},
      {16383, {0xff, 0x7f}},
      {16384, {0x81, 0x80, 0x00}},
      {2097151, {0xff, 0xff, 0x7f}},
      {2097152, {0x81, 0x80, 0x80, 0x00}},
      {268435455, {0xff, 0xff, 0xff, 0x7f}},
  };
  for (const auto& [tick, delta] : cases) {
    const MidiSequence sequence{.tracks = {MidiTrack{.events = {midi::programChange(tick, 0, 5)}}}};
    const auto bytes = encodeMidiFile(sequence);
    auto expected = delta;
    expected.insert(expected.end(), {0xc0, 0x05, 0x00, 0xff, 0x2f, 0x00});
    expect(std::vector<u8>(bytes.begin() + 22, bytes.end()) == expected,
           "SMF delta times must keep byte order and continuation bits at every seven-bit boundary");
  }
}

void midiExporterKeeps14BitControllerPairsAdjacent() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track;
  midi::appendController14(track, 0, 0, MidiController::ChannelVolume, 0x1234);
  track.events.push_back(midi::controller(0, 0, MidiController::Pan, 64));
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x07, 0x24, 0x00, 0xb0, 0x27, 0x34, 0x00, 0xb0, 0x0a, 0x40,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should keep 14-bit volume MSB/LSB controllers adjacent before same-tick pan");
}

void midiExporterWritesAllSoundOffImmediatelyBeforeNoteOn() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track{.endTick = 36};
  track.events = {midi::note(12, 2, 60, 100, 24), midi::controller(12, 2, MidiController::AllSoundOff, 0, 45)};
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x0c, 0xb2, 0x78, 0x00, 0x00, 0x92, 0x3c, 0x64,
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should write All Sound Off immediately before the replacement note-on");
}

void midiExporterReleasesBeforeCuttingAndRetuningAVoice() {
  MidiSequence sequence{.timebase = {.ppqn = 48}};
  sequence.tracks.push_back(
      MidiTrack{.endTick = 8,
                .events = {midi::note(0, 2, 60, 100, 4), midi::controller(4, 2, MidiController::AllSoundOff, 0, 7),
                           midi::pitchBend(4, 2, 0), midi::note(4, 2, 62, 100, 4)}});
  const auto bytes = encodeMidiFile(sequence);
  const std::vector<u8> ordered{4, 0x82, 60, 64, 0, 0xb2, 120, 0, 0, 0xe2, 0, 64, 0, 0x92, 62, 100};
  expect(std::search(bytes.begin(), bytes.end(), ordered.begin(), ordered.end()) != bytes.end(),
         "same-tick output must order Note Off, All Sound Off, new pitch, then Note On");
}

void midiExporterPreservesLegacyPortamentoTimeByteOrder() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track;
  midi::appendController14(track, 0, 0, MidiController::PortamentoTime, 0x01d3, true);
  track.events.push_back(midi::controller(0, 0, MidiController::PortamentoControl, 60));
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x25, 0x53, 0x00, 0xb0, 0x05, 0x03, 0x00, 0xb0, 0x54, 0x3c,
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should write fine then coarse portamento time before source-key control");
}

void midiExporterOrdersFineTuneBeforeSameTickProgramChange() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track;
  midi::appendRpn(track, 0, 2, 0, 1, 0x1100, 8);
  track.events.push_back(midi::bankSelect(0, 2, 0, false));
  track.events.push_back(midi::programChange(0, 2, 9));
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb2, 0x65, 0x00, 0x00, 0xb2, 0x64, 0x01, 0x00, 0xb2, 0x06, 0x22,
      0x00, 0xb2, 0x26, 0x00, 0x00, 0xb2, 0x00, 0x00, 0x00, 0xc2, 0x09,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should serialize fine tuning RPN before same-tick bank and program changes");
}

void midiExporterKeepsSameTickBankProgramPairsAdjacent() {
  MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48}};
  MidiTrack track{.endTick = 24};
  track.events = {
      midi::bankSelect(0, 0, 0, false), midi::programChange(0, 0, 13), midi::bankSelect(0, 0, 0x7f, false),
      midi::programChange(0, 0, 0),     midi::note(0, 0, 60, 100, 24),
  };
  midiSequence.tracks.push_back(std::move(track));

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x00, 0xb0, 0x00, 0x00, 0x00, 0xc0, 0x0d, 0x00, 0xb0, 0x00, 0x7f, 0x00, 0xc0, 0x00, 0x00, 0x90, 0x3c, 0x64,
  };

  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should keep same-tick bank/program pairs adjacent before notes");
}

void midiExporterWritesTimeSignatureMetaEvent() {
  const MidiSequence midiSequence{.timebase = Timebase{.ppqn = 48},
                                  .tracks = {MidiTrack{.events = {midi::meta(0, 0x58, {3, 2, 48, 8})}}}};

  const std::vector<u8> expected{
      'M', 'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 'M',  'T',  'r',
      'k', 0x00, 0x00, 0x00, 0x0c, 0x00, 0xff, 0x58, 0x04, 0x03, 0x02, 0x30, 0x08, 0x00, 0xff, 0x2f, 0x00,
  };

  expect(encodeMidiFile(midiSequence) == expected, "MIDI exporter should write time-signature meta events");
}

void midiExporterOrdersGeneratedNoteOffBeforeSameTickNoteOn() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{.events = {midi::note(10, 0, 60, 100, 10), midi::note(0, 0, 60, 100, 10)}, .endTick = 20}}};

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x0a, 0x80, 0x3c, 0x40, 0x00, 0x90, 0x3c, 0x64,
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "MIDI exporter should write generated note-off before same-tick note-on");
}

void midiExporterKeepsZeroDurationNotePairedAtSameTick() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{.events = {midi::note(10, 0, 60, 90, 0), midi::note(10, 0, 60, 100, 5)}, .endTick = 15}}};

  const auto exported = encodeMidiFile(midiSequence);
  const std::vector<u8> expectedOrder{
      0x0a, 0x90, 0x3c, 0x5a,  // zero-duration attack
      0x00, 0x80, 0x3c, 0x40,  // its same-tick release
      0x00, 0x90, 0x3c, 0x64,  // following attack of the same key
  };
  expect(std::search(exported.begin(), exported.end(), expectedOrder.begin(), expectedOrder.end()) != exported.end(),
         "zero-duration notes should close before a following same-key attack");
}

void midiExporterSaturatesTimelineEndpoints() {
  constexpr u64 lastTick = std::numeric_limits<u64>::max();
  struct Case { Timebase timebase; u64 tick; u32 duration; u8 attackDeltaEnd; u8 releaseDelta; };
  const std::array<Case, 4> cases{{
      {{48}, lastTick - 2, 3, 0x7d, 2},
      {{48}, lastTick, 1, 0x7f, 0},
      {{1, 2}, u64{1} << 63, 0, 0x7f, 0},
      {{2, 3}, 0xaaaaaaaaaaaaaaab, 0, 0x7f, 0},
  }};
  for (const auto& test : cases) {
    const MidiSequence sequence{
        .timebase = test.timebase,
        .tracks = {MidiTrack{.events = {{.tick = test.tick,
                                      .payload = NoteDuration{.key = 60, .velocity = 100,
                                                              .duration = test.duration}}}}}};
    const auto bytes = encodeMidiFile(sequence);
    const std::vector<u8> expected{
        0x81, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, test.attackDeltaEnd,
        0x90, 0x3c, 0x64,
        test.releaseDelta, 0x80, 0x3c, 0x40,
        0x00, 0xff, 0x2f, 0x00,
    };
    expect(std::vector<u8>(bytes.begin() + 22, bytes.end()) == expected,
           "MIDI tick conversion and note endpoints should saturate and keep clipped notes paired");
  }
}

void midiExporterClampsSignedPitchBendBeforeBias() {
  const std::array<std::pair<s32, u16>, 11> cases{{
      {std::numeric_limits<s32>::min(), 0},
      {-8193, 0},
      {-8192, 0},
      {-1, 8191},
      {0, 8192},
      {8191, 16383},
      {8192, 16383},
      {std::numeric_limits<s32>::max() - 8193, 16383},
      {std::numeric_limits<s32>::max() - 8192, 16383},
      {std::numeric_limits<s32>::max() - 8191, 16383},
      {std::numeric_limits<s32>::max(), 16383},
  }};
  for (const auto& [value, expected] : cases) {
    const MidiSequence sequence{
        .tracks = {MidiTrack{.events = {{.payload = MidiChannelMessage{
                                            .kind = MidiChannelMessageKind::PitchBend,
                                            .value = value,
                                        }}}}}};
    const auto bytes = encodeMidiFile(sequence);
    const std::vector<u8> expectedMessages{
        0x00, 0xe0, static_cast<u8>(expected & 0x7f), static_cast<u8>(expected >> 7),
        0x00, 0xff, 0x2f, 0x00,
    };
    expect(std::vector<u8>(bytes.begin() + 22, bytes.end()) == expectedMessages,
           "MIDI pitch bend should clip the signed offset before adding its unsigned 14-bit bias");
  }
}

}  // namespace

void runValueMidiEncodingTests() {
  midiExporterWritesStandardMidiFile();
  midiExporterWritesVariableLengthBoundaries();
  midiExporterKeeps14BitControllerPairsAdjacent();
  midiExporterWritesAllSoundOffImmediatelyBeforeNoteOn();
  midiExporterReleasesBeforeCuttingAndRetuningAVoice();
  midiExporterPreservesLegacyPortamentoTimeByteOrder();
  midiExporterOrdersFineTuneBeforeSameTickProgramChange();
  midiExporterKeepsSameTickBankProgramPairsAdjacent();
  midiExporterWritesTimeSignatureMetaEvent();
  midiExporterOrdersGeneratedNoteOffBeforeSameTickNoteOn();
  midiExporterKeepsZeroDurationNotePairedAtSameTick();
  midiExporterSaturatesTimelineEndpoints();
  midiExporterClampsSignedPitchBendBeforeBias();
}
