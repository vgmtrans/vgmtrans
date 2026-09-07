/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SoftCreatSnes/SoftCreatSnes.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::softcreat_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

std::array<u16, kTrackCount> pointerColumns(u16 first, u16 stride) {
  std::array<u16, kTrackCount> columns{};
  for (u32 track = 0; track < kTrackCount; ++track) {
    columns[track] = static_cast<u16>(first + track * stride);
  }
  return columns;
}

void writeModernPointerColumns(std::vector<u8>& bytes, u8 songCount,
                               const std::array<u16, kTrackCount>& lowColumns, u16 highOffset) {
  bytes[0x0102] = songCount;
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u32 highLoad = 0x0100 + 8 + track * 17;
    const u32 lowLoad = highLoad + 7;
    bytes[highLoad] = 0xf6;  // MOV A,!abs+Y
    bytes[lowLoad] = 0xf6;
    writeLe16(bytes, highLoad + 1, static_cast<u16>(lowColumns[track] + highOffset));
    writeLe16(bytes, lowLoad + 1, lowColumns[track]);
  }
}

void writeSplitPointer(std::vector<u8>& bytes, u16 lowColumn, u16 highColumn, u8 song, u16 address) {
  bytes[lowColumn + song] = static_cast<u8>(address);
  bytes[highColumn + song] = static_cast<u8>(address >> 8);
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

PerformanceSequence render(const std::vector<u8>& commands, Version version = Version::V2) {
  std::vector<u8> bytes(kAramSize);
  constexpr u16 start = 0x1000;
  std::ranges::copy(commands, bytes.begin() + start);
  for (u32 note = 0; note < 0x80; ++note) {
    const u16 pitch = static_cast<u16>(
        std::clamp(std::lround(0x1000 * std::exp2((static_cast<double>(note) - 62.0) / 12.0)), 1l, 0xffffl));
    bytes[0x0200 + note] = static_cast<u8>(pitch);
    bytes[0x0280 + note] = static_cast<u8>(pitch >> 8);
  }
  Layout layout{
      .version = version,
      .initialTimer = 0x84,
      .musicVolume = 0x80,
      .pitchLowTableAddress = 0x0200,
      .pitchHighTableAddress = 0x0280,
      .coarseTableAddress = 0x0300,
      .fineTableAddress = 0x0400,
      .envelopeTableAddress = 0x0500,
  };
  const ByteReader reader(SourceId{301}, bytes);
  SequenceProgram program = sequenceConfig().makeProgram();
  program.runtime = sequenceRuntime(RetainedSource::copyOf(reader), layout);
  program.tracks.push_back(decodeSourceTrack(reader, layout, 0, start));
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

// Synthetic ARAM made from the scanner's minimal instruction shapes. It is not
// an SPC dump: every address and data value is fabricated, with no song or BRR payload.
std::vector<u8> modernScannerFixture() {
  std::vector<u8> bytes(kAramSize);

  // Sequence loader: compare the song index, then read high and low pointer
  // bytes through MOV A,!abs+Y. Remaining track loads are generated below.
  writeBytes(bytes, 0x0100,
             {0x7d, 0x68, 0x05, 0xb0, 0xfa, 0xfd, 0xcd, 0x00, 0xf6, 0x00, 0x00, 0xf0, 0x0a,
              0xc4, 0x31, 0xf6, 0x00, 0x00, 0xc4, 0x30, 0x3f, 0x38, 0x06, 0x3d, 0x3d});
  const auto columns = pointerColumns(0x2000, 0x10);
  writeModernPointerColumns(bytes, 5, columns, 5);

  // Command dispatcher: reject opcodes at the cutoff, then indirect through
  // the handler table at $1000.
  writeBytes(bytes, 0x0200,
             {0x3f, 0x85, 0x07, 0x10, 0x1d, 0x68, 0xc7, 0xb0, 0x0b, 0x1c,
              0xfd, 0xf6, 0x70, 0x0b, 0x2d, 0xf6, 0x00, 0x10, 0x2d, 0x6f});

  // Instrument coarse tuning lookup at $4200.
  writeBytes(bytes, 0x0300,
             {0xfb, 0x20, 0x60, 0x96, 0x00, 0x42, 0x5b, 0xc0, 0xb0, 0x04,
              0x60, 0x95, 0x40, 0x02, 0xd5, 0xb0, 0x02, 0xd5, 0x90, 0x02});

  // Note-pitch lookups at $4000/$4055 and fine tuning at $4100.
  writeBytes(bytes, 0x0340,
             {0xf6, 0x00, 0x40, 0xc4, 0xd9, 0xf6, 0x55, 0x40, 0xc4, 0xda, 0xfb, 0x20, 0xf6,
              0x00, 0x41, 0xfd, 0x6d, 0xe4, 0xd9, 0xcf, 0xcb, 0xdd, 0xee, 0xe4, 0xda, 0xcf,
              0x8f, 0x00, 0xde, 0x7a, 0xdd, 0x7a, 0xd9});

  // Immediate loads form the envelope-table address $4300.
  writeBytes(bytes, 0x0400, {0xe8, 0x00, 0xc4, 0xd9, 0xe8, 0x43, 0xc4, 0xda});

  // Timer target $92, followed by the DSP register IDs and their fabricated,
  // zero-initialized parallel values. DIR selects page $44.
  writeBytes(bytes, 0x0480, {0x8f, 0x92, 0xfc, 0x8f, 0x04, 0xf1});
  writeBytes(bytes, 0x0500,
             {0x2c, 0x3c, 0x5c, 0x2d, 0x3d, 0x4d, 0x7d, 0x6d, 0x0d, 0x5d, 0x0f, 0x1f, 0x2f, 0x3f,
              0x4f, 0x5f, 0x6f, 0x7f, 0x05, 0x15, 0x25, 0x35, 0x45, 0x55, 0x65, 0x75, 0xff});
  bytes[0x0500 + 27 + 9] = 0x44;

  // Minimal live state: song 2 contains one terminated track and one BRR sample.
  bytes[0xe4] = 2;
  bytes[0xe8] = 0x70;
  writeSplitPointer(bytes, columns[0], columns[0] + 5, 2, 0x3000);
  bytes[0x3000] = 0x80;
  writeLe16(bytes, 0x4400, 0x4500);
  writeLe16(bytes, 0x4402, 0x4500);
  bytes[0x4500] = 1;
  return bytes;
}

// Synthetic V1 layout. Each byte run is the minimum scanner signature for the
// named operation; operands and data addresses are intentionally fabricated.
std::vector<u8> v1ScannerFixture() {
  std::vector<u8> bytes(kAramSize);

  // Sequence loader: read three adjacent 32-song pointer columns at
  // $2000/$2020/$2040.
  writeBytes(bytes, 0x0100,
             {0x3f, 0x00, 0x06, 0xf5, 0x00, 0x20, 0xc4, 0x30, 0xf5,
              0x20, 0x20, 0xc4, 0x31, 0xf5, 0x40, 0x20, 0xc4, 0x32});

  // V1 dispatch indexes the handler table at $1000 with even opcodes.
  writeBytes(bytes, 0x0200,
             {0x3f, 0x00, 0x0a, 0x10, 0x0a, 0xfd, 0xf6, 0x01,
              0x10, 0x2d, 0xf6, 0x00, 0x10, 0x2d, 0x6f});

  // Instrument coarse tuning lookup at $5084.
  writeBytes(bytes, 0x0300,
             {0xfb, 0x20, 0x60, 0x96, 0x84, 0x50, 0x5b, 0xc0,
              0xb0, 0x04, 0x60, 0x95, 0x40, 0x02});
  // Note-pitch lookups at $4000/$4055 and fine tuning at $50a4.
  writeBytes(bytes, 0x0340,
             {0xf6, 0x00, 0x40, 0xc4, 0xd6, 0xf6, 0x55, 0x40, 0xc4, 0xd7, 0xfb,
              0x20, 0xf6, 0xa4, 0x50, 0xfd, 0x6d, 0xe4, 0xd6, 0xcf, 0xcb, 0xd8,
              0xee, 0xe4, 0xd7, 0xcf, 0x8f, 0x00, 0xd9, 0x7a, 0xd8, 0x7a, 0xd6});
  // Envelope lookup at $1090; timer $42 is doubled by the V1 driver.
  writeBytes(bytes, 0x0400, {0xfb, 0x61, 0xf6, 0x90, 0x10, 0xd4, 0xa0});
  writeBytes(bytes, 0x0480, {0x8f, 0x42, 0xfa, 0x8f, 0x81, 0xf1});

  // A nearby one-track song must beat a more populated but distant candidate.
  constexpr u8 selectedSong = 5;
  constexpr u8 distantSong = 6;
  writeSplitPointer(bytes, 0x2000, 0x2020, selectedSong, 0x3000);
  writeLe16(bytes, 0x30, 0x3005);
  bytes[0x3000] = 0x40;
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u16 competing = static_cast<u16>(0x6000 + track * 0x20);
    const u16 lowColumn = static_cast<u16>(0x2000 + track * 0x40);
    writeSplitPointer(bytes, lowColumn, lowColumn + 0x20, distantSong, competing);
    bytes[competing] = 0x40;
  }
  return bytes;
}

void modernLayoutFindsRelocatedTables() {
  const std::vector<u8> bytes = modernScannerFixture();
  const auto layout = findLayout(ByteReader(SourceId{302}, bytes));
  expect(layout && layout->version == Version::V6 && layout->songIndex == 2 &&
             layout->tracks[0].address == 0x3000 && layout->pitchLowTableAddress == 0x4000 &&
             layout->pitchHighTableAddress == 0x4055 && layout->coarseTableAddress == 0x4200 &&
             layout->fineTableAddress == 0x4100 && layout->envelopeTableAddress == 0x4300 &&
             layout->spcDirAddress == 0x4400 && layout->initialTimer == 0x92 && layout->musicVolume == 0x70,
         "SoftCreatSnes layout should use the live song and recover every relocated driver table");
}

void v5LayoutUsesOverlappingPointerColumns() {
  auto bytes = modernScannerFixture();
  bytes[0x0206] = 0xb9;
  bytes[0xe4] = 1;
  std::fill(bytes.begin() + 0x2000, bytes.begin() + 0x2050, 0);

  // This generation packs its low/high pointer columns at irregular,
  // overlapping offsets. Song 1 points track 0 at our synthetic terminator.
  const std::array<u16, kTrackCount> lowColumns{0x2000, 0x2006, 0x200a, 0x200e,
                                                0x2012, 0x2016, 0x201a, 0x201e};
  writeModernPointerColumns(bytes, 4, lowColumns, 2);
  writeSplitPointer(bytes, lowColumns[0], lowColumns[0] + 2, 1, 0x3000);
  bytes[0x3000] = 0x40;

  const auto layout = findLayout(ByteReader(SourceId{305}, bytes));
  expect(layout && layout->version == Version::V5 && layout->songIndex == 1 &&
             layout->tracks[0].address == 0x3000,
         "V5 should use its overlapping pointer columns and $B9 cutoff");
}

void v7LayoutAlignsPrefixedDspValues() {
  auto bytes = modernScannerFixture();
  bytes[0x0206] = 0xc3;
  bytes[0xe4] = 7;
  bytes[0xe6] = 0x7f;
  bytes[0xe8] = 0;

  // Three DSP register IDs precede the regular register table. Their matching
  // values precede its values too, so DIR remains aligned at index 9.
  writeBytes(bytes, 0x04fd, {0x6c, 0x0c, 0x1c});
  bytes[0x0500 + 30 + 9] = 0xff;

  const auto columns = pointerColumns(0x2000, 4);
  std::fill(bytes.begin() + 0x2000, bytes.begin() + 0x2020, 0);
  writeModernPointerColumns(bytes, 2, columns, 2);
  writeSplitPointer(bytes, columns[0], columns[0] + 2, 0, 0x3000);
  writeSplitPointer(bytes, columns[0], columns[0] + 2, 1, 0x3100);
  bytes[0x3000] = 0x80;
  bytes[0x3100] = 0x40;

  // V7 track init copies the live music volume into track state.
  writeBytes(bytes, 0x0638, {0xe4, 0xe6, 0xd5, 0x66, 0x03});
  writeLe16(bytes, 0x1000 + (0xaau - 0x80u) * 2u, 0x0600);

  const auto layout = findLayout(ByteReader(SourceId{306}, bytes));
  expect(layout && layout->version == Version::V7 && layout->songIndex == 1 &&
             layout->tracks[0].address == 0x3100 && layout->musicVolume == 0x7f &&
             layout->spcDirAddress == 0xff00,
         "V7 should retain echo commands and align prefixed DSP values");
}

void v1LayoutSelectsTheCurrentSequence() {
  const auto layout = findLayout(ByteReader(SourceId{307}, v1ScannerFixture()));
  expect(layout && layout->version == Version::V1 && layout->songIndex == 5 &&
             layout->tracks[0].address == 0x3000 && layout->initialTimer == 0x84 &&
             layout->musicVolume == 0x100 && layout->pitchLowTableAddress == 0x4000 &&
             layout->pitchHighTableAddress == 0x4055 && layout->coarseTableAddress == 0x5084 &&
             layout->fineTableAddress == 0x50a4 && layout->envelopeTableAddress == 0x1090 &&
             layout->spcDirAddress == 0x5000,
         "the V1 layout should recover its current sequence, timer, tuning, envelope, and sample tables");
}

void instrumentAnnotationsReflectTheSynthModel() {
  Session session;
  session.registerFormat(module());
  const SourceId source =
      session.addSource(SourceFile{.name = "synthetic-softcreat.aram"}, modernScannerFixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  const auto* bank = snapshot.collections().empty() || snapshot.collections().front().members.soundBanks.empty()
                         ? nullptr
                         : snapshot.asset<SoundBankAsset>(snapshot.collections().front().members.soundBanks.front());
  expect(bank != nullptr && bank->instruments.size() == 1,
         "SoftCreatSnes scanning should publish its referenced instrument");

  const auto annotation = [&](const auto& ids, std::string_view category) -> const SourceAnnotation* {
    const auto found = std::ranges::find_if(ids, [&](SourceAnnotationId id) {
      return snapshot.sourceMap().get(id).category() == category;
    });
    return found == ids.end() ? nullptr : &snapshot.sourceMap().get(*found);
  };
  const SourceAnnotation* instrument =
      annotation(snapshot.sourceMap().ownedBy(ObjectRefs::instrument(bank->metadata.id, 0)),
                 "softcreat-snes-instrument");
  expect(instrument != nullptr, "SoftCreatSnes instruments should expose a source annotation");
  const auto tables = snapshot.sourceMap().annotationsForAsset(bank->metadata.id);
  const SourceAnnotation* coarseTable = annotation(tables, "softcreat-snes-coarse-tuning-table");
  const SourceAnnotation* fineTable = annotation(tables, "softcreat-snes-fine-tuning-table");
  expect(coarseTable != nullptr && coarseTable->range.offset == 0x4200 && coarseTable->range.size == 0x100 &&
             coarseTable->fields.size() == 0x100 && fineTable != nullptr && fineTable->range.offset == 0x4100 &&
             fineTable->range.size == 0x100 && fineTable->fields.size() == 0x100,
         "SoftCreatSnes should annotate every entry in both complete tuning tables");
  expect(instrument->parent == coarseTable->id,
         "referenced instruments should be rooted in the source table that defines their coarse tuning");
  expect(instrument->fieldsAsChildren &&
             std::ranges::count_if(instrument->fields, [](const SourceField& field) { return field.range.valid(); }) ==
                 2,
         "SoftCreatSnes instruments should expose their two source-backed tuning fields");
  expect(bank->instruments.front().regions.size() == 1 && !bank->instruments.front().regions.front().range.valid() &&
             snapshot.sourceMap().ownedBy(ObjectRefs::region(bank->metadata.id, 0, 0)).empty(),
         "SoftCreatSnes's derived playable region should remain in the synth model without claiming source bytes");
  expect(std::ranges::all_of(instrument->fields, [&](const SourceField& field) {
           return !field.range.valid() || field.range.source == source;
         }),
         "SoftCreatSnes instrument fields should retain their source identity");
}

void versionedOpcodesRetainTheirRealOperandLengths() {
  std::vector<u8> bytes(kAramSize);
  writeBytes(bytes, 0x1000, {0xb9, 0, 4, 0x80});
  bytes[0x2000] = 0x20;
  Layout v5{.version = Version::V5, .noteAliasTableAddress = 0x2000};
  const TrackProgram v5Track = decodeSourceTrack(ByteReader(SourceId{303}, bytes), v5, 0, 0x1000);
  expect(v5Track.commands.size() == 2 && v5Track.commands.front().semantic == SequenceSemantic::Note,
         "V5 should decode a valid $B9 note alias before applying its $B9 cutoff");

  writeBytes(bytes, 0x1000, {0xa1, 1, 2, 3, 4, 5, 6, 7, 0x80});
  Layout v6c{.version = Version::V6c};
  const TrackProgram v6cTrack = decodeSourceTrack(ByteReader(SourceId{304}, bytes), v6c, 0, 0x1000);
  expect(v6cTrack.commands.size() == 2 && v6cTrack.commands.front().range.size == 8 &&
             v6cTrack.commands.front().semantic == SequenceSemantic::Envelope,
         "V6c A1 should be the seven-byte inline software envelope");

  writeBytes(bytes, 0x1000, {0xaa, 0x40, 0x80});
  Layout v6d{.version = Version::V6d};
  const TrackProgram v6dTrack = decodeSourceTrack(ByteReader(SourceId{305}, bytes), v6d, 0, 0x1000);
  expect(v6dTrack.commands.size() == 2 && v6dTrack.commands.front().range.size == 2 &&
             v6dTrack.commands.front().semantic == SequenceSemantic::Level,
         "V6d AA should consume a volume-decay factor, not toggle echo");

  const PerformanceSequence v1 = render({0x8c, 4, 0x92, 3, 0x20, 0x80}, Version::V1);
  const auto v1Notes = events<NotePerformanceEvent>(v1.tracks.front());
  expect(v1.diagnostics.empty() && v1Notes.size() == 1 && v1Notes.front()->durationTicks == 4,
         "V1 should decode its even-numbered commands as their later equivalents");
}

void physicalEffectsAndSoftwareGainRender() {
  const PerformanceSequence performance = render(
      {0x86, 8, 0xb3, 0x70, 0x80, 0xa2, 1, 0, 3, 0x7f, 3, 0x40, 3, 0x92, 2,
       0x8e, 0, 4, 7, 1, 0xaa, 0xac, 0x40, 0xad, 0xc0, 0xae, 0xe0,
       0xaf, 0x7f, 0, 0, 0, 0, 0, 0, 0, 0x80});
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto expression = events<ExpressionPerformanceEvent>(track);
  const auto modulation = events<ModulationPerformanceEvent>(track);
  const auto reverb = events<ReverbPerformanceEvent>(track);
  const auto balance = events<StereoBalancePerformanceEvent>(track);
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->durationTicks == 8,
         "the feature fixture should render one eight-tick note without diagnostics");
  expect(!expression.empty() && std::ranges::any_of(expression, [](const auto* event) {
           return event->linearGain < 0.01;
         }),
         "dynamic GAIN should emit its zero attack/release level");
  expect(std::ranges::any_of(modulation, [](const auto* event) {
           return event->target == ModulationPerformanceTarget::VibratoDepth && event->pitchDepthSemitones &&
                  *event->pitchDepthSemitones > 0.001 && event->context.shape &&
                  event->context.shape->waveform == LfoWaveform::Triangle &&
                  event->context.polarity == LfoPolarity::Bipolar && event->context.pitchRangeSemitones &&
                  event->context.pitchRangeSemitones->minimum < 0.0 &&
                  event->context.pitchRangeSemitones->maximum > 0.0 && event->context.cyclesPerTick &&
                  std::abs(*event->context.cyclesPerTick - 1.0 / 14.0) < 0.000001 &&
                  event->context.shape->samples.size() == 14;
         }),
         "vibrato should retain its bipolar physical range, stepped shape, and full direction interval");
  expect(reverb.size() >= 5 && reverb.back()->voiceMask == 1 && reverb.back()->leftGain &&
             *reverb.back()->leftGain == 0.5 && reverb.back()->rightGain && *reverb.back()->rightGain == -0.5 &&
             reverb.back()->feedback == -0.25 && reverb.back()->filterIndex == 0,
         "sequence echo commands should preserve signed DSP volumes, EON, feedback, and FIR presets");
  expect(!balance.empty() && balance.front()->leftGain != balance.front()->rightGain,
         "the driver's signed stereo mixer should remain a stereo-balance event");
}

void gainHoldContinuesTheCurrentEnvelope() {
  const PerformanceSequence performance =
      render({0xa2, 1, 0, 6, 50, 1, 50, 6, 0x3d, 1, 0x9f, 0x90, 16, 0x9c, 0x3c, 30, 0x80});
  const auto expression = events<ExpressionPerformanceEvent>(performance.tracks.front());
  const auto modulation = events<ModulationPerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && expression.size() >= 6 && expression[0]->header.tick == 0 &&
             expression[0]->linearGain == 0.0 && expression[1]->header.tick == 1 &&
             std::abs(expression[1]->linearGain - 10.0 / 127.0) < 0.000001 &&
             expression[5]->header.tick == 5 && std::abs(expression[5]->linearGain - 50.0 / 127.0) < 0.000001,
         "GAIN should attack on the note tick and keep advancing after retrigger is disabled");
  expect(modulation.empty(), "legato notes without vibrato should not emit redundant modulation resets");
}

