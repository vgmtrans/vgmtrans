/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"

#include "value/export/Export.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/formats/ValueFormats.h"

#include "ValueFormatTestSupport.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::capcom_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string hexAddress(u64 value) {
  std::ostringstream out;
  out << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << value;
  return out.str();
}

std::string snapshotNumber(double value) {
  std::ostringstream out;
  out << std::setprecision(9) << value;
  return out.str();
}

std::string semanticValueSnapshot(const SemanticOperandValue& value) {
  return std::visit(
      [](const auto& typedValue) {
        using T = std::decay_t<decltype(typedValue)>;
        if constexpr (std::is_same_v<T, bool>) {
          return std::string(typedValue ? "true" : "false");
        } else if constexpr (std::is_same_v<T, std::string>) {
          return typedValue;
        } else if constexpr (std::is_same_v<T, Address>) {
          return "@" + hexAddress(typedValue.value);
        } else if constexpr (std::is_same_v<T, double>) {
          return snapshotNumber(typedValue);
        } else {
          return std::to_string(typedValue);
        }
      },
      value);
}

std::string decodedTrackSnapshot(const TrackProgram& track) {
  std::string snapshot;
  for (const auto& command : track.commands) {
    if (!snapshot.empty()) {
      snapshot += '|';
    }
    snapshot += hexAddress(command.address.value) + ':' + std::to_string(command.opcode) + ':' +
                std::to_string(command.encodedSize);
    for (const auto& operand : command.operands) {
      snapshot += ',' + operand.name + '=' + semanticValueSnapshot(operand.value);
      if (operand.encodedValue) {
        snapshot += '<' + semanticValueSnapshot(*operand.encodedValue) + '>';
      }
    }
    snapshot += ",flow=" + std::to_string(static_cast<int>(command.flow.kind));
    if (command.flow.fallthrough) {
      snapshot += "->" + hexAddress(command.flow.fallthrough->value);
    }
    for (const Address target : command.flow.staticTargets) {
      snapshot += "=>" + hexAddress(target.value);
    }
  }
  return snapshot;
}

std::string performanceTrackSnapshot(const PerformanceTrack& track) {
  std::string snapshot;
  for (const auto& event : track.events) {
    if (!snapshot.empty()) {
      snapshot += '|';
    }
    std::visit(
        [&](const auto& typedEvent) {
          using T = std::decay_t<decltype(typedEvent)>;
          if constexpr (std::is_same_v<T, TempoPerformanceEvent>) {
            snapshot += "tempo@" + std::to_string(typedEvent.header.tick) + '=' +
                        std::to_string(typedEvent.microsecondsPerQuarter);
          } else if constexpr (std::is_same_v<T, InstrumentPerformanceEvent>) {
            snapshot +=
                "instrument@" + std::to_string(typedEvent.header.tick) + '=' +
                (typedEvent.sourceInstrument ? typedEvent.sourceInstrument->domain : "legacy") + ':' +
                std::to_string(typedEvent.sourceInstrument ? typedEvent.sourceInstrument->key : typedEvent.program);
          } else if constexpr (std::is_same_v<T, LevelPerformanceEvent>) {
            snapshot += "level@" + std::to_string(typedEvent.header.tick) + '=' +
                        snapshotNumber(typedEvent.linearGain) + "/q" +
                        std::to_string(typedEvent.sourceQuantization ? typedEvent.sourceQuantization->levels : 0);
          } else if constexpr (std::is_same_v<T, PanPerformanceEvent>) {
            snapshot += "pan@" + std::to_string(typedEvent.header.tick) + '=' +
                        snapshotNumber(typedEvent.stereoPosition) + ',' + snapshotNumber(typedEvent.linearGain);
          } else if constexpr (std::is_same_v<T, StereoBalancePerformanceEvent>) {
            snapshot += "balance@" + std::to_string(typedEvent.header.tick) + '=' +
                        snapshotNumber(typedEvent.leftGain) + ',' + snapshotNumber(typedEvent.rightGain);
          } else if constexpr (std::is_same_v<T, ModulationPerformanceEvent>) {
            snapshot += "mod@" + std::to_string(typedEvent.header.tick) + ':' +
                        std::to_string(static_cast<int>(typedEvent.target)) + '=' + snapshotNumber(typedEvent.amount);
          } else if constexpr (std::is_same_v<T, NotePerformanceEvent>) {
            snapshot += "note@" + std::to_string(typedEvent.header.tick) + '=' + snapshotNumber(typedEvent.key) + '/' +
                        std::to_string(typedEvent.durationTicks);
          } else if constexpr (std::is_same_v<T, ReverbPerformanceEvent>) {
            snapshot += "reverb@" + std::to_string(typedEvent.header.tick) + '=' + snapshotNumber(typedEvent.send);
          } else if constexpr (std::is_same_v<T, MonoModePerformanceEvent>) {
            snapshot += "mono@" + std::to_string(typedEvent.header.tick) + '=' + std::to_string(typedEvent.channels);
          } else {
            snapshot += "other@" + std::to_string(typedEvent.header.tick);
          }
        },
        event);
  }
  return snapshot;
}

const SourceAnnotation* annotationWithKind(const SourceMap& sourceMap, SourceId source, SourceRole role,
                                           std::string_view localKind) {
  const auto annotations = sourceMap.withRole(source, role);
  for (const SourceAnnotationId id : annotations) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    if (annotation.localKind == localKind) {
      return &annotation;
    }
  }
  return nullptr;
}

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeBe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value >> 8);
  bytes[offset + 1] = static_cast<u8>(value & 0xff);
}

template <size_t Size>
void writeBytes(std::vector<u8>& bytes, size_t offset, const std::array<u8, Size>& values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

constexpr std::array<u8, 16> kReadSongListPattern{0x1c, 0x5d, 0xf5, 0x03, 0x0e, 0xc4, 0xc0, 0xf5,
                                                  0x02, 0x0e, 0xc4, 0xc1, 0x04, 0xc0, 0xf0, 0xdd};
constexpr std::array<u8, 16> kReadBgmAddressPattern{0x6f, 0x3f, 0xef, 0x06, 0x8f, 0x0d, 0xa1, 0x8f,
                                                    0xaf, 0xa0, 0x3f, 0x82, 0x05, 0x8d, 0x00, 0xdd};
constexpr std::array<u8, 16> kDspRegInitPattern{0x8d, 0x03, 0xf6, 0x63, 0x04, 0xc5, 0xf2, 0x00,
                                                0xf6, 0x66, 0x04, 0xc5, 0xf3, 0x00, 0xfe, 0xf2};
constexpr std::array<u8, 15> kDspRegInitOldPattern{0xf5, 0xf9, 0x0b, 0xfd, 0xf5, 0x05, 0x0c, 0x3f,
                                                   0xf2, 0x0b, 0x3d, 0xc8, 0x0c, 0xd0, 0xf1};
constexpr std::array<u8, 12> kLoadInstrTablePattern{0x8d, 0x06, 0xcf, 0xda, 0xa0, 0x60,
                                                    0x98, 0xac, 0xa0, 0x98, 0x47, 0xa1};

void addSongListReader(std::vector<u8>& bytes, u16 songListAddress) {
  writeBytes(bytes, 0x0400, kReadSongListPattern);
  writeLe16(bytes, 0x0403, songListAddress);
  writeLe16(bytes, 0x0408, songListAddress);
}

void addFixedBgmReader(std::vector<u8>& bytes, u16 bgmHeaderAddress) {
  writeBytes(bytes, 0x0500, kReadBgmAddressPattern);
  bytes[0x0505] = static_cast<u8>(bgmHeaderAddress >> 8);
  bytes[0x0508] = static_cast<u8>(bgmHeaderAddress & 0xff);
}

void addValidBgmHeader(std::vector<u8>& bytes, u16 headerAddress, u16 trackAddress = 0x3000) {
  for (u32 track = 0; track < kCapcomSnesMaxTracks; ++track) {
    writeBe16(bytes, headerAddress + 1 + track * 2, trackAddress);
  }
}

void addModernDspInit(std::vector<u8>& bytes, u8 dirPage) {
  writeBytes(bytes, 0x0700, kDspRegInitPattern);
  bytes[0x0701] = 1;
  writeLe16(bytes, 0x0703, 0x0800);
  writeLe16(bytes, 0x0709, 0x0810);
  bytes[0x0801] = 0x5d;
  bytes[0x0811] = dirPage;
}

void addOldDspInit(std::vector<u8>& bytes, u8 dirPage) {
  writeBytes(bytes, 0x0700, kDspRegInitOldPattern);
  bytes[0x070c] = 1;
  writeLe16(bytes, 0x0701, 0x0800);
  writeLe16(bytes, 0x0705, 0x0810);
  bytes[0x0800] = 0x5d;
  bytes[0x0810] = dirPage;
}

std::vector<u8> makeCapcomSnesAram() {
  std::vector<u8> bytes(0x10000);

  addFixedBgmReader(bytes, 0x2000);
  writeBytes(bytes, 0x0600, kLoadInstrTablePattern);
  bytes[0x0600 + 7] = 0x00;
  bytes[0x0600 + 10] = 0x40;
  addModernDspInit(bytes, 0x50);
  addValidBgmHeader(bytes, 0x2000);

  bytes[0x3000] = 0x05;
  bytes[0x3001] = 0x12;
  bytes[0x3002] = 0x34;
  bytes[0x3003] = 0x08;
  bytes[0x3004] = 0x00;
  bytes[0x3005] = 0x07;
  bytes[0x3006] = 0x40;
  bytes[0x3007] = 0x18;
  bytes[0x3008] = 0x00;
  bytes[0x3009] = 0x1a;
  bytes[0x300a] = 0x00;
  bytes[0x300b] = 0x20;
  bytes[0x300c] = 0x1a;
  bytes[0x300d] = 0x02;
  bytes[0x300e] = 0x20;
  bytes[0x300f] = 0x41;
  bytes[0x3010] = 0x17;

  bytes[0x4000] = 0x00;
  bytes[0x4001] = 0x8f;
  bytes[0x4002] = 0xe0;
  bytes[0x4003] = 0x00;
  writeBe16(bytes, 0x4004, 0x0100);

  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x6000] = 0x01;

  return bytes;
}

