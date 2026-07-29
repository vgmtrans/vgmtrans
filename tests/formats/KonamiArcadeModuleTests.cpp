/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/MameRomSetExtractor.h"
#include "value/export/SequenceModulationProfile.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/formats/KonamiArcade/KonamiArcade.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SampleDecoder.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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

void writeBe32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value >> 24);
  bytes[offset + 1] = static_cast<u8>(value >> 16);
  bytes[offset + 2] = static_cast<u8>(value >> 8);
  bytes[offset + 3] = static_cast<u8>(value);
}

void writeBytes(std::vector<u8>& bytes, size_t offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

KonamiArcadeSequenceLayout makeGxSequenceLayout(SourceId source, std::string name) {
  return KonamiArcadeSequenceLayout{
      .index = 0,
      .offset = 0x20,
      .trackTable = SourceRange{.source = source, .offset = 0x20, .size = 64},
      .tracks =
          {
              KonamiArcadeTrackLayout{
                  .number = 0,
                  .encodedAddress = 0x80,
                  .offset = 0x80,
                  .pointer = SourceRange{.source = source, .offset = 0x20, .size = 4},
              },
          },
      .name = std::move(name),
  };
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
  bytes[0x103] = 1;     // tempo offset
  bytes[0x104] = 0xff;  // initial loudness offset (-1)
  bytes[0x105] = 2;     // initial transpose
  bytes[0x107] = 0;
  writeLe16(bytes, 0x108, 0x8300);
  writeLe16(bytes, 0x10a, 0x0280);
  writeLe16(bytes, 0x10e, 1);
  writeBytes(bytes, 0x280, {0x2a, 0x01, 0xff, 0xff});

  // One melodic sample-info row, plus one drum sample-info row.
  writeLe16(bytes, 0x200, 0x210);
  writeLe16(bytes, 0x202, 0x219);
  writeBytes(bytes, 0x210, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  writeBytes(bytes, 0x220, {0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00});

  // MysticWarrior uses the high six bits as unsigned sixteenths, so 0x80 is
  // two semitones. Unlike GX, its zero default duration remains zero.
  writeBytes(bytes, 0x229, {0x00, 0x2a, 0x80, 0x08, 0x00, 0x00, 0x00, 0x00});
  bytes[0x232] = 0x60;

  // MysticWarrior track pointers are addresses relative to the sequence's
  // memory base. A zero in the upper half selects the eight-channel layout.
  writeLe16(bytes, 0x300, 0x8320);
  writeBytes(bytes, 0x320,
             {
                 0xea, 0x80,                                         // tempo
                 0xe2, 0x00,                                         // program
                 0xee, 0x7f,                                         // volume
                 0xe3, 0x08,                                         // pan
                 0xe4, 0x01, 0x40, 0x20,                             // vibrato: delay, rate, depth
                 0xf9, 0x04,                                         // fade vibrato depth over four ticks
                 0x30, 0x06, 0x32, 0x7f,                             // note, delta, duration rate, velocity
                 0x26, 0x04, 0x64, 0x0a,                             // quiet note entering duration-tie mode
                 0x26, 0x04, 0x63, 0x7f,                             // same note: extend it and raise expression
                 0xf0, 0x04,                                         // continuous portamento over four ticks
                 0x28, 0x08, 0x63, 0x7f,                             // glide up two semitones
                 0xf0, 0x00,                                         // continuous portamento off
                 0xec, 0x04,                                         // transpose ordinary notes up four semitones
                 0x28, 0x0a, 0x63, 0x7f,                             // note followed by a delayed pitch slide
                 0xf3, 0x02, 0x03, 0x2a,                             // Z80 target uses the note transpose path
                 0x60,                                               // percussion on
                 0xe6,                                               // loop start
                 0x00, 0x02, 0x00, 0x7f,                             // drum note, delta, duration rate, velocity
                 0xe7, 0x02, 0x80, 0x20,                             // replay silently, transposed by one key
                 0x61,                                               // percussion off
                 0xc4, 0xc5, 0xc6, 0xcd, 0xce, 0xdc, 0xdd,           // Z80 zero-data driver states
                 0xf4, 1,    2,    3,    0xf5, 4,    5,    6, 0xfc,  // Z80-only command widths
                 0xd2, 0x0f, 0x00,                                   // low nibble is encoded first on Z80
                 0xf2, 0xfd,                                         // Z80 negative packed pitch (-2/16)
                 0xfb, 0x00,                                         // indexed jump to a four-byte note
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
  const auto& sequenceLayout = layout->sequences.front();
  expect(sequenceLayout.trackTable.offset == 0x300 && sequenceLayout.trackTable.size == 16 &&
             sequenceLayout.tracks.size() == 1 && sequenceLayout.tracks.front().encodedAddress == 0x8320 &&
             sequenceLayout.tracks.front().offset == 0x320,
         "layout should retain the exact track table and its normalized track addresses");
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
             sequence->program.tracks[0].commands.size() == 35,
         "KonamiArcade sequence should compile the source track into typed command values");
  expect(instruments->instruments.size() == 2,
         "KonamiArcade synth should contain one melodic instrument and one drum kit");
  expect(samples->samples.samples.size() == 2 && samples->samples.samples[0].codec == AudioCodec::PcmS8 &&
             samples->samples.samples[0].encodedData.size == 4 && !samples->samples.samples[0].loop.enabled &&
             samples->samples.samples[0].loop.start == 0 && samples->samples.samples[0].loop.length == 0 &&
             std::abs(samples->samples.samples[0].attenuationDb) < 0.0001,
         "MysticWarrior samples should preserve codec and bounds without GX's fixed attenuation");
  expect(instruments->instruments[1].regions.size() == 1 &&
             std::abs(instruments->instruments[1].regions[0].unityKey - 22.0) < 0.0001,
         "MysticWarrior drums should preserve the Z80 driver's six-bit-sixteenths pitch");

  const PerformanceSequence performance =
      SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, konamiArcadeSequenceDialect());
  expect(performance.diagnostics.empty() && performance.tracks.size() == 1 && performance.tracks[0].endTick == 37,
         "KonamiArcade playback should execute loops and retain MysticWarrior command alignment");
  const SequenceModulationProfile modulationProfile = analyzeSequenceModulation(performance);
  const double expectedVibratoRate = (0x40 / 256.0) * (0x81 / 256.0) * layout->nmiRateHertz;
  expect(modulationProfile.instruments.vibrato &&
             std::abs(modulationProfile.instruments.vibrato->maxDepthCents - 100.0) < 0.0001 &&
             std::abs(modulationProfile.instruments.vibrato->rateHertz.minimum - expectedVibratoRate) < 0.0001 &&
             modulationProfile.instruments.vibrato->delaySeconds,
         "KonamiArcade E4 should preserve physical triangle-LFO depth, tempo-relative rate, and delay");
  expect(!instruments->instruments.front().modulation.vibrato,
         "scanned KonamiArcade instruments should not guess sequence-independent vibrato");
  InstrumentSetAsset preparedInstruments = *instruments;
  applySequenceModulation(preparedInstruments, modulationProfile);
  expect(preparedInstruments.instruments.front().modulation.vibrato &&
             std::abs(preparedInstruments.instruments.front().modulation.vibrato->maxDepthCents - 100.0) < 0.0001,
         "collection preparation should apply the sequence's vibrato range to KonamiArcade instruments");
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
  expect(notes.size() == 8 && notes[0]->key == 74.0 && notes[1]->key == 64.0 && notes[2]->key == 64.0 &&
             notes[3]->key == 66.0 && notes[4]->key == 70.0 && notes[5]->key == 24.0 && notes[6]->key == 24.0 &&
             notes[7]->key == 72.0 && notes[7]->durationTicks == 1,
         "KonamiArcade playback should retain nominal notes without synthesizing MIDI slide fragments");
  expect(notes[5]->durationTicks == 2 && notes[6]->durationTicks == 2,
         "MysticWarrior should preserve a zero drum-table duration instead of applying GX's fallback");
  expect(notes[6]->linearVelocity < notes[5]->linearVelocity,
         "the full signed range of loop loudness deltas should survive attenuation-domain conversion");
  expect(std::ranges::any_of(performance.tracks[0].events,
                             [](const PerformanceEvent& event) {
                               const auto* reverb = std::get_if<ReverbPerformanceEvent>(&event);
                               return reverb != nullptr && reverb->send < 0.0001;
                             }),
         "MysticWarrior D2 should pack its low-nibble operand before its high-nibble operand");
  expect(std::ranges::any_of(performance.tracks[0].events,
                             [](const PerformanceEvent& event) {
                               const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
                               return bend != nullptr && bend->header.tick == 36 &&
                                      std::abs(bend->semitones + 0.125) < 0.0001;
                             }),
         "MysticWarrior F2 should preserve its asymmetric negative packed-fraction conversion");
  expect(notes[1]->linearVelocity == 1.0 && !notes[1]->extendsPrevious && notes[2]->linearVelocity == 1.0 &&
             notes[2]->extendsPrevious,
         "100% duration notes should use full note velocity and extend an existing same-key voice");
  expect(expressions.size() == 3 && expressions[0]->header.tick == 6 && expressions[0]->linearGain < 0.01 &&
             expressions[1]->header.tick == 10 && expressions[1]->linearGain > 0.9 &&
             expressions[1]->linearGain < 1.0 && expressions[2]->header.tick == 14 && expressions[2]->linearGain == 1.0,
         "duration-tie velocity and header attenuation should become expression changes across the sustained voice");

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
  expect(
      transitions.size() == 2 && transitions[0]->startKey == 64.0 && transitions[0]->targetKey == 66.0 &&
          transitions[0]->previousNote == notes[2]->note && transitions[0]->nativePortamento.useCurrentTiming &&
          transitions[1]->startKey == 70.0 && transitions[1]->targetKey == 72.0 &&
          std::holds_alternative<FixedDurationPitchSlideTiming>(transitions[1]->timing.physical),
      "continuous and delayed slides should retain typed intent, including MysticWarrior's fully transposed F3 target");
  expect(std::ranges::none_of(performance.tracks[0].events,
                              [](const PerformanceEvent& event) {
                                return std::holds_alternative<PortamentoPerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoControlPerformanceEvent>(event);
                              }),
         "KonamiArcade format code should not preselect a MIDI slide representation");

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{instruments};
  const MidiSequence midi = renderMidiSequence(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat},
      ModulationConversionPolicy::SynthModulators, instrumentSets, &modulationProfile);
  expect(
      std::ranges::any_of(midi.tracks[0].events,
                          [](const MidiEvent& event) { return std::holds_alternative<VibratoDepth>(event); }) &&
          std::ranges::any_of(midi.tracks[0].events,
                              [](const MidiEvent& event) { return std::holds_alternative<VibratoFrequency>(event); }) &&
          std::ranges::any_of(midi.tracks[0].events,
                              [](const MidiEvent& event) { return std::holds_alternative<VibratoDelay>(event); }),
      "KonamiArcade synth-modulator lowering should retain vibrato depth, rate, and delay controls");
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* bank = std::get_if<BankSelect>(&event);
                               return bank != nullptr && bank->bank == (2 << 7);
                             }),
         "KonamiArcade percussion should select SF2 bank 2 under MSB-only MIDI lowering");
  const auto tiedNote = std::ranges::find_if(midi.tracks[0].events, [](const MidiEvent& event) {
    const auto* note = std::get_if<NoteDuration>(&event);
    return note != nullptr && note->tick == 6 && note->key == 64;
  });
  expect(tiedNote != midi.tracks[0].events.end() && std::get<NoteDuration>(*tiedNote).duration == 9,
         "MIDI lowering should retain one note-on and the one-tick overlap needed for portamento");
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* control = std::get_if<PortamentoControl>(&event);
                               return control != nullptr && control->tick == 14 && control->key == 64;
                             }),
         "MIDI lowering should retain the continuous-portamento source key");
  expect(std::ranges::any_of(midi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* note = std::get_if<NoteDuration>(&event);
                               return note != nullptr && note->tick == 22 && note->key == 70 && note->duration == 3;
                             }) &&
             std::ranges::any_of(midi.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* note = std::get_if<NoteDuration>(&event);
                                   return note != nullptr && note->tick == 24 && note->key == 72 && note->duration == 8;
                                 }),
         "MysticWarrior F3 timing should overlap the fully transposed source and target notes by one tick");

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
  expect(std::ranges::none_of(pitchBendMidi.tracks[0].events,
                              [](const MidiEvent& event) {
                                const auto* note = std::get_if<NoteDuration>(&event);
                                return note != nullptr && note->tick == 14 && note->key == 66;
                              }),
         "continuous KonamiArcade pitch bend should not retrigger its destination note");
  expect(std::ranges::any_of(pitchBendMidi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* note = std::get_if<NoteDuration>(&event);
                               return note != nullptr && note->tick == 22 && note->key == 70 && note->duration == 10;
                             }),
         "pitch-bend lowering should retain the delayed slide's one nominal source note");
}