void restsPreserveTheKeyedVoice() {
  const PerformanceSequence performance =
      render({0xa2, 1, 120, 1, 120, 1, 120, 14, 0x93, 5, 0x84, 2, 0x18, 20, 0x9f, 0x18, 10, 0x18, 10,
              0x93, 0, 0x18, 40, 0, 160, 0, 80, 0x85, 0x9e, 0x19, 1, 0x80});
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const MidiSequence midi = renderMidiSequence(performance);
  const auto heldNote = std::ranges::find_if(midi.tracks.front().events, [](const MidiEvent& event) {
    const auto* note = std::get_if<NoteDuration>(&event.payload);
    return note != nullptr && event.tick == 0 && note->duration == 640;
  });
  expect(performance.diagnostics.empty() && notes.size() == 13 && notes[4]->header.tick == 80 &&
             notes[4]->extendsPrevious && notes[5]->header.tick == 240 && notes[5]->extendsPrevious &&
             notes[6]->header.tick == 320 && notes[6]->extendsPrevious && notes[6]->note == notes[0]->note &&
             notes[12]->header.tick == 640 && !notes[12]->extendsPrevious && notes[12]->note != notes[0]->note &&
             heldNote != midi.tracks.front().events.end(),
         "rests should extend a keyed voice until a retriggering note sends KOFF/KON");

  const PerformanceSequence releasedRest =
      render({0xa2, 1, 120, 1, 120, 1, 120, 14, 0x93, 0, 0x18, 20, 0x93, 5, 0, 20, 0x80});
  const auto expression = events<ExpressionPerformanceEvent>(releasedRest.tracks.front());
  expect(std::ranges::any_of(expression, [](const auto* event) { return event->header.tick == 35; }),
         "rest durations should schedule software release just like note durations");
}