std::vector<u8> makeCapcomSnesSpc() {
  std::vector<u8> bytes(0x10180);
  constexpr std::string_view signature = "SNES-SPC700 Sound File Data";
  std::ranges::copy(signature, bytes.begin());
  bytes[0x21] = 0x1a;
  bytes[0x22] = 0x1a;
  bytes[0x23] = 0x1a;
  bytes[0x24] = 0x30;
  constexpr std::string_view title = "Capcom Logo";
  std::ranges::copy(title, bytes.begin() + 0x2e);

  const auto aram = makeCapcomSnesAram();
  std::ranges::copy(aram, bytes.begin() + 0x100);
  return bytes;
}

}  // namespace

void capcomSnesLayoutSelectsSongListAndFixedHeaders() {
  std::vector<u8> v1Bytes(0x10000);
  addSongListReader(v1Bytes, 0x1800);
  writeBe16(v1Bytes, 0x1800, 0x2200);
  addValidBgmHeader(v1Bytes, 0x2200);
  // Track zero maps to voice seven; make that the only active V1 cursor.
  v1Bytes[0x0f] = 0x10;
  v1Bytes[0x1f] = 0x30;

  const SourceId v1Source{17};
  const auto v1 = findCapcomSnesLayout(ByteReader(v1Source, v1Bytes));
  expect(v1.has_value(), "V1 song-list layout should be discovered");
  expect(v1->version == CapcomSnesEngineVersion::v1BgmInList, "song-list-only driver should be classified as V1");
  expect(v1->sequenceHeaderRange == SourceRange{.source = v1Source, .offset = 0x2200, .size = 17} &&
             v1->trackPointerTableAddress == 0x2201,
         "V1 layout should retain its priority byte and expose the pointer table directly");

  std::vector<u8> v2Bytes(0x10000);
  addSongListReader(v2Bytes, 0x1800);
  addFixedBgmReader(v2Bytes, 0x2000);
  addValidBgmHeader(v2Bytes, 0x2000);

  const auto v2 = findCapcomSnesLayout(ByteReader(SourceId{18}, v2Bytes));
  expect(v2.has_value() && v2->version == CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation,
         "driver with song-list and fixed-header readers should be classified as V2");
  expect(v2->sequenceHeaderRange.offset == 0x2001 && v2->sequenceHeaderRange.size == 16 &&
             v2->trackPointerTableAddress == 0x2001,
         "V2 layout should select its valid fixed header without exposing discovery flags");
}

void capcomSnesLayoutFallsBackToV2SongList() {
  std::vector<u8> bytes(0x10000);
  addSongListReader(bytes, 0x1800);
  addFixedBgmReader(bytes, 0x1000);  // The zero-filled header is invalid.
  writeBe16(bytes, 0x1800, 0x2200);
  addValidBgmHeader(bytes, 0x2200);
  // Later drivers store voice-seven cursor bytes at $07/$0F.
  bytes[0x07] = 0x10;
  bytes[0x0f] = 0x30;

  const auto layout = findCapcomSnesLayout(ByteReader(SourceId{19}, bytes));
  expect(layout.has_value() && layout->version == CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation,
         "V2 classification should survive an unusable fixed-header operand");
  expect(layout->sequenceHeaderRange.offset == 0x2200 && layout->sequenceHeaderRange.size == 17 &&
             layout->trackPointerTableAddress == 0x2201,
         "V2 should fall back to the song selected from current playback cursors");
}

void capcomSnesLayoutReadsOldAndRejectsMalformedDspInit() {
  std::vector<u8> oldBytes(0x10000);
  addFixedBgmReader(oldBytes, 0x2000);
  addValidBgmHeader(oldBytes, 0x2000);
  addOldDspInit(oldBytes, 0x50);

  const auto oldLayout = findCapcomSnesLayout(ByteReader(SourceId{20}, oldBytes));
  expect(oldLayout && oldLayout->spcDirAddress == 0x5000,
         "old DSP initialization table should provide the SPC DIR page");

  std::vector<u8> malformedBytes(0x10000);
  addFixedBgmReader(malformedBytes, 0x2000);
  addValidBgmHeader(malformedBytes, 0x2000);
  writeBytes(malformedBytes, 0x0700, kDspRegInitPattern);
  malformedBytes[0x0701] = 1;
  writeLe16(malformedBytes, 0x0703, 0xffff);
  writeLe16(malformedBytes, 0x0709, 0xffff);

  const auto malformedLayout = findCapcomSnesLayout(ByteReader(SourceId{21}, malformedBytes));
  expect(malformedLayout && !malformedLayout->spcDirAddress,
         "malformed DSP initialization table should not invalidate an otherwise usable sequence layout");
}