void konamiArcadeGxLfosMatchDriverState() {
  std::vector<u8> bytes(0x200);
  writeBe32(bytes, 0x20, 0x80);
  writeBytes(bytes, 0x80,
             {
                 0xea, 0x80,              // tempo
                 0xe4, 0x01, 0x20, 0x10,  // retriggered vibrato
                 0xf9, 0x04,              // four-tick depth fade
                 0x30, 0x06, 0x32, 0x7f,  // restarts phase and fade
                 0xdf, 0x00, 0x10, 0x08,  // continuous vibrato
                 0xed, 0x01, 0x40, 0x20,  // tremolo: delay, rate, depth
                 0x32, 0x06, 0x32, 0x7f,  // preserves phase
                 0xe4, 0x00, 0x10, 0x80,  // high-depth scale
                 0xe5, 0x20, 0x01, 0xff,  // random pitch spikes: rate, mask
                 0x34, 0x06, 0x32, 0x7f,  // restarts phase
                 0xff,
             });

  const SourceFile source{
      .id = SourceId{77},
      .name = "Salamander 2 LFO fixture",
      .title = "fixture",
      .size = bytes.size(),
  };
  KonamiArcadeLayout layout{
      .version = KonamiArcadeVersion::Gx,
      .game = "fixture",
      .code = SourceRange{.source = source.id, .offset = 0, .size = bytes.size()},
      .nmiRateHertz = 245.0,
  };
  const KonamiArcadeSequenceLayout sequenceLayout = makeGxSequenceLayout(source.id, "LFO");
  std::vector<Diagnostic> diagnostics;
  const SequenceProgram program = decodeKonamiArcadeSequence(ByteReader(source.id, bytes), layout, sequenceLayout,
                                                             AssetId{1}, nullptr, &diagnostics);
  expect(diagnostics.empty() && program.tracks.size() == 1 && program.tracks[0].commands.size() == 11,
         "GX LFO commands should decode into typed sequence commands");

  const PerformanceSequence performance =
      SequenceVm(LoopPolicy::PlayOnce).render(program, konamiArcadeSequenceDialect());
  expect(performance.diagnostics.empty() && performance.tracks.size() == 1,
         "GX LFO playback should render without diagnostics");

  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 3 && notes[0]->restartsLfoPhase && !notes[1]->restartsLfoPhase && notes[2]->restartsLfoPhase,
         "E4 should restart vibrato on ordinary notes while active DF preserves oscillator phase");
  expect(notes[1]->restartsVibratoLfoPhase == false && notes[1]->restartsTremoloLfoPhase == true,
         "DF should preserve vibrato without suppressing ED's independent note-on tremolo reset");

  const auto fade = std::ranges::find_if(performance.tracks[0].automations, [](const PerformanceAutomation& value) {
    const auto* scalar = std::get_if<ScalarPerformanceAutomationIntent>(&value.intent);
    return scalar != nullptr && scalar->target == PerformanceAutomationTarget::VibratoDepth;
  });
  expect(fade != performance.tracks[0].automations.end(),
         "F9 should create a reusable note-scoped vibrato depth envelope");
  const auto& fadeIntent = std::get<ScalarPerformanceAutomationIntent>(fade->intent);
  expect(fadeIntent.motion == PerformanceAutomationMotion::Envelope && fadeIntent.durationTicks == 4 &&
             fadeIntent.delayTicks == 2 && fadeIntent.targetValue && std::abs(*fadeIntent.targetValue - 0.5) < 0.0001,
         "F9 depth fade should begin after E4's delay and reach the configured piecewise depth");

  const SequenceModulationProfile profile = analyzeSequenceModulation(performance);
  expect(profile.instruments.vibrato && std::abs(profile.instruments.vibrato->maxDepthCents - 1600.0) < 0.0001 &&
             std::abs(profile.instruments.vibrato->rateHertz.maximum - 15.3125) < 0.0001,
         "GX vibrato should preserve the 0x80 depth discontinuity and tempo-relative physical rate");
  const double expectedTremoloRate = (0x40 / 256.0) * (0x80 / 256.0) * layout.nmiRateHertz;
  expect(profile.instruments.tremolo && std::abs(profile.instruments.tremolo->maxDepthDb - 4.5) < 0.0001 &&
             std::abs(profile.instruments.tremolo->rateHertz.maximum - expectedTremoloRate) < 0.0001 &&
             profile.instruments.tremolo->delaySeconds &&
             profile.instruments.tremolo->gainMode == TremoloGainMode::NoBoost,
         "ED tremolo should preserve its attenuation-only triangle depth, tempo-relative rate, and delay");
}