void durationModesLegatoAndRepeatsAreStateful() {
  const PerformanceSequence repeated = render({0x86, 4, 0x84, 2, 1, 0x85, 0x9f, 2, 0x80});
  const auto notes = events<NotePerformanceEvent>(repeated.tracks.front());
  expect(repeated.diagnostics.empty() && notes.size() == 3 && notes[0]->header.tick == 0 &&
             notes[1]->header.tick == 4 && notes[2]->header.tick == 8 && !notes[2]->restartsEnvelope,
         "persistent duration, repeat-stack counts, and retrigger suppression should share runtime state");

  const PerformanceSequence wrappedRepeat = render({0x86, 1, 0x84, 0, 1, 0x85, 0x80});
  expect(wrappedRepeat.diagnostics.empty() &&
             events<NotePerformanceEvent>(wrappedRepeat.tracks.front()).size() == 256,
         "a zero repeat byte should wrap through all 256 SPC700 counter values");

  const PerformanceSequence perNote =
      render({0x86, 3, 0xbf, 0, 0x20, 1, 0x40, 0xc0, 2, 0x80}, Version::V6);
  expect(perNote.diagnostics.empty() && events<NotePerformanceEvent>(perNote.tracks.front()).size() == 2 &&
             !events<StereoBalancePerformanceEvent>(perNote.tracks.front()).empty(),
         "late per-note volume mode should consume a suffix on rests and notes and affect the mixer");

  const PerformanceSequence polymorphicTail =
      render({0x84, 2, 0, 1, 0xbf, 0x86, 1, 1, 0x40, 0x86, 0, 0x85, 0x80}, Version::V6);
  const auto polymorphicNotes = events<NotePerformanceEvent>(polymorphicTail.tracks.front());
  expect(polymorphicTail.diagnostics.empty() &&
             std::ranges::count_if(polymorphicNotes, [](const auto* note) { return !note->extendsPrevious; }) == 2,
         "a byte that becomes a per-note suffix on a later pass should retain both control-flow interpretations");

  constexpr u16 subroutine = 0x1100;
  std::vector<u8> repeatedCalls(0x104);
  for (u32 offset = 0; offset < 60; offset += 3) {
    repeatedCalls[offset] = 0x82;
    writeLe16(repeatedCalls, offset + 1, subroutine);
  }
  repeatedCalls[60] = 0x80;
  writeBytes(repeatedCalls, subroutine - 0x1000, {0x32, 1, 0x83});
  const PerformanceSequence manyCalls = render(repeatedCalls, Version::V7);
  expect(manyCalls.diagnostics.empty() && events<NotePerformanceEvent>(manyCalls.tracks.front()).size() == 20,
         "repeated calls to one pattern should decode every return continuation");
}