void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  expect(session.dialects().contains("capcom-snes"),
         "value format registration should include the CapcomSnes sequence dialect");
  const auto capcomModule = std::ranges::find_if(
      session.formats().modules(), [](const FormatModule& module) { return module.name == "CapcomSnes"; });
  expect(capcomModule != session.formats().modules().end() && capcomModule->canScan == nullptr,
         "CapcomSnes recognition should run once inside scan rather than through a duplicate probe");
  const SourceId source = session.addSource(SourceFile{.name = "Mega Man X.spc"}, makeCapcomSnesAram());

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  expect(project.diagnostics().empty(), "CapcomSnes scan should not report diagnostics for complete fixture");
  expect(project.collections().size() == 1, "CapcomSnes scan should produce one collection");
  expect(project.assets().size() == 3, "CapcomSnes scan should produce sequence, instrument set, and samples");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets()[0]);
  expect(sequence != nullptr, "first CapcomSnes asset should be sequence");
  expect(sequence->metadata.format == "CapcomSnes", "sequence should retain format name");
  expect(sequence->metadata.range.offset == 0x2001, "sequence range should point at fixed BGM header body");
  expect(sequence->program.dialect.value == "capcom-snes", "sequence should carry the CapcomSnes dialect family");
  expect(sequence->program.timebase.ppqn == 48, "sequence should use CapcomSnes PPQN");
  expect(sequence->program.behavior.defaultLoopPolicy == LoopPolicy::PlayOnce,
         "sequence should carry CapcomSnes default loop policy");
  expect(sequence->program.tracks.size() == 8, "sequence should decode all nonzero track pointers");
  const auto sequenceInspection = session.inspect(sequence->metadata.id);
  expect(sequenceInspection && sequenceInspection->range().offset == 0x2001 &&
             sequenceInspection->bytes().size() == 0x1010,
         "sequence inspection should cover its header and decoded tracks");

  const auto* dialect = session.dialects().find(sequence->program.dialect.value);
  expect(dialect != nullptr, "registered dialect should interpret the scanned sequence program");
  expect(dialect->execute != nullptr, "CapcomSnes should register a compiled command executor");
  const auto& firstTrack = sequence->program.tracks[0];
  expect(firstTrack.commands.size() == 8, "track should decode all fixture commands");
  constexpr std::array<std::string_view, 8> expectedCommandDetailKinds{
      "capcom-snes.tempo", "capcom-snes.instrument", "capcom-snes.volume", "capcom-snes.pan",
      "capcom-snes.lfo",   "capcom-snes.lfo",        "capcom-snes.note",   "capcom-snes.end",
  };
  for (size_t index = 0; index < expectedCommandDetailKinds.size(); ++index) {
    expect(commandDetailKind(project.sourceMap(), firstTrack.commands[index]) == expectedCommandDetailKinds[index],
           "track should decode command " + std::to_string(index));
  }
  expect(firstTrack.commands[0].opcode == 0x05, "CapcomSnes tempo should be a compiled command with source metadata");
  const SemanticOperand* tempo = semanticOperand(firstTrack.commands[0], "tempo");
  expect(tempo != nullptr && std::abs(std::get<double>(tempo->value) - 1422.10424024) < 0.000001 &&
             tempo->display == SourceValueDisplay::BeatsPerMinute && tempo->encodedValue &&
             std::get<u64>(*tempo->encodedValue) == 0x1234,
         "tempo command should retain readable BPM and its encoded value");
  expect(tempo->name == "tempo" && tempo->encodedName == "raw",
         "semantic operands should retain generic SourceMap presentation metadata");
  expect(tempo->range.offset == 0x3001 && tempo->range.size == 2,
         "typed operands should retain their exact source range");
  expect(firstTrack.commands[0].flow.fallthrough && firstTrack.commands[0].flow.fallthrough->value == 0x3003,
         "semantic commands should retain decode-time control flow");
  const SourceMap& sourceMap = project.sourceMap();
  const SourceAnnotation& programAnnotation = commandAnnotation(sourceMap, firstTrack.commands[1]);
  const auto programInstrumentLink = std::ranges::find_if(
      programAnnotation.links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesInstrument; });
  expect(programInstrumentLink != programAnnotation.links.end(),
         "program command should record its instrument reference as a source annotation link");
  const auto* programInstrument = std::get_if<ObjectRef>(&programInstrumentLink->target);
  expect(programInstrument != nullptr && programInstrument->kind == ObjectKind::InstrumentIndex &&
             !programInstrument->asset.valid() && programInstrument->index0 == 0,
         "program source link should preserve the unresolved source instrument identity");
  expect(programAnnotation.range.offset == 0x3003 && programAnnotation.range.size == 2,
         "program source link should be anchored by the program command annotation range");

  const auto commandAnnotations = sourceMap.withRole(source, SourceRole::Command);
  expect(commandAnnotations.size() == sequence->program.tracks.size() * sequence->program.tracks[0].commands.size(),
         "source map should expose decoded command annotations for every track");

  const auto trackAnnotations = sourceMap.withRole(source, SourceRole::SequenceTrack);
  expect(!trackAnnotations.empty(), "source map should expose track annotations");
  const SourceAnnotation& firstTrackAnnotation = sourceMap.get(trackAnnotations.front());
  expect(firstTrackAnnotation.owner == ObjectRefs::sequenceTrack(sequence->metadata.id, firstTrack.sourceTrackNumber),
         "track annotation should point at the semantic sequence track");
  expect(sourceMap.childrenOf(firstTrackAnnotation.id).size() == sequence->program.tracks[0].commands.size(),
         "track annotation should parent its decoded command annotations");

  const auto firstTempoAnnotationId = std::ranges::find_if(commandAnnotations, [&](SourceAnnotationId id) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    return annotation.detailKind == "capcom-snes.tempo";
  });
  expect(firstTempoAnnotationId != commandAnnotations.end(), "source map should expose typed command annotations");
  const SourceAnnotation& firstTempoAnnotation = sourceMap.get(*firstTempoAnnotationId);
  expect(firstTempoAnnotation.parent == firstTrackAnnotation.id, "command annotation should point back to its track");
  expect(firstTempoAnnotation.label == "Tempo", "command annotation should carry a readable command name");
  expect(firstTempoAnnotation.range.offset == 0x3000 && firstTempoAnnotation.range.size == 3,
         "command annotation should preserve command source range");

  const auto tempoAnnotations = project.sourceMap().withSequenceSemantic(source, SequenceSemantic::Tempo);
  expect(tempoAnnotations.size() == sequence->program.tracks.size(),
         "CapcomSnes scan should publish source annotations for decoded tempo commands");
  const SourceAnnotation& tempoAnnotation = project.sourceMap().get(tempoAnnotations.front());
  expect(tempoAnnotation.label == "Tempo" && tempoAnnotation.localKind == "tempo",
         "CapcomSnes tempo annotation should carry command display metadata");
  expect(tempoAnnotation.range.offset == 0x3000 && tempoAnnotation.range.size == 3,
         "CapcomSnes tempo annotation should use the exact decoded command range");
  const auto hasTempoField = [&](std::string_view name) {
    return std::ranges::any_of(tempoAnnotation.fields, [&](const SourceField& field) { return field.name == name; });
  };
  expect(hasTempoField("opcode") && hasTempoField("raw") && hasTempoField("tempo"),
         "CapcomSnes tempo annotation should record opcode, raw operand, and interpreted tempo");
  const auto* sequenceHeader = annotationWithKind(sourceMap, source, SourceRole::Header, "capcom-snes-sequence-header");
  expect(sequenceHeader != nullptr && sequenceHeader->range.offset == 0x2001 && sequenceHeader->range.size == 16,
         "CapcomSnes scan should annotate the sequence header");
  expect(sequenceHeader->owner == ObjectRefs::sequence(sequence->metadata.id),
         "sequence header annotation should point at the semantic sequence asset");
  const auto* trackPointer = annotationWithKind(sourceMap, source, SourceRole::Pointer, "capcom-snes-track-pointer");
  const auto trackPointers = sourceMap.withRole(source, SourceRole::Pointer);
  const auto capcomTrackPointerCount = std::ranges::count_if(
      trackPointers, [&](SourceAnnotationId id) { return sourceMap.get(id).localKind == "capcom-snes-track-pointer"; });
  expect(trackPointer != nullptr && trackPointer->range.size == 2 && capcomTrackPointerCount == 8,
         "CapcomSnes scan should annotate track pointer fields");
  expect(trackPointer->parent == sequenceHeader->id && !firstTrackAnnotation.parent,
         "track pointers should remain under the header while tracks are header siblings");
  expect(trackPointer->owner == firstTrackAnnotation.owner,
         "track pointer annotations should identify the semantic track they reference");
  const auto* instrumentTable =
      annotationWithKind(sourceMap, source, SourceRole::Table, "capcom-snes-instrument-table");
  expect(instrumentTable != nullptr && instrumentTable->range.offset == 0x4000 && instrumentTable->range.size == 6,
         "CapcomSnes scan should annotate the instrument table");
  const auto* instrumentRow = annotationWithKind(sourceMap, source, SourceRole::Instrument, "capcom-snes-instrument");
  expect(instrumentRow != nullptr && instrumentRow->range.offset == 0x4000 && instrumentRow->range.size == 6,
         "CapcomSnes scan should annotate parsed instrument rows");
  const auto instrumentSampleLink = std::ranges::find_if(
      instrumentRow->links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesSample; });
  expect(instrumentSampleLink != instrumentRow->links.end(),
         "CapcomSnes instrument annotations should link to the sample they use");
  const auto* sampleDir = annotationWithKind(sourceMap, source, SourceRole::Table, "snes-sample-dir");
  expect(sampleDir != nullptr && sampleDir->range.offset == 0x5000 && sampleDir->range.size == 4,
         "CapcomSnes scan should annotate the sample DIR table");
  const auto* sampleEntry = annotationWithKind(sourceMap, source, SourceRole::Sample, "snes-sample-dir-entry");
  expect(sampleEntry != nullptr && sampleEntry->range.offset == 0x5000 && sampleEntry->range.size == 4,
         "CapcomSnes scan should annotate sample DIR entries");
  const auto* samplePayload = annotationWithKind(sourceMap, source, SourceRole::Payload, "snes-brr-payload");
  expect(samplePayload != nullptr && samplePayload->range.offset == 0x6000 && samplePayload->range.size == 9,
         "CapcomSnes scan should annotate BRR sample payloads");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, *dialect);
  const auto instrumentEvent = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<InstrumentPerformanceEvent>(event);
  });
  expect(instrumentEvent != performance.tracks[0].events.end(),
         "CapcomSnes performance should select a source instrument");
  const auto& instrumentSelection = std::get<InstrumentPerformanceEvent>(*instrumentEvent);
  expect(instrumentSelection.sourceInstrument ==
                 InstrumentIdentity{.domain = std::string(kCapcomSnesInstrumentDomain), .key = 0} &&
             instrumentSelection.bank == 0 && instrumentSelection.program == 0 && !instrumentSelection.forceBankSelect,
         "CapcomSnes performance should not pre-encode target bank/program behavior");
  const auto levelEvent = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<LevelPerformanceEvent>(event);
  });
  expect(levelEvent != performance.tracks[0].events.end() &&
             std::get<LevelPerformanceEvent>(*levelEvent).sourceQuantization &&
             std::get<LevelPerformanceEvent>(*levelEvent).sourceQuantization->levels == 256,
         "CapcomSnes volume should retain neutral source quantization rather than a MIDI bit width");
  const MidiSequence midiSequence = renderMidiSequence(performance);
  expect(midiSequence.diagnostics.empty(), "CapcomSnes MIDI sequence build should not warn for linear fixture");
  expect(midiSequence.tracks.size() == 8, "builder should preserve track count");
  expect(midiSequence.tracks[0].events.size() == 15,
         "built track should include port, initial, command, and end events");
  expect(std::get<MidiPort>(midiSequence.tracks[0].events[0]).port == 0,
         "CapcomSnes should emit the legacy MIDI port metadata");
  expect(std::get<Reverb>(midiSequence.tracks[0].events[1]).value == 0,
         "CapcomSnes should emit the legacy initial reverb controller");
  expect(std::get<MonoMode>(midiSequence.tracks[0].events[2]).channels == 0,
         "CapcomSnes should emit the legacy initial mono-mode controller");
  expect(std::get<Tempo>(midiSequence.tracks[0].events[3]).microsecondsPerQuarter == 42191,
         "CapcomSnes source command should interpret tempo with driver timing math");
  expect(std::holds_alternative<BankSelect>(midiSequence.tracks[0].events[4]),
         "CapcomSnes source command should force bank select like the legacy converter");
  expect(std::holds_alternative<ProgramChange>(midiSequence.tracks[0].events[5]),
         "CapcomSnes source command should emit program changes");
  expect(std::holds_alternative<Volume14>(midiSequence.tracks[0].events[6]),
         "CapcomSnes source command should emit high-resolution target-quantized volume");
  expect(std::get<Pan>(midiSequence.tracks[0].events[7]).value == 64,
         "CapcomSnes center pan should map to MIDI center pan");
  expect(std::holds_alternative<Expression>(midiSequence.tracks[0].events[8]),
         "CapcomSnes pan should emit expression compensation for the source pan law");
  expect(std::get<VibratoDepth>(midiSequence.tracks[0].events[9]).value == 0,
         "CapcomSnes vibrato depth should stay silent until the LFO rate enables output");
  expect(std::get<VibratoDepth>(midiSequence.tracks[0].events[10]).value == 32,
         "CapcomSnes LFO rate should enable the latched vibrato depth");
  expect(std::holds_alternative<VibratoFrequency>(midiSequence.tracks[0].events[11]),
         "CapcomSnes LFO rate should emit vibrato frequency");
  expect(std::holds_alternative<TremoloFrequency>(midiSequence.tracks[0].events[12]),
         "CapcomSnes LFO rate should emit tremolo frequency");
  expect(std::get<NoteDuration>(midiSequence.tracks[0].events[13]).duration == 6,
         "CapcomSnes note length index should map to ticks");
  expect(std::get<EndOfTrack>(midiSequence.tracks[0].events[14]).tick == 6,
         "builder should advance time before end of track");

  const auto vibratoDepth = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    return modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoDepth &&
           modulation->amount > 0.0;
  });
  const auto vibratoRate = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    return modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoRate;
  });
  expect(vibratoDepth != performance.tracks[0].events.end() &&
             std::get<ModulationPerformanceEvent>(*vibratoDepth).pitchDepthSemitones == 3.0,
         "CapcomSnes vibrato depth should retain the driver's physical pitch range");
  expect(vibratoRate != performance.tracks[0].events.end() &&
             std::get<ModulationPerformanceEvent>(*vibratoRate).frequencyHz == 1.953125,
         "CapcomSnes vibrato rate should retain the driver's physical LFO frequency");

  const MidiSequence simulatedMidi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  expect(std::ranges::any_of(simulatedMidi.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* bend = std::get_if<PitchBend>(&event);
                               return bend != nullptr && bend->value != 0;
                             }),
         "CapcomSnes sequence-event modulation should render vibrato as nonzero pitch bends");

  const auto artifacts = session.exportCollection(
      project.collections()[0].id, ExportRequest{
                                       .kinds = {ExportKind::Midi},
                                       .loopPolicy = LoopPolicy::PlayOnce,
                                       .modulationConversion = ModulationConversionPolicy::SynthModulators,
                                   });
  expect(artifacts.size() == 1, "value export should produce one MIDI artifact");
  expect(artifacts[0].filename == "Mega Man X.mid", "MIDI artifact should use collection name");
  expect(artifacts[0].mediaType == "audio/midi", "MIDI artifact should use audio/midi media type");
  expect(artifacts[0].diagnostics.empty(), "MIDI artifact should not carry diagnostics for linear fixture");
  expect(artifacts[0].bytes == encodeMidiFile(midiSequence),
         "Session MIDI export should match direct builder/exporter output");

  const Artifact individualMidi =
      session.exportSequenceMidi(sequence->metadata.id, SequenceExportRequest{.loopPolicy = LoopPolicy::PlayOnce});
  const auto contextualMidi = session.exportCollection(
      project.collections()[0].id, ExportRequest{
                                       .kinds = {ExportKind::Midi},
                                       .loopPolicy = LoopPolicy::PlayOnce,
                                       .modulationConversion = ModulationConversionPolicy::SequenceEventSimulation,
                                   });
  expect(contextualMidi.size() == 1 && individualMidi.bytes == contextualMidi[0].bytes,
         "individual sequence export should use its first collection's prepared MIDI context");

  const auto wavArtifacts = session.exportCollection(project.collections()[0].id, ExportRequest{
                                                                                      .kinds = {ExportKind::Wav},
                                                                                  });
  expect(wavArtifacts.size() == 1, "value export should produce one WAV artifact for one sample");
  expect(wavArtifacts[0].filename == "Mega Man X-0-Sample 0.wav", "WAV artifact should include sample index and name");
  expect(wavArtifacts[0].mediaType == "audio/wav", "WAV artifact should use audio/wav media type");
  expect(wavArtifacts[0].diagnostics.empty(), "WAV artifact should not carry diagnostics for decodable sample");
  expect(wavArtifacts[0].bytes.size() == 76, "one BRR block should export as 44-byte header plus 32 PCM bytes");
  expect(std::vector<u8>(wavArtifacts[0].bytes.begin(), wavArtifacts[0].bytes.begin() + 4) ==
             std::vector<u8>{'R', 'I', 'F', 'F'},
         "WAV artifact should start with a RIFF header");
  expect(wavArtifacts[0].bytes[24] == 0x00 && wavArtifacts[0].bytes[25] == 0x7d,
         "WAV artifact should preserve the CapcomSnes sample rate");

  const auto sf2Artifacts = session.exportCollection(project.collections()[0].id, ExportRequest{
                                                                                      .kinds = {ExportKind::SoundFont2},
                                                                                  });
  expect(sf2Artifacts.size() == 1, "value export should produce one SoundFont artifact");
  expect(sf2Artifacts[0].filename == "Mega Man X.sf2", "SoundFont artifact should use collection name");
  expect(sf2Artifacts[0].mediaType == "audio/soundfont", "SoundFont artifact should use audio/soundfont media type");
  expect(sf2Artifacts[0].diagnostics.empty(), "SoundFont artifact should not carry diagnostics for complete fixture");
  expect(sf2Artifacts[0].bytes.size() > 44, "SoundFont artifact should contain RIFF bytes");
  expect(std::vector<u8>(sf2Artifacts[0].bytes.begin(), sf2Artifacts[0].bytes.begin() + 4) ==
             std::vector<u8>{'R', 'I', 'F', 'F'},
         "SoundFont artifact should start with a RIFF header");
  expect(std::vector<u8>(sf2Artifacts[0].bytes.begin() + 8, sf2Artifacts[0].bytes.begin() + 12) ==
             std::vector<u8>{'s', 'f', 'b', 'k'},
         "SoundFont artifact should use sfbk RIFF type");

  const auto dlsArtifacts = session.exportCollection(project.collections()[0].id, ExportRequest{
                                                                                      .kinds = {ExportKind::Dls},
                                                                                  });
  expect(dlsArtifacts.size() == 1, "value export should produce one DLS artifact");
  expect(dlsArtifacts[0].filename == "Mega Man X.dls", "DLS artifact should use collection name");
  expect(dlsArtifacts[0].mediaType == "audio/dls", "DLS artifact should use audio/dls media type");
  expect(dlsArtifacts[0].diagnostics.empty(), "DLS artifact should not carry diagnostics for complete fixture");
  expect(dlsArtifacts[0].bytes.size() > 44, "DLS artifact should contain RIFF bytes");
  expect(std::vector<u8>(dlsArtifacts[0].bytes.begin(), dlsArtifacts[0].bytes.begin() + 4) ==
             std::vector<u8>{'R', 'I', 'F', 'F'},
         "DLS artifact should start with a RIFF header");
  expect(std::vector<u8>(dlsArtifacts[0].bytes.begin() + 8, dlsArtifacts[0].bytes.begin() + 12) ==
             std::vector<u8>{'D', 'L', 'S', ' '},
         "DLS artifact should use DLS RIFF type");

  const auto* instruments = std::get_if<InstrumentSetAsset>(&project.assets()[1]);
  expect(instruments != nullptr, "second CapcomSnes asset should be instrument set");
  const Artifact individualSf2 =
      session.exportInstrumentSet(instruments->metadata.id, SynthExportFormat::SoundFont2, ExportRequest{});
  const Artifact individualDls =
      session.exportInstrumentSet(instruments->metadata.id, SynthExportFormat::Dls, ExportRequest{});
  expect(individualSf2.bytes == sf2Artifacts[0].bytes && individualDls.bytes == dlsArtifacts[0].bytes,
         "individual instrument export should use its first collection's complete synth context");
  expect(instruments->instruments.size() == 1, "instrument set should parse one valid instrument");
  const auto& instrument = instruments->instruments[0];
  expect(instrument.identity == InstrumentIdentity{.domain = std::string(kCapcomSnesInstrumentDomain), .key = 0},
         "instrument should preserve its source-domain identity without target addressing");
  expect(instrument.range.offset == 0x4000 && instrument.range.size == 6,
         "instrument should preserve the table entry source range");
  expect(instrument.regions.size() == 1, "instrument should expose one region");
  expect(instrument.regions[0].range.offset == 0x4000 && instrument.regions[0].range.size == 6,
         "region should preserve the instrument header source range");
  const auto& envelope = instrument.regions[0].envelope;
  expect(envelope.attackSeconds && std::abs(*envelope.attackSeconds - 0.0000625) < 0.000001,
         "instrument envelope should convert SNES attack to seconds");
  expect(envelope.decaySeconds && std::isinf(*envelope.decaySeconds),
         "instrument envelope should preserve infinite SNES sustain decay");
  expect(envelope.sustainAmplitude == 1.0, "instrument envelope should convert SNES sustain to linear amplitude");
  expect(envelope.releaseSeconds == 0.0, "instrument envelope should match Capcom legacy gain-based release handling");
  expect(instrument.modulation.vibrato.has_value(), "instrument should describe Capcom vibrato");
  expect(instrument.modulation.vibrato->maxDepthCents == 1200.0 &&
             instrument.modulation.vibrato->rateHertz.minimum == kCapcomSnesLfoStepHertz &&
             instrument.modulation.vibrato->rateHertz.maximum == 255.0 * kCapcomSnesLfoStepHertz,
         "instrument should preserve the Capcom vibrato range in physical units");
  expect(instrument.modulation.tremolo.has_value(), "instrument should describe Capcom tremolo");
  expect(instrument.modulation.tremolo->maxDepthDb == kCapcomSnesTremoloHalfDepthCentibels / 10.0 &&
             instrument.modulation.tremolo->rateHertz.minimum == 2.0 * kCapcomSnesLfoStepHertz &&
             instrument.modulation.tremolo->rateHertz.maximum == 510.0 * kCapcomSnesLfoStepHertz &&
             instrument.modulation.tremolo->gainMode == TremoloGainMode::NoBoost,
         "instrument should preserve the Capcom no-boost tremolo range in physical units");

  const auto instrumentAnnotations = sourceMap.ownedBy(ObjectRefs::instrument(instruments->metadata.id, 0));
  expect(!instrumentAnnotations.empty(), "instrument set source map should expose instrument annotations");
  const SourceAnnotation& instrumentAnnotation = sourceMap.get(instrumentAnnotations.front());
  const auto instrumentChildren = sourceMap.childrenOf(instrumentAnnotation.id);
  const auto regionAnnotationId = std::ranges::find_if(instrumentChildren, [&](SourceAnnotationId id) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    return annotation.role == SourceRole::Region && annotation.localKind == "capcom-snes-region";
  });
  expect(regionAnnotationId != instrumentChildren.end(), "instrument set source map should expose region annotations");
  const SourceAnnotation& regionAnnotation = sourceMap.get(*regionAnnotationId);
  expect(regionAnnotation.parent == instrumentAnnotation.id, "region annotation should point back to its instrument");
  expect(regionAnnotation.owner == ObjectRefs::region(instruments->metadata.id, 0, 0),
         "region annotation should identify its durable instrument and region indexes");
  expect(regionAnnotation.range.offset == 0x4000 && regionAnnotation.range.size == 6,
         "region annotation should preserve the instrument header source range");
  const auto adsrAnnotationId = std::ranges::find_if(instrumentChildren, [&](SourceAnnotationId id) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    return annotation.role == SourceRole::DataBlock && annotation.localKind == "capcom-snes-adsr-gain";
  });
  expect(adsrAnnotationId != instrumentChildren.end(), "instrument source map should expose ADSR/Gain documentation");
  const SourceAnnotation& adsrAnnotation = sourceMap.get(*adsrAnnotationId);
  expect(adsrAnnotation.outline == SourceOutlinePolicy::Show,
         "ADSR/Gain annotation should be forced visible for Tree View consumers");
  expect(adsrAnnotation.range.offset == 0x4001 && adsrAnnotation.range.size == 3,
         "ADSR/Gain annotation should preserve the raw byte range");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets()[2]);
  expect(samples != nullptr, "third CapcomSnes asset should be sample collection");
  expect(samples->samples.samples.size() == 1, "sample collection should include referenced sample");
  expect(samples->samples.samples[0].codec == AudioCodec::SnesBrr, "sample should preserve BRR codec");
  expect(samples->samples.samples[0].encodedData.offset == 0x6000, "sample should point at encoded BRR bytes");
  expect(samples->samples.samples[0].encodedData.size == 9, "sample should preserve encoded BRR byte length");

  const auto* sampleCollectionAnnotation = annotationWithKind(sourceMap, source, SourceRole::Table, "snes-sample-dir");
  expect(sampleCollectionAnnotation != nullptr, "sample collection source map should expose the sample DIR root");
  expect(sampleCollectionAnnotation->range.offset == 0x5000 && sampleCollectionAnnotation->range.size == 4,
         "sample collection root should preserve the DIR table source range");
  const auto* sampleAnnotation = annotationWithKind(sourceMap, source, SourceRole::Payload, "snes-brr-payload");
  expect(sampleAnnotation != nullptr, "sample collection source map should expose sample payload annotations");
  expect(sampleAnnotation->range.offset == 0x6000 && sampleAnnotation->range.size == 9,
         "sample payload node should preserve the encoded BRR source range");

  expect(project.collections()[0].sequence == sequence->metadata.id, "collection should reference sequence");
  expect(project.collections()[0].instrumentSets == std::vector<AssetId>{instruments->metadata.id},
         "collection should reference instrument set");
  expect(project.collections()[0].sampleCollections == std::vector<AssetId>{samples->metadata.id},
         "collection should reference sample collection");
}