void konamiArcadeGxDriverQuirksRemainRepresented() {
  std::vector<u8> bytes(0x200);
  writeBe32(bytes, 0x20, 0x80);
  writeBytes(bytes, 0x80,
             {
                 0xea, 0x80,                          // tempo
                 0xfa, 0x01,                          // enable a software release
                 0xe3, 0x81,                          // encoded hard-left pan
                 0x30, 0x0a, 0x1e, 0x7f,              // establish the Salamander 2 track's 30% duration
                 0xe0, 0x24,                          // Salamander 2 0x102bd: clears only live duration
                 0x2e, 0x18, 0xfd,                    // 0x102bf A#4 reuses the stored 30% duration
                 0x91, 0xfd,                          // 0x102c2 B4 reuses both delta and duration
                 0xf2, 0x40,                          // tune future notes up one semitone
                 0x92, 0x00, 0x7f,                    // explicit zero preserves 30%
                 0xe3, 0x00,                          // let drums supply pan
                 0x60,                                // percussion on
                 0x00, 0x02, 0x00, 0x7f,              // zero table duration becomes 99%
                 0xd2, 0x0f, 0x0f,                    // maximum reverb send
                 0xc1, 0x55,                          // valid one-byte driver state
                 0xc4, 1,    2,    3,    4, 5, 6, 7,  // seven-byte DSP command
                 0xdb, 0x44,                          // valid one-byte driver state
                 0xdc, 0x03,                          // loop-point program selection
                 0xff,
             });

  const SourceFile source{
      .id = SourceId{78},
      .name = "Salamander 2 driver-quirk fixture",
      .title = "fixture",
      .size = bytes.size(),
  };
  KonamiArcadeLayout layout{
      .version = KonamiArcadeVersion::Gx,
      .game = "fixture",
      .code = SourceRange{.source = source.id, .offset = 0, .size = bytes.size()},
      .nmiRateHertz = 245.0,
  };
  layout.drums[0] = KonamiArcadeDrum{
      .unityKey = 0x2a,
      .pan = 0x82,
      .defaultDuration = 0,
  };
  layout.drumCount = 1;
  KonamiArcadeSequenceLayout sequenceLayout = makeGxSequenceLayout(source.id, "Driver quirks");
  sequenceLayout.initialAttenuation = 1;
  sequenceLayout.initialTranspose = 2;
  sequenceLayout.tempoOffset = 1;
  std::vector<Diagnostic> diagnostics;
  const SequenceProgram program = decodeKonamiArcadeSequence(ByteReader(source.id, bytes), layout, sequenceLayout,
                                                             AssetId{1}, nullptr, &diagnostics);
  expect(diagnostics.empty() && program.tracks.size() == 1 && program.tracks[0].commands.size() == 18,
         "valid GX state and DSP commands should not truncate sequence decoding");

  const PerformanceSequence performance =
      SequenceVm(LoopPolicy::PlayOnce).render(program, konamiArcadeSequenceDialect());
  expect(performance.diagnostics.empty() && performance.tracks.size() == 1,
         "GX driver-state commands should render without diagnostics");

  std::vector<const NotePerformanceEvent*> notes;
  std::vector<const StereoBalancePerformanceEvent*> balances;
  std::vector<const ReverbPerformanceEvent*> reverbs;
  std::vector<const PitchBendPerformanceEvent*> pitchBends;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    } else if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event)) {
      balances.push_back(balance);
    } else if (const auto* reverb = std::get_if<ReverbPerformanceEvent>(&event)) {
      reverbs.push_back(reverb);
    } else if (const auto* pitchBend = std::get_if<PitchBendPerformanceEvent>(&event)) {
      pitchBends.push_back(pitchBend);
    }
  }
  expect(notes.size() == 5 && notes[0]->key == 74.0 && notes[1]->key == 72.0 && notes[2]->key == 73.0 &&
             notes[3]->key == 74.0 && notes[4]->key == 24.0 && notes[0]->durationTicks == 3 &&
             notes[1]->durationTicks == 7 && notes[2]->durationTicks == 7 && notes[3]->durationTicks == 7 &&
             notes[4]->durationTicks == 1 && notes[0]->linearVelocity < 1.0,
         "GX should retain its encoded duration across a rest while keeping live duration and drum fallback distinct");
  expect(balances.size() == 2 && std::abs(balances[0]->leftGain - 1.0) < 0.0001 &&
             std::abs(balances[0]->rightGain) < 0.0001 && balances[1]->leftGain > balances[1]->rightGain,
         "encoded sequence and drum pan bytes should use their low-nibble positions");
  expect(std::ranges::any_of(
             reverbs, [](const ReverbPerformanceEvent* reverb) { return std::abs(reverb->send - 1.0) < 0.0001; }),
         "D2's two loudness nibbles should preserve the K054539 reverb gain");
  expect(
      std::ranges::none_of(pitchBends, [](const PitchBendPerformanceEvent* bend) { return bend->header.tick < 94; }) &&
          std::ranges::any_of(pitchBends,
                              [](const PitchBendPerformanceEvent* bend) {
                                return bend->header.tick == 94 && std::abs(bend->semitones - 1.0) < 0.0001;
                              }),
      "F2 should retune the following note without bending the voice that is already playing");
}