void finiteRepeatsAreNotSongLoops() {
  const PerformanceSequence performance = render(
      {0x86, 1, 0x84, 4, 0x82, 0x0d, 0x10, 0x85, 2, 0x81, 0x08, 0x10, 0x80, 1, 0x83});
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && performance.tracks.front().endTick == 5 && notes.size() == 5,
         "finite SoftCreatSnes repeats should remain distinct from the following infinite song loop");
}

void perNoteVolumePrecedesLiteralDuration() {
  const PerformanceSequence performance =
      render({0xb9, 0x19, 100, 12, 0x80}, Version::V6c);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto balance = events<StereoBalancePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->durationTicks == 12 &&
             balance.size() == 1 && std::abs(balance.front()->rightGain - 100.0 / 256.0) < 0.000001,
         "per-note mode should read volume before a literal duration, matching the SPC700 driver");
}

void pitchEffectsRetainPhysicalTiming() {
  const PerformanceSequence detuned = render({0x8d, 10, 0x34, 1, 0x80});
  const auto detuneBends = events<PitchBendPerformanceEvent>(detuned.tracks.front());
  expect(detuneBends.size() == 1 && detuneBends.front()->semitones > 0.0 && detuneBends.front()->semitones < 0.2,
         "detune should carry from the DSP pitch low byte into its high byte");

  const PerformanceSequence repeatedInstrument = render({0x89, 25, 0x32, 1, 0x89, 25, 0x32, 1, 0x80});
  expect(events<InstrumentPerformanceEvent>(repeatedInstrument.tracks.front()).size() == 2,
         "selecting the current SRCN should not emit another program change");

  const PerformanceSequence portamento = render({0x86, 4, 0x32, 0x90, 0x40, 0x3e, 0x80});
  expect(portamento.diagnostics.empty() &&
             std::ranges::any_of(portamento.tracks.front().automations, [](const PerformanceAutomation& automation) {
               const auto* slide = std::get_if<PitchTransitionIntent>(&automation.intent);
               return slide != nullptr && slide->timing.timelineTicks == 32 && slide->targetKey - slide->startKey > 11.9;
             }),
         "raw-pitch portamento should retain the driver's fixed step rate as a physical pitch transition");

  const PerformanceSequence retriggered =
      render({0x3b, 1, 0x90, 16, 0x9f, 0x3d, 29, 0x90, 8, 0x84, 2, 0x3d, 10, 0x3e, 5, 0x85, 0x9e, 0x90, 0,
              0x3b, 30, 0x80});
  expect(retriggered.diagnostics.empty() &&
             std::ranges::any_of(retriggered.tracks.front().automations, [](const auto& automation) {
               const auto* slide = std::get_if<PitchTransitionIntent>(&automation.intent);
               return slide != nullptr && automation.header.tick == 55 && automation.realization.endTick == 60 &&
                      automation.realization.endReason == PerformanceAutomationEndReason::Interrupted &&
                      !slide->continuesAcrossNotes;
             }),
         "a new attack should cancel the preceding legato portamento instead of bending the fresh note");

  const PerformanceSequence trill = render({0x86, 8, 0x96, 12, 2, 3, 0x32, 0x80});
  const auto bends = events<PitchBendPerformanceEvent>(trill.tracks.front());
  expect(trill.diagnostics.empty() && std::ranges::any_of(bends, [](const auto* event) {
           return event->header.tick == 3 && event->semitones > 11.9;
         }),
         "trill should begin with the low-phase duration and then reach its high pitch");
}

}  // namespace

void runSoftCreatSnesModuleTests() {
  modernLayoutFindsRelocatedTables();
  v5LayoutUsesOverlappingPointerColumns();
  v7LayoutAlignsPrefixedDspValues();
  v1LayoutSelectsTheCurrentSequence();
  instrumentAnnotationsReflectTheSynthModel();
  versionedOpcodesRetainTheirRealOperandLengths();
  physicalEffectsAndSoftwareGainRender();
  gainHoldContinuesTheCurrentEnvelope();
  restsPreserveTheKeyedVoice();
  durationModesLegatoAndRepeatsAreStateful();
  finiteRepeatsAreNotSongLoops();
  perNoteVolumePrecedesLiteralDuration();
  pitchEffectsRetainPhysicalTiming();
}