void capcomSnesModuleWarnsWhenDetectedSynthIsEmpty() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  auto bytes = makeCapcomSnesAram();
  std::fill(bytes.begin() + 0x4000, bytes.begin() + 0x4006, 0);
  session.addSource(SourceFile{.name = "Empty Synth.spc"}, std::move(bytes));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  expect(project.assets().size() == 1, "empty synth fixture should still preserve its sequence");
  expect(project.diagnostics().size() == 1 &&
             project.diagnostics()[0].message ==
                 "CapcomSnes sequence found, but no valid instruments or samples were discovered",
         "detected but unusable synth tables should produce a clear diagnostic");
}

void capcomSnesCompiledAndPerformanceSnapshotsAreStable() {
  const auto bytes = makeCapcomSnesAram();
  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const auto& dialect = capcomSnesSequenceDialect();
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{7}, bytes), version,
                                                         CapcomSnesTrackDecodeOptions{.startOffset = 0x3000});
  const std::string decoded = decodedTrackSnapshot(track);
  constexpr std::string_view expectedDecoded = "3000:5:3,tempo=1422.10424<4660>,flow=0->3003|"
                                               "3003:8:2,instrument=0,flow=0->3005|"
                                               "3005:7:2,linear_gain=0.403921569<64>,flow=0->3007|"
                                               "3007:24:2,left_gain=0.6328125<0>,right_gain=0.6328125,flow=0->3009|"
                                               "3009:26:3,type=0,amount=0.251968504<32>,"
                                               "pitch_depth_semitones=3,flow=0->300C|"
                                               "300C:26:3,type=2,enabled=true<32>,amount=0.625441449,"
                                               "frequency_hz=1.953125,flow=0->300F|"
                                               "300F:65:1,duration_index=2,key_index=1,flow=0->3010|"
                                               "3010:23:1,flow=4";
  expect(decoded == expectedDecoded, "CapcomSnes decoded-command golden changed:\n" + decoded);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .behavior = dialect.defaultBehavior,
      .tracks = {track},
  };
  const std::string performance = performanceTrackSnapshot(SequenceVm().render(program, dialect).tracks[0]);
  constexpr std::string_view expectedPerformance =
      "reverb@0=0|mono@0=0|tempo@0=42191|instrument@0=capcom-snes.instrument:0|"
      "level@0=0.403921569/q256|balance@0=0.6328125,0.6328125|mod@0:0=0|mod@0:0=0.251968504|"
      "mod@0:1=0.625441449|mod@0:3=0.625441449|note@0=0/6";
  expect(performance == expectedPerformance, "CapcomSnes neutral-performance golden changed:\n" + performance);
}

