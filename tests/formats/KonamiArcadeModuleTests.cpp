/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/MameRomSetExtractor.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/formats/KonamiArcade/KonamiArcade.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SampleDecoder.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats;
using namespace vgmtrans::formats::konami_arcade;

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

void writeBytes(std::vector<u8>& bytes, size_t offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

template <class T>
const T* firstAsset(const ScanResult& result) {
  for (const auto& asset : result.assets) {
    if (const auto* typed = std::get_if<T>(&asset)) {
      return typed;
    }
  }
  return nullptr;
}

struct KonamiArcadeFixture {
  SourceFile source;
  std::vector<u8> bytes;
};

KonamiArcadeFixture makeMysticWarriorFixture() {
  constexpr u32 codeSize = 0x800;
  constexpr u32 soundSize = 0x100;
  std::vector<u8> bytes(codeSize + soundSize);

  // Driver patterns provide the interrupt timer and skip counter.
  writeBytes(bytes, 0x40, {0x3e, 0x71, 0x32, 0x27, 0xe2});
  writeBytes(bytes, 0x50, {0x3e, 0x03, 0xa6, 0xc2, 0x78, 0x00, 0x2c, 0x36, 0x01});

  // One sequence-table entry followed by a non-entry sentinel.
  writeLe16(bytes, 0x100, 0);
  bytes[0x107] = 0;
  writeLe16(bytes, 0x108, 0x8300);
  writeLe16(bytes, 0x10e, 1);

  // One melodic sample-info row, plus one drum sample-info row.
  writeLe16(bytes, 0x200, 0x210);
  writeLe16(bytes, 0x202, 0x219);
  writeBytes(bytes, 0x210, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  writeBytes(bytes, 0x220, {0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00});

  // Drum zero uses the drum sample, middle pan, and a 50% default duration.
  writeBytes(bytes, 0x229, {0x00, 0x2a, 0x00, 0x08, 0x00, 0x00, 0x32, 0x00});
  bytes[0x232] = 0x60;

  // MysticWarrior track pointers are addresses relative to the sequence's
  // memory base. A zero in the upper half selects the eight-channel layout.
  writeLe16(bytes, 0x300, 0x8320);
  writeBytes(bytes, 0x320,
             {
                 0xea, 0x80,              // tempo
                 0xe2, 0x00,              // program
                 0xee, 0x7f,              // volume
                 0xe3, 0x08,              // pan
                 0x30, 0x06, 0x32, 0x7f,  // note, delta, duration rate, velocity
                 0x26, 0x04, 0x64, 0x0a,  // quiet note entering duration-tie mode
                 0x26, 0x04, 0x63, 0x7f,  // same note: extend it and raise expression
                 0xf0, 0x04,              // continuous portamento over four ticks
                 0x28, 0x08, 0x63, 0x7f,  // glide up two semitones
                 0xf0, 0x00,              // continuous portamento off
                 0xec, 0x04,              // transpose ordinary notes up four semitones
                 0x28, 0x0a, 0x63, 0x7f,  // note followed by a delayed pitch slide
                 0xf3, 0x02, 0x03, 0x2a,  // absolute target ignores channel transpose
                 0x60,                    // percussion on
                 0xe6,                    // loop start
                 0x00, 0x02, 0x00, 0x7f,  // drum note, delta, duration rate, velocity
                 0xe7, 0x02, 0x00, 0x20,  // repeat once, transpose the replay by one key
                 0x61,                    // percussion off
                 0xff,
             });

  // Two short K054539 PCM8 samples terminated by 0x8080.
  writeBytes(bytes, codeSize, {0x10, 0x20, 0x30, 0x40, 0x80, 0x80});
  writeBytes(bytes, codeSize + 0x10, {0x11, 0x22, 0x33, 0x80, 0x80});

  SourceFile source{
      .id = SourceId{42},
      .name = "fixture ROM regions",
      .title = "fixture",
      .size = bytes.size(),
      .attributes =
          {
              {std::string(mame::kMameGameAttribute), "fixture"},
              {std::string(mame::kMameFormatAttribute), std::string(kKonamiArcadeFormatName)},
              {std::string(mame::kMameFormatVersionAttribute), "MysticWarrior"},
          },
      .segments =
          {
              SourceSegment{
                  .name = "soundcpu",
                  .offset = 0,
                  .size = codeSize,
                  .attributes =
                      {
                          {"seq_table", "0x100"},
                          {"samp_tables", "0x200"},
                          {"drum_samp_table", "0x220"},
                          {"drum_table", "0x229"},
                      },
              },
              SourceSegment{.name = "sound", .offset = codeSize, .size = soundSize},
          },
  };
  return KonamiArcadeFixture{.source = std::move(source), .bytes = std::move(bytes)};
}

}  // namespace

void mameRomDatabaseAndGroupAssemblyAreValueOriented() {
  std::istringstream json{R"json(
    {"games":[{
      "name":"fixture",
      "format":"KonamiArcade",
      "fmt_version":"MysticWarrior",
      "rom_groups":[{
        "type":"soundcpu",
        "load_method":"deinterlace",
        "seq_table":"0x100",
        "roms":["a.bin","b.bin"]
      }]
    }]}
  )json"};
  const mame::RomDatabase database = mame::RomDatabase::parse(json);
  const auto* set = database.find("fixture");
  expect(database.size() == 1 && set != nullptr, "MAME database should parse into an immutable named value");
  expect(set->format == "KonamiArcade" && set->formatVersion == "MysticWarrior",
         "MAME set should retain format dispatch metadata");
  expect(set->groups.size() == 1 && set->groups[0].attributes.at("seq_table") == "0x100",
         "MAME group should retain format-specific table attributes");

  mame::RomGroupDefinition deinterlace{
      .name = "soundcpu",
      .loadMethod = mame::RomLoadMethod::Deinterlace,
  };
  expect(mame::assembleRomGroup(deinterlace, {{1, 2}, {3, 4}}) == std::vector<u8>({1, 3, 2, 4}),
         "deinterlace should weave equally sized ROM values");

  mame::RomGroupDefinition swap{
      .name = "sound",
      .loadMethod = mame::RomLoadMethod::AppendSwap16,
      .loadOrder = mame::RomLoadOrder::Reverse,
  };
  expect(mame::assembleRomGroup(swap, {{1, 2}, {3, 4}}) == std::vector<u8>({4, 3, 2, 1}),
         "append_swap16 and reverse load order should compose without mutable loader state");

  mame::RomGroupDefinition pairs{
      .name = "sound",
      .loadMethod = mame::RomLoadMethod::DeinterlacePairs,
  };
  expect(mame::assembleRomGroup(pairs, {{1, 2}, {3, 4}, {5}, {6}}) == std::vector<u8>({1, 3, 2, 4, 5, 6}),
         "deinterlace_pairs should keep pair boundaries while weaving each pair");
  pairs.loadOrder = mame::RomLoadOrder::Reverse;
  expect(mame::assembleRomGroup(pairs, {{1, 2}, {3, 4}, {5}, {6}}) == std::vector<u8>({3, 1, 4, 2, 6, 5}),
         "reverse deinterlace_pairs should reverse each pair without reversing pair order");
}

void konamiArcadeModuleBuildsSequencesSynthAndCollections() {
  const auto fixture = makeMysticWarriorFixture();
  std::vector<Diagnostic> layoutDiagnostics;
  const auto layout =
      findKonamiArcadeLayout(fixture.source, ByteReader(fixture.source.id, fixture.bytes), &layoutDiagnostics);
  expect(layout.has_value() && layoutDiagnostics.empty(),
         "complete KonamiArcade fixture should produce a layout without diagnostics");
  expect(layout->version == KonamiArcadeVersion::MysticWarrior && layout->sequences.size() == 1,
         "layout should retain its engine version and discover one sequence");
  expect(layout->sampleInfos.size() == 2 && layout->melodicSampleCount == 1 && layout->drumCount == 1,
         "layout should separate melodic and drum sample metadata");

  ScanIdAllocator ids;
  const ScanInput input{
      .source = fixture.source,
      .reader = ByteReader(fixture.source.id, fixture.bytes),
      .ids = ids,
  };
  const auto definition = konamiArcadeDefinition();
  const ScanResult result = definition.module.scan(input);
  expect(result.diagnostics.empty(), "complete KonamiArcade scan should not report diagnostics");
  expect(result.assets.size() == 3 && result.explicitCollections.size() == 1,
         "KonamiArcade scan should publish sequence, instruments, samples, and a collection");

  const auto* sequence = firstAsset<SequenceProgramAsset>(result);
  const auto* instruments = firstAsset<InstrumentSetAsset>(result);
  const auto* samples = firstAsset<SampleCollectionAsset>(result);
  expect(sequence != nullptr && instruments != nullptr && samples != nullptr,
         "KonamiArcade result should use the core value asset types");
  expect(sequence->program.dialect.value == kKonamiArcadeSequenceDialectId && sequence->program.tracks.size() == 1 &&
             sequence->program.tracks[0].commands.size() == 19,
         "KonamiArcade sequence should compile the source track into typed command values");
  expect(instruments->instruments.size() == 2,
         "KonamiArcade synth should contain one melodic instrument and one drum kit");
  expect(samples->samples.samples.size() == 2 && samples->samples.samples[0].codec == AudioCodec::PcmS8 &&
             samples->samples.samples[0].encodedData.size == 4 && !samples->samples.samples[0].loop.enabled &&
             samples->samples.samples[0].loop.start == 0 && samples->samples.samples[0].loop.length == 0,
         "KonamiArcade samples should preserve codec and bounded encoded ranges");

  const PerformanceSequence performance =
      SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, konamiArcadeSequenceDialect());
  expect(performance.diagnostics.empty() && performance.tracks.size() == 1 && performance.tracks[0].endTick == 36,
         "KonamiArcade playback should execute counted loops and timing without source bytes");
  std::vector<const NotePerformanceEvent*> notes;
  std::vector<const ExpressionPerformanceEvent*> expressions;
  bool chronological = true;
  u64 previousTick = 0;
  for (const auto& event : performance.tracks[0].events) {
    const u64 tick = performanceEventHeader(event).tick;
    chronological = chronological && tick >= previousTick;
    previousTick = tick;
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    } else if (const auto* expression = std::get_if<ExpressionPerformanceEvent>(&event)) {
      expressions.push_back(expression);
    }
  }
  expect(chronological, "KonamiArcade delayed slides should leave the performance timeline chronological");
  expect(notes.size() == 7 && notes[0]->key == 72.0 && notes[1]->key == 62.0 && notes[2]->key == 62.0 &&
             notes[3]->key == 64.0 && notes[4]->key == 68.0 && notes[5]->key == 24.0 && notes[6]->key == 25.0,
         "KonamiArcade playback should retain nominal notes without synthesizing MIDI slide fragments");
  expect(notes[1]->linearVelocity == 1.0 && !notes[1]->extendsPrevious && notes[2]->linearVelocity == 1.0 &&
             notes[2]->extendsPrevious,
         "100% duration notes should use full note velocity and extend an existing same-key voice");
  expect(expressions.size() == 3 && expressions[0]->header.tick == 6 && expressions[0]->linearGain < 0.01 &&
             expressions[1]->header.tick == 10 && expressions[1]->linearGain == 1.0 &&
             expressions[2]->header.tick == 14 && expressions[2]->linearGain == 1.0,
         "duration-tie velocity changes should become expression changes across the sustained voice");

  const auto settings = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* glide = std::get_if<PitchTransitionSettingsPerformanceEvent>(&event);
    return glide != nullptr && glide->header.tick == 14;
  });
  expect(settings != performance.tracks[0].events.end(),
         "continuous portamento should retain its physical glide setting without choosing a MIDI controller");
  std::vector<const PitchTransitionIntent*> transitions;
  for (const auto& automation : performance.tracks[0].automations) {
    if (const auto* transition = pitchTransitionIntent(automation)) {
      transitions.push_back(transition);
    }
  }
  expect(transitions.size() == 2 && transitions[0]->startKey == 62.0 && transitions[0]->targetKey == 64.0 &&
             transitions[0]->previousNote == notes[2]->note && transitions[0]->nativePortamento.useCurrentTiming &&
             transitions[1]->startKey == 68.0 && transitions[1]->targetKey == 66.0 &&
             std::holds_alternative<FixedDurationPitchSlideTiming>(transitions[1]->timing.physical),
         "continuous and delayed slides should retain typed intent, including F3's absolute target");
  expect(std::ranges::none_of(performance.tracks[0].events,
                              [](const PerformanceEvent& event) {
                                return std::holds_alternative<PortamentoPerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoControlPerformanceEvent>(event) ||
                                       std::holds_alternative<PitchBendPerformanceEvent>(event);
                              }),
         "KonamiArcade format code should not preselect a MIDI slide representation");

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{instruments};
  const MidiSequence midi =
      renderMidiSequence(performance, {}, ModulationConversionPolicy::SynthModulators, instrumentSets);
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* bank = std::get_if<BankSelect>(&event);
                               return bank != nullptr && bank->bank == (2 << 7);
                             }),
         "KonamiArcade percussion should select SF2 bank 2 under MSB-only MIDI lowering");
  const auto tiedNote = std::ranges::find_if(midi.tracks[0].events, [](const MidiEvent& event) {
    const auto* note = std::get_if<NoteDuration>(&event);
    return note != nullptr && note->tick == 6 && note->key == 62;
  });
  expect(tiedNote != midi.tracks[0].events.end() && std::get<NoteDuration>(*tiedNote).duration == 9,
         "MIDI lowering should retain one note-on and the one-tick overlap needed for portamento");
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* control = std::get_if<PortamentoControl>(&event);
                               return control != nullptr && control->tick == 14 && control->key == 62;
                             }),
         "MIDI lowering should retain the continuous-portamento source key");
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* note = std::get_if<NoteDuration>(&event);
                               return note != nullptr && note->tick == 24 && note->key == 66 && note->duration == 8;
                             }),
         "delayed slides should emit an overlapping target note at the driver-specified tick");

  const MidiSequence pitchBendMidi =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend},
                         ModulationConversionPolicy::SynthModulators, instrumentSets);
  expect(std::ranges::none_of(pitchBendMidi.tracks[0].events,
                              [](const MidiEvent& event) {
                                return std::holds_alternative<PortamentoTime>(event) ||
                                       std::holds_alternative<PortamentoTime14>(event) ||
                                       std::holds_alternative<PortamentoControl>(event);
                              }) &&
             std::ranges::any_of(pitchBendMidi.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* bend = std::get_if<PitchBend>(&event);
                                   return bend != nullptr && bend->tick >= 14 && bend->value != 0;
                                 }),
         "the export request should be able to render KonamiArcade transitions as pitch bend");
  expect(std::ranges::any_of(pitchBendMidi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* note = std::get_if<NoteDuration>(&event);
                               return note != nullptr && note->tick == 22 && note->key == 68 && note->duration == 10;
                             }),
         "pitch-bend lowering should retain the delayed slide's one nominal source note");
}

void konamiArcadeAdpcmDecoderSupportsForwardAndReverseSamples() {
  const std::array<u8, 2> bytes{0x12, 0x34};
  Sample sample{
      .codec = AudioCodec::KonamiK054539Adpcm,
      .encodedData = SourceRange{.source = SourceId{7}, .offset = 0, .size = bytes.size()},
      .sampleRate = kKonamiArcadeSampleRate,
  };
  const auto forward = decodeSample(sample, bytes);
  expect(forward && forward->pcm.size() == 4 && forward->pcm[0] == 512 && forward->pcm[1] == 768,
         "K054539 ADPCM should decode low then high nibbles with the chip delta table");

  sample.reverse = true;
  const auto reverse = decodeSample(sample, bytes);
  expect(reverse && reverse->pcm.size() == 4 && reverse->pcm[0] == 2048 && reverse->pcm[1] == 3072,
         "reverse K054539 samples should walk encoded bytes backward without copying source data");
}