void konamiArcadeMysticDrumPitchSlidesUseTablePitch() {
  auto fixture = makeMysticWarriorFixture();
  writeBytes(fixture.bytes, 0x320,
             {
                 0xea, 0x80,                    // tempo
                 0xde, 0x01,                    // secondary percussion flag on
                 0x61,                          // primary percussion flag off
                 0x00, 0x60, 0x63, 0x7f,        // drum 0 for 96 ticks
                 0xf3, 0x0a, 0x23, 0x1f,        // slide to driver note 31
                 0xff,
             });

  std::vector<Diagnostic> diagnostics;
  const auto layout =
      findKonamiArcadeLayout(fixture.source, ByteReader(fixture.source.id, fixture.bytes), &diagnostics);
  expect(layout && diagnostics.empty() && layout->sequences.size() == 1,
         "MysticWarrior drum-slide fixture should produce a valid layout");

  const SequenceProgram program = decodeKonamiArcadeSequence(ByteReader(fixture.source.id, fixture.bytes), *layout,
                                                             layout->sequences[0], AssetId{1}, nullptr, &diagnostics);
  const PerformanceSequence performance =
      SequenceVm(LoopPolicy::PlayOnce).render(program, konamiArcadeSequenceDialect());
  expect(diagnostics.empty() && performance.diagnostics.empty() && performance.tracks.size() == 1,
         "MysticWarrior drum-slide fixture should render without diagnostics");

  const auto transition = std::ranges::find_if(performance.tracks[0].automations, [](const auto& automation) {
    return pitchTransitionIntent(automation) != nullptr;
  });
  expect(transition != performance.tracks[0].automations.end(),
         "F3 after a MysticWarrior drum note should retain its pitch transition");
  const PitchTransitionIntent& slide = *pitchTransitionIntent(*transition);
  expect(std::abs(slide.startKey - 24.0) < 0.0001 && std::abs(slide.targetKey - 11.0) < 0.0001,
         "MysticWarrior drum F3 should slide from the table pitch toward the target instead of from the selector key");

  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  expect(std::ranges::any_of(midi.tracks[0].events, [](const MidiEvent& event) {
    const auto* bend = std::get_if<PitchBend>(&event);
    return bend != nullptr && bend->value < 0;
  }), "MIDI lowering should preserve the drum slide's downward direction");
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