void capcomSnesLfoValuesAreResolvedDuringDecode() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x1a;
  bytes[0x3001] = 0x00;
  bytes[0x3002] = 0xa0;
  bytes[0x3003] = 0x1a;
  bytes[0x3004] = 0x01;
  bytes[0x3005] = 0x40;
  bytes[0x3006] = 0x1a;
  bytes[0x3007] = 0x02;
  bytes[0x3008] = 0x20;
  bytes[0x3009] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                                         CapcomSnesTrackDecodeOptions{.startOffset = 0x3000});
  expect(track.commands.size() == 4, "CapcomSnes LFO fixture should decode three parameters and end");

  const SemanticOperand* vibratoAmount = semanticOperand(track.commands[0], "amount");
  const SemanticOperand* vibratoDepth = semanticOperand(track.commands[0], "pitch_depth_semitones");
  expect(vibratoAmount != nullptr && std::get<double>(vibratoAmount->value) == 32.0 / 127.0 &&
             vibratoAmount->encodedName == "value" && vibratoAmount->encodedValue &&
             std::get<u64>(*vibratoAmount->encodedValue) == 0xa0 && vibratoDepth != nullptr &&
             std::get<double>(vibratoDepth->value) == 3.0,
         "CapcomSnes vibrato decode should retain raw depth while resolving playback amount and pitch range");

  const SemanticOperand* tremoloAmount = semanticOperand(track.commands[1], "amount");
  expect(tremoloAmount != nullptr && std::get<double>(tremoloAmount->value) > 0.0 &&
             tremoloAmount->encodedName == "value" && tremoloAmount->encodedValue &&
             std::get<u64>(*tremoloAmount->encodedValue) == 0x40,
         "CapcomSnes tremolo decode should resolve its playback amount before execution");

  const TrackProgram version1Track =
      decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), CapcomSnesEngineVersion::v1BgmInList,
                                  CapcomSnesTrackDecodeOptions{.startOffset = 0x3000});
  const SemanticOperand* version1TremoloAmount = semanticOperand(version1Track.commands[1], "amount");
  expect(version1TremoloAmount != nullptr &&
             std::get<double>(version1TremoloAmount->value) != std::get<double>(tremoloAmount->value),
         "CapcomSnes tremolo should resolve version-dependent driver math during decode");

  const SemanticOperand* enabled = semanticOperand(track.commands[2], "enabled");
  const SemanticOperand* rateAmount = semanticOperand(track.commands[2], "amount");
  const SemanticOperand* frequency = semanticOperand(track.commands[2], "frequency_hz");
  expect(enabled != nullptr && std::get<bool>(enabled->value) && enabled->encodedName == "value" &&
             enabled->encodedValue && std::get<u64>(*enabled->encodedValue) == 0x20 && rateAmount != nullptr &&
             frequency != nullptr && std::get<double>(frequency->value) == 1.953125,
         "CapcomSnes LFO rate decode should resolve enable state, normalized amount, and physical frequency");

  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  const auto emittedTremolo = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    return modulation != nullptr && modulation->target == ModulationPerformanceTarget::TremoloDepth &&
           modulation->amount > 0.0;
  });
  expect(performance.diagnostics.empty() && emittedTremolo != performance.tracks[0].events.end() &&
             std::get<ModulationPerformanceEvent>(*emittedTremolo).amount == std::get<double>(tremoloAmount->value),
         "CapcomSnes playback should emit the tremolo amount already resolved by decode");
}

void capcomSnesCompiledCommandsDoNotNeedEngineProfile() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x41;
  bytes[0x3001] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                                         CapcomSnesTrackDecodeOptions{.startOffset = 0x3000});
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty() && performance.tracks[0].endTick == 6,
         "compiled CapcomSnes commands should render without reopening source-version configuration");
}

void capcomSnesModuleScansSpcThroughVirtualAramSource() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  const auto sourceId = session.addSource(SourceFile{.name = "Mega Man X.spc"}, makeCapcomSnesSpc());

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  expect(project.diagnostics().empty(), "SPC-backed CapcomSnes scan should not report diagnostics");
  expect(project.sources().size() == 2, "SPC scan should preserve original source plus extracted ARAM");
  expect(!project.sources()[0].derived(), "original SPC source should not be derived");
  expect(project.sources()[1].derived(), "SPC RAM source should be derived");
  expect(project.sources()[1].name == "Mega Man X.spc - ram", "derived ARAM source should match legacy naming");
  expect(project.sources()[1].title == "Capcom Logo", "derived ARAM source should carry the SPC title tag");
  expect(project.sources()[1].origin.has_value(), "derived ARAM source should preserve origin range");
  expect(project.sources()[1].origin->source == sourceId, "derived ARAM origin should point at the SPC source");
  expect(project.sources()[1].origin->offset == 0x100 && project.sources()[1].origin->size == 0x10000,
         "derived ARAM origin should point at SPC RAM bytes");

  expect(project.collections().size() == 1, "SPC-backed scan should produce one collection");
  expect(project.collections()[0].name == "Capcom Logo", "SPC-backed collection should use the SPC title tag");
  expect(project.assets().size() == 3, "SPC-backed scan should produce CapcomSnes assets from derived ARAM");
  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets()[0]);
  expect(sequence != nullptr, "SPC-backed scan should produce a sequence");
  expect(sequence->metadata.name == "Capcom Logo", "SPC-backed sequence should use the SPC title tag");
  expect(sequence->metadata.range.source == SourceId{1}, "sequence range should point at derived ARAM source");
  expect(sequence->metadata.range.offset == 0x2001, "sequence range should preserve ARAM-relative address");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets()[2]);
  expect(samples != nullptr, "SPC-backed scan should produce samples");
  expect(!samples->samples.samples.empty(), "SPC-backed scan should discover sample data");
  expect(samples->samples.samples[0].encodedData.source == SourceId{1},
         "sample encoded data should point at derived ARAM source");
}

void capcomSnesInstrumentTableSkipsBlankSlotsLikeLegacy() {
  auto bytes = makeCapcomSnesAram();
  bytes[0x400c] = 0x00;
  bytes[0x400d] = 0x8f;
  bytes[0x400e] = 0xe0;
  bytes[0x400f] = 0x00;
  writeBe16(bytes, 0x4010, 0x0200);

  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "blank-terminated.spc"}, std::move(bytes));
  const auto infos = parseCapcomSnesInstrumentInfos(sources.reader(sourceId), 0x4000, 0x5000);
  expect(infos.size() == 2, "CapcomSnes instrument parsing should skip blank table slots like legacy");
  expect(infos[0].index == 0 && infos[1].index == 2,
         "CapcomSnes instrument parsing should preserve sparse instrument indexes");

  ScanIdAllocator ids;
  ScanInput input{
      .source = sources.source(sourceId),
      .reader = sources.reader(sourceId),
      .ids = ids,
  };
  ScanResultBuilder result(input, "CapcomSnes");
  const auto instrumentSet = result.reserveInstrumentSet();
  const auto sampleCollection = result.reserveSampleCollection();
  expect(addCapcomSnesSynth(result, instrumentSet, sampleCollection, 0x4000, 0x5000, "Sparse"),
         "CapcomSnes synth builder fixture should accept sparse instrument entries");
  const ScanResult scan = result.finish();
  const auto* builtInstruments = std::get_if<InstrumentSetAsset>(&scan.assets[0]);
  expect(builtInstruments != nullptr && builtInstruments->instruments.size() == 2,
         "CapcomSnes builder should retain both sparse instruments");
  expect(builtInstruments->instruments[1].identity && builtInstruments->instruments[1].identity->key == 2,
         "a sparse source identity should remain distinct from its dense model position");
  const auto secondInstrumentSources = scan.sourceMap.ownedBy(ObjectRefs::instrument(instrumentSet.id, 1));
  expect(secondInstrumentSources.size() == 1 && scan.sourceMap.get(secondInstrumentSources[0]).range.offset == 0x400c,
         "CapcomSnes annotations should use dense instrument ownership while preserving sparse source ranges");
  expect(scan.sourceMap.ownedBy(ObjectRefs::instrument(instrumentSet.id, 2)).empty(),
         "a sparse source program must not leak into the dense annotation owner");
  expect(scan.sourceMap.ownedBy(ObjectRefs::region(instrumentSet.id, 1, 0)).size() == 1,
         "CapcomSnes sparse instruments should expose stable region ownership");

  std::vector<u8> fullTable(0x10000);
  writeLe16(fullTable, 0x5000, 0x6000);
  writeLe16(fullTable, 0x5002, 0x6000);
  fullTable[0x6000] = 0x01;
  for (u32 index = 0; index <= 0x80; ++index) {
    const size_t address = 0x3000 + index * 6;
    fullTable[address] = 0x00;
    fullTable[address + 1] = 0x8f;
    fullTable[address + 2] = 0xe0;
    fullTable[address + 3] = 0x00;
    writeBe16(fullTable, address + 4, 0x0100);
  }

  SourceStore limitSources;
  const auto limitSourceId = limitSources.add(SourceFile{.name = "program-limit.spc"}, std::move(fullTable));
  const auto limitedInfos = parseCapcomSnesInstrumentInfos(limitSources.reader(limitSourceId), 0x3000, 0x5000);
  expect(limitedInfos.size() == 0x81, "CapcomSnes instrument parsing should match legacy banked program scanning");
  expect(limitedInfos.back().index == 0x80, "CapcomSnes instrument parsing should emit bank-1 programs");
}

void capcomSnesNoteStateCommandsAreTypedAndInterpreted() {
  auto bytes = makeCapcomSnesAram();
  bytes[0x3000] = 0x09;
  bytes[0x3001] = 0x04;
  bytes[0x3002] = 0x04;
  bytes[0x3003] = 0x48;
  bytes[0x3004] = 0x41;
  bytes[0x3005] = 0x17;

  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = "Mega Man X.spc"}, std::move(bytes));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  expect(project.diagnostics().empty(), "CapcomSnes note-state scan should not report diagnostics");
  expect(!project.assets().empty(), "CapcomSnes note-state scan should produce assets");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets()[0]);
  expect(sequence != nullptr, "CapcomSnes note-state scan should produce a sequence");
  expect(!sequence->program.tracks.empty(), "CapcomSnes note-state scan should decode tracks");

  const auto* dialect = session.dialects().find(sequence->program.dialect.value);
  expect(dialect != nullptr, "CapcomSnes note-state scan should have a registered dialect");
  const auto& track = sequence->program.tracks[0];
  const auto& commands = track.commands;
  expect(commands.size() == 4, "CapcomSnes note-state fixture should decode four commands");

  expect(commandDetailKind(project.sourceMap(), commands[0]) == "capcom-snes.octave",
         "CapcomSnes octave opcode should decode as a local command");
  const SourceField* octaveField = fieldWithName(commandAnnotation(project.sourceMap(), commands[0]), "octave");
  expect(fieldEquals(octaveField, u64{4}),
         "CapcomSnes octave command should preserve its raw octave operand in source annotations");
  expect(commands[0].range.offset == 0x3000 && commands[0].range.size == 2,
         "CapcomSnes octave command should preserve its source range");

  expect(commandDetailKind(project.sourceMap(), commands[1]) == "capcom-snes.note-attributes",
         "CapcomSnes attributes opcode should decode as a local command");
  const SourceField* attributeField = fieldWithName(commandAnnotation(project.sourceMap(), commands[1]), "attributes");
  expect(fieldEquals(attributeField, u64{0x48}),
         "CapcomSnes note attributes should preserve their raw attribute byte in source annotations");
  expect(commands[1].range.offset == 0x3002 && commands[1].range.size == 2,
         "CapcomSnes note attributes should preserve their source range");

  const auto noteCommandAnnotations = project.sourceMap().withRole(commands[1].range.source, SourceRole::Command);
  const auto attributeAnnotationId = std::ranges::find_if(noteCommandAnnotations, [&](SourceAnnotationId id) {
    const SourceAnnotation& annotation = project.sourceMap().get(id);
    return annotation.detailKind == "capcom-snes.note-attributes" && annotation.range.offset == 0x3002;
  });
  expect(attributeAnnotationId != noteCommandAnnotations.end(),
         "CapcomSnes source map should expose typed note-attribute command annotations");
  expect(project.sourceMap().get(*attributeAnnotationId).label == "Note Attributes",
         "note-attribute annotation should carry a readable name");

  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, *dialect);
  const MidiSequence midiSequence = renderMidiSequence(performance);
  expect(midiSequence.diagnostics.empty(), "CapcomSnes note-state emission should not report diagnostics");
  expect(!midiSequence.tracks.empty(), "CapcomSnes note-state emission should preserve tracks");

  const auto& events = midiSequence.tracks[0].events;
  const auto note = std::ranges::find_if(events, [](const MidiEvent& event) {
    const auto* typed = std::get_if<NoteDuration>(&event);
    return typed != nullptr && typed->tick == 0;
  });
  expect(note != events.end(), "CapcomSnes note-state fixture should emit a note");
  expect(std::get<NoteDuration>(*note).key == 72,
         "CapcomSnes note-state emission should apply octave and 2-octave-up attributes");
  expect(std::get<NoteDuration>(*note).duration == 7,
         "CapcomSnes slurred note-state emission should preserve legacy note extension");
  expect(std::get<EndOfTrack>(events.back()).tick == 6,
         "CapcomSnes note-state emission should still advance by the decoded note length");
}

void capcomSnesSourceDialectDecodesAndRendersDriverCommands() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x05;
  bytes[0x3001] = 0x12;
  bytes[0x3002] = 0x00;
  bytes[0x3003] = 0x08;
  bytes[0x3004] = 0x85;
  bytes[0x3005] = 0x07;
  bytes[0x3006] = 0x80;
  bytes[0x3007] = 0x04;
  bytes[0x3008] = 0x10;
  bytes[0x3009] = 0x64;
  bytes[0x300a] = 0x18;
  bytes[0x300b] = 0x00;
  bytes[0x300c] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track = decodeCapcomSnesSourceTrack(
      ByteReader(SourceId{8}, bytes), version,
      CapcomSnesTrackDecodeOptions{.trackIndex = 2, .startOffset = 0x3000, .sourceMap = &sourceMap});
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 7,
         "CapcomSnes source dialect should decode the fixture commands, got " + std::to_string(track.commands.size()));
  expect(track.addressIndex.find(Address{0x3009}).has_value(),
         "CapcomSnes source dialect should index decoded command addresses");
  const SourceAnnotation& programAnnotation = commandAnnotation(annotations, track.commands[1]);
  const SourceField* instrument = fieldWithName(programAnnotation, "instrument");
  expect(fieldEquals(instrument, u64{0x85}),
         "CapcomSnes source command should preserve its source instrument identity");

  expect(commandAnnotation(annotations, track.commands[1]).label == "Instrument",
         "CapcomSnes dialect should describe commands through local command code");
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes source dialect fixture should render without diagnostics");
  expect(performance.tracks.size() == 1, "CapcomSnes source dialect fixture should render one track");
  expect(performance.tracks[0].endTick == 18,
         "CapcomSnes source dialect should apply one-shot dotted timing before the end command");

  const auto note = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* typed = std::get_if<NotePerformanceEvent>(&event);
    return typed != nullptr;
  });
  expect(note != performance.tracks[0].events.end(), "CapcomSnes source dialect should emit a note event");
  const auto& noteEvent = std::get<NotePerformanceEvent>(*note);
  expect(noteEvent.key == 3.0 && noteEvent.durationTicks == 18,
         "CapcomSnes note event should reflect source key and dotted duration");
  expect(noteEvent.header.sourceCommand == CommandId{4} && noteEvent.header.tick == 0,
         "CapcomSnes note event should link back to the source command");

  const auto pan = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<StereoBalancePerformanceEvent>(event);
  });
  expect(pan != performance.tracks[0].events.end(), "CapcomSnes pan command should emit a target-neutral pan event");
  expect(std::get<StereoBalancePerformanceEvent>(*pan).header.tick == 18,
         "CapcomSnes pan event should occur after the note advances the VM clock");
}

void capcomSnesInitialDurationRateIsFullLength() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0xe1;
  bytes[0x3001] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                                         CapcomSnesTrackDecodeOptions{.startOffset = 0x3000});
  expect(track.commands.size() == 2, "CapcomSnes duration fixture should decode note and end");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes duration fixture should render without diagnostics");

  const auto note = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(note != performance.tracks[0].events.end(), "CapcomSnes duration fixture should emit a note");
  expect(std::get<NotePerformanceEvent>(*note).durationTicks == 192,
         "CapcomSnes initial duration rate should produce full-length notes like legacy");
  expect(performance.tracks[0].endTick == 192, "CapcomSnes initial duration rate should not change source note length");
}

void capcomSnesPanPerformanceCarriesGainCompensation() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x18;
  bytes[0x3001] = 0x40;
  bytes[0x3002] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                  CapcomSnesTrackDecodeOptions{.startOffset = 0x3000, .sourceMap = &sourceMap});
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 2, "CapcomSnes pan fixture should decode pan and end");

  const SourceAnnotation& panAnnotation = commandAnnotation(annotations, track.commands[0]);
  expect(fieldWithName(panAnnotation, "left_gain") != nullptr && fieldWithName(panAnnotation, "right_gain") != nullptr,
         "CapcomSnes pan annotation should expose the source engine's stereo gains");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes pan fixture should render without diagnostics");
  expect(performance.tracks[0].events.size() == 3,
         "CapcomSnes pan fixture should emit initial defaults and one pan event");
  const auto* performancePan = std::get_if<StereoBalancePerformanceEvent>(&performance.tracks[0].events[2]);
  expect(performancePan != nullptr && performancePan->rightGain > performancePan->leftGain,
         "CapcomSnes pan performance should retain the source engine's stereo balance");

  const MidiSequence midi = renderMidiSequence(performance);
  expect(midi.tracks[0].events.size() == 6,
         "CapcomSnes compensated pan should render port, initial defaults, pan, expression, and end");
  expect(std::get<Pan>(midi.tracks[0].events[3]).value == 113,
         "CapcomSnes pan renderer should emit the driver-computed MIDI pan");
  expect(std::get<Expression>(midi.tracks[0].events[4]).value == 123,
         "CapcomSnes pan renderer should quantize the source gain compensation as expression");
}

void capcomSnesDialectEmitsSourceOnlyDriverSemantics() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x0c;
  bytes[0x3001] = 0x80;
  bytes[0x3002] = 0x0d;
  bytes[0x3003] = 0x20;
  bytes[0x3004] = 0x19;
  bytes[0x3005] = 0x40;
  bytes[0x3006] = 0x1b;
  bytes[0x3007] = 0x01;
  bytes[0x3008] = 0x02;
  bytes[0x3009] = 0x1c;
  bytes[0x300a] = 0x01;
  bytes[0x300b] = 0x1d;
  bytes[0x300c] = 0x05;
  bytes[0x300d] = 0x1e;
  bytes[0x300e] = 0x1f;
  bytes[0x300f] = 0x41;
  bytes[0x3010] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                  CapcomSnesTrackDecodeOptions{.startOffset = 0x3000, .sourceMap = &sourceMap});
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 10, "CapcomSnes source-only commands should not truncate track decoding");

  const std::vector<std::string> expectedKinds{
      "capcom-snes.tuning",        "capcom-snes.portamento-time",
      "capcom-snes.master-volume", "capcom-snes.echo-param",
      "capcom-snes.echo-on-off",   "capcom-snes.release-rate",
      "capcom-snes.nop",           "capcom-snes.nop",
      "capcom-snes.note",          "capcom-snes.end",
  };
  for (size_t index = 0; index < expectedKinds.size(); ++index) {
    expect(commandDetailKind(annotations, track.commands[index]) == expectedKinds[index],
           "CapcomSnes source-only fixture should decode typed command " + std::to_string(index));
  }

  const SourceAnnotation& tuningAnnotation = commandAnnotation(annotations, track.commands[0]);
  const SourceField* tuning = fieldWithName(tuningAnnotation, "tuning");
  const SourceField* cents = fieldWithName(tuningAnnotation, "cents");
  expect(fieldEquals(tuning, s64{-128}) && fieldEquals(cents, -50.0),
         "CapcomSnes tuning command should preserve raw and interpreted operands in source annotations");
  const SourceField* gain = fieldWithName(commandAnnotation(annotations, track.commands[5]), "gain");
  expect(fieldEquals(gain, u64{165}),
         "CapcomSnes release command should keep the driver GAIN display value in source annotations");
  expect(commandAnnotation(annotations, track.commands[6]).playbackStatus == CommandPlaybackStatus::NoOp,
         "CapcomSnes no-op command should persist no-op playback status");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes source-only commands should render without diagnostics");
  expect(performance.tracks[0].events.size() == 6,
         "CapcomSnes source-only commands should emit semantic performance events where possible");
  expect(std::holds_alternative<ReverbPerformanceEvent>(performance.tracks[0].events[0]),
         "CapcomSnes should emit initial reverb before source command events");
  expect(std::holds_alternative<MonoModePerformanceEvent>(performance.tracks[0].events[1]),
         "CapcomSnes should emit initial mono mode before source command events");
  expect(std::holds_alternative<TuningPerformanceEvent>(performance.tracks[0].events[2]),
         "CapcomSnes tuning should emit a target-neutral tuning event");
  expect(std::holds_alternative<MasterLevelPerformanceEvent>(performance.tracks[0].events[3]),
         "CapcomSnes master volume should emit a target-neutral master level event");
  expect(std::holds_alternative<ReverbPerformanceEvent>(performance.tracks[0].events[4]),
         "CapcomSnes echo on/off should emit a target-neutral reverb event");
  expect(std::holds_alternative<NotePerformanceEvent>(performance.tracks[0].events[5]),
         "CapcomSnes source-only fixture should still reach the later note");
  expect(performance.tracks[0].endTick == 6, "CapcomSnes source-only fixture should advance through the later note");

  const MidiSequence midi = renderMidiSequence(performance);
  expect(std::holds_alternative<FineTune>(midi.tracks[0].events[3]),
         "CapcomSnes tuning performance should render as MIDI fine tuning");
  expect(std::holds_alternative<MasterVolume>(midi.tracks[0].events[4]),
         "CapcomSnes master level performance should render as MIDI master volume");
  expect(std::get<Reverb>(midi.tracks[0].events[5]).value == 40,
         "CapcomSnes reverb performance should preserve the legacy echo send");
}

void capcomSnesDialectEmitsPortamentoFromPreviousSourceKey() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x0d;
  bytes[0x3001] = 0x40;
  bytes[0x3002] = 0x41;
  bytes[0x3003] = 0x46;
  bytes[0x3004] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  const TrackProgram track = decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                                         CapcomSnesTrackDecodeOptions{.startOffset = 0x3000});
  expect(track.commands.size() == 4, "CapcomSnes portamento fixture should decode portamento, notes, and end");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes portamento fixture should render without diagnostics");
  expect(performance.tracks[0].events.size() == 5,
         "CapcomSnes portamento fixture should emit initial defaults, two notes, and one portamento event");
  expect(std::holds_alternative<NotePerformanceEvent>(performance.tracks[0].events[2]),
         "CapcomSnes portamento fixture should emit the first note before portamento");
  const auto* portamento = std::get_if<PortamentoPerformanceEvent>(&performance.tracks[0].events[3]);
  expect(portamento != nullptr && portamento->timeMilliseconds == 160.0 && portamento->previousKey == 0.0,
         "CapcomSnes portamento should use source-key distance and previous source key");

  const MidiSequence midi = renderMidiSequence(performance);
  expect(std::holds_alternative<NoteDuration>(midi.tracks[0].events[3]),
         "CapcomSnes portamento fixture should render the first note");
  expect(std::get<PortamentoTime14>(midi.tracks[0].events[4]).value == 160,
         "CapcomSnes portamento performance should render as 14-bit MIDI portamento time");
  expect(std::get<PortamentoControl>(midi.tracks[0].events[5]).key == 0,
         "CapcomSnes portamento performance should render the previous-key controller");
  expect(std::holds_alternative<NoteDuration>(midi.tracks[0].events[6]),
         "CapcomSnes portamento fixture should render the second note after portamento controllers");
}

void capcomSnesDialectExecutesRepeatUntilCommand() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x41;
  bytes[0x3001] = 0x0e;
  bytes[0x3002] = 0x02;
  bytes[0x3003] = 0x30;
  bytes[0x3004] = 0x00;
  bytes[0x3005] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                  CapcomSnesTrackDecodeOptions{.startOffset = 0x3000, .sourceMap = &sourceMap});
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 3, "CapcomSnes repeat fixture should decode note, repeat, and end");

  expect(commandDetailKind(annotations, track.commands[1]) == "capcom-snes.repeat-until",
         "CapcomSnes repeat opcode should decode as Repeat Until");
  const SourceAnnotation& repeatAnnotation = commandAnnotation(annotations, track.commands[1]);
  expect(fieldEquals(fieldWithName(repeatAnnotation, "slot"), u64{1}) &&
             fieldEquals(fieldWithName(repeatAnnotation, "count"), u64{2}) &&
             fieldEquals(fieldWithName(repeatAnnotation, "destination"), u64{0x3000}),
         "CapcomSnes repeat display should preserve slot, count, and destination in source annotations");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes finite repeat should render without diagnostics");
  expect(performance.tracks[0].events.size() == 5,
         "CapcomSnes repeat count should emit initial defaults and replay the loop body");
  expect(performance.tracks[0].endTick == 18, "CapcomSnes repeat count should include the original pass plus replays");

  for (u64 tick : {0ULL, 6ULL, 12ULL}) {
    const bool found = std::ranges::any_of(performance.tracks[0].events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == tick;
    });
    expect(found, "CapcomSnes repeat fixture should emit a note at tick " + std::to_string(tick));
  }
}

void capcomSnesDialectAppliesRepeatBreakAttributesOnlyWhenBranchIsTaken() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x41;
  bytes[0x3001] = 0x12;
  bytes[0x3002] = 0x10;
  bytes[0x3003] = 0x30;
  bytes[0x3004] = 0x0a;
  bytes[0x3005] = 0x41;
  bytes[0x3006] = 0x0e;
  bytes[0x3007] = 0x01;
  bytes[0x3008] = 0x30;
  bytes[0x3009] = 0x00;
  bytes[0x300a] = 0x41;
  bytes[0x300b] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                  CapcomSnesTrackDecodeOptions{.startOffset = 0x3000, .sourceMap = &sourceMap});
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 6, "CapcomSnes repeat-break fixture should decode both branch paths");

  expect(commandDetailKind(annotations, track.commands[1]) == "capcom-snes.repeat-break",
         "CapcomSnes repeat-break opcode should decode as Repeat Break");
  const SourceAnnotation& repeatBreakAnnotation = commandAnnotation(annotations, track.commands[1]);
  expect(fieldEquals(fieldWithName(repeatBreakAnnotation, "slot"), u64{1}) &&
             fieldEquals(fieldWithName(repeatBreakAnnotation, "attributes"), u64{16}) &&
             fieldEquals(fieldWithName(repeatBreakAnnotation, "destination"), u64{0x300a}),
         "CapcomSnes repeat-break display should preserve slot, attributes, and destination in source annotations");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes repeat-break should render without diagnostics");
  expect(performance.tracks[0].events.size() >= 5,
         "CapcomSnes repeat-break should emit initial defaults and repeated notes");
  expect(performance.tracks[0].endTick == 27,
         "CapcomSnes repeat-break attributes should dot the branch-target note only on the final pass");

  const auto finalNote = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* note = std::get_if<NotePerformanceEvent>(&event);
    return note != nullptr && note->header.tick == 18;
  });
  expect(
      finalNote != performance.tracks[0].events.end() && std::get<NotePerformanceEvent>(*finalNote).durationTicks == 9,
      "CapcomSnes repeat-break branch should apply note attributes before the branch target plays");
}

void capcomSnesDialectDecodesRepeatBreakSideTargets() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x12;
  bytes[0x3001] = 0x00;
  bytes[0x3002] = 0x30;
  bytes[0x3003] = 0x06;
  bytes[0x3004] = 0x17;
  bytes[0x3006] = 0x41;
  bytes[0x3007] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> diagnostics;
  const TrackProgram track = decodeCapcomSnesSourceTrack(
      ByteReader(SourceId{8}, bytes), version,
      CapcomSnesTrackDecodeOptions{.startOffset = 0x3000, .sourceMap = &sourceMap, .diagnostics = &diagnostics});
  const SourceMap annotations = sourceMap.finish();

  expect(track.commands.size() == 4,
         "CapcomSnes linear decode should preserve repeat-break side target commands after fallthrough");
  expect(commandDetailKind(annotations, track.commands[0]) == "capcom-snes.repeat-break",
         "CapcomSnes side-target fixture should start with repeat break");
  expect(commandDetailKind(annotations, track.commands[1]) == "capcom-snes.end",
         "CapcomSnes side-target fixture should keep the fallthrough end command");
  expect(commandDetailKind(annotations, track.commands[2]) == "capcom-snes.note",
         "CapcomSnes side-target fixture should decode the out-of-line repeat-break target");
  expect(diagnostics.empty(), "CapcomSnes side-target fixture should decode without diagnostics");

  const auto repeatBreakAnnotations = annotations.withSequenceSemantic(SourceId{8}, SequenceSemantic::RepeatBreak);
  expect(repeatBreakAnnotations.size() == 1, "CapcomSnes repeat-break should publish a source annotation");
  const SourceAnnotation& repeatBreak = annotations.get(repeatBreakAnnotations.front());
  const auto link = std::ranges::find_if(
      repeatBreak.links, [](const SourceLink& sourceLink) { return sourceLink.role == SourceLinkRole::RepeatTarget; });
  expect(link != repeatBreak.links.end(), "CapcomSnes repeat-break should link to its side target");
  const auto* targetRange = std::get_if<SourceRange>(&link->target);
  expect(targetRange != nullptr && targetRange->source == SourceId{8} && targetRange->offset == 0x3006,
         "CapcomSnes repeat-break source link should point at the out-of-line target");
}

void capcomSnesV1DialectPreservesUnknownOneByteEvents() {
  std::vector<u8> bytes(0x4000);
  bytes[0x3000] = 0x1e;
  bytes[0x3001] = 0xab;
  bytes[0x3002] = 0x1f;
  bytes[0x3003] = 0xcd;
  bytes[0x3004] = 0x41;
  bytes[0x3005] = 0x17;

  constexpr auto version = CapcomSnesEngineVersion::v1BgmInList;
  const SequenceDialect& dialect = capcomSnesSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeCapcomSnesSourceTrack(ByteReader(SourceId{8}, bytes), version,
                                  CapcomSnesTrackDecodeOptions{.startOffset = 0x3000, .sourceMap = &sourceMap});
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 4, "CapcomSnes V1 unknown one-byte events should not truncate track decoding");
  expect(commandDetailKind(annotations, track.commands[0]) == "capcom-snes.unknown-one-byte",
         "CapcomSnes V1 opcode $1E should decode as a one-byte unknown event");
  expect(commandDetailKind(annotations, track.commands[1]) == "capcom-snes.unknown-one-byte",
         "CapcomSnes V1 opcode $1F should decode as a one-byte unknown event");
  expect(track.commands[0].range.offset == 0x3000 && track.commands[0].range.size == 2,
         "CapcomSnes V1 unknown one-byte event should preserve its source range");

  const SourceAnnotation& unknownAnnotation = commandAnnotation(annotations, track.commands[0]);
  const SourceField* opcode = fieldWithName(unknownAnnotation, "opcode");
  const SourceField* bytesField = fieldWithName(unknownAnnotation, "bytes");
  expect(fieldEquals(opcode, u64{0x1e}) && fieldEquals(bytesField, "AB"),
         "CapcomSnes V1 ignored event should preserve its opcode and operand bytes in source annotations");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = static_cast<u32>(version)},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "CapcomSnes V1 unknown one-byte events should render without diagnostics");
  expect(performance.tracks[0].events.size() == 3,
         "CapcomSnes V1 fixture should emit initial defaults and still reach the later note");
}
