/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/DlsExporter.h"
#include "value/export/Export.h"
#include "value/core/FormatModule.h"
#include "value/export/MidiExporter.h"
#include "value/core/ModulationAnalysis.h"
#include "value/core/SequenceDialect.h"
#include "value/core/SequenceVm.h"
#include "value/core/Session.h"
#include "value/core/SampleDecoder.h"
#include "value/export/ModulationScaling.h"
#include "value/export/PerformanceMidiRenderer.h"
#include "value/export/SoundFontExporter.h"
#include "value/export/WavExporter.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace vgmtrans::core;

void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
void capcomSnesModuleScansSpcThroughVirtualAramSource();
void capcomSnesInstrumentTableSkipsBlankSlotsLikeLegacy();
void capcomSnesNoteStateCommandsAreTypedAndInterpreted();
void capcomSnesSourceDialectDecodesAndRendersDriverCommands();
void capcomSnesPanPerformanceCarriesGainCompensation();
void capcomSnesDialectEmitsSourceOnlyDriverSemantics();
void capcomSnesDialectEmitsPortamentoFromPreviousSourceKey();
void capcomSnesDialectExecutesRepeatUntilCommand();
void capcomSnesV1DialectPreservesUnknownOneByteEvents();
void ndsSequenceDialectDecodesAndRendersNoteWaitCommands();
void ndsSequenceDialectExecutesCallAndReturn();
void ndsSequenceDialectDiscoversSecondaryTrackStarts();
void ndsSequenceDialectPreservesIgnoredNoOpOperands();

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

u32 readLe32(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<u32>(bytes[offset]) | (static_cast<u32>(bytes[offset + 1]) << 8) |
         (static_cast<u32>(bytes[offset + 2]) << 16) | (static_cast<u32>(bytes[offset + 3]) << 24);
}

u16 readLe16(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<u16>(bytes[offset]) | (static_cast<u16>(bytes[offset + 1]) << 8);
}

s16 readLeS16(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<s16>(readLe16(bytes, offset));
}

s32 readLeS32(const std::vector<u8>& bytes, size_t offset) {
  return static_cast<s32>(readLe32(bytes, offset));
}

bool containsAscii(const std::vector<u8>& bytes, std::string_view text) {
  return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

size_t asciiOffset(const std::vector<u8>& bytes, std::string_view text) {
  const auto found = std::search(bytes.begin(), bytes.end(), text.begin(), text.end());
  if (found == bytes.end()) {
    throw std::runtime_error("expected ASCII marker was not found");
  }
  return static_cast<size_t>(std::distance(bytes.begin(), found));
}

u32 chunkSize(const std::vector<u8>& bytes, std::string_view chunkId) {
  return readLe32(bytes, asciiOffset(bytes, chunkId) + 4);
}

bool soundFontIgenContainsAmount(const std::vector<u8>& bytes, u16 generator, s16 expectedAmount) {
  const auto chunkOffset = asciiOffset(bytes, "igen");
  const auto size = chunkSize(bytes, "igen");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 4 <= payloadOffset + size; offset += 4) {
    if (readLe16(bytes, offset) == generator && readLeS16(bytes, offset + 2) == expectedAmount) {
      return true;
    }
  }
  return false;
}

bool soundFontPgenContainsAmount(const std::vector<u8>& bytes, u16 generator, s16 expectedAmount) {
  const auto chunkOffset = asciiOffset(bytes, "pgen");
  const auto size = chunkSize(bytes, "pgen");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 4 <= payloadOffset + size; offset += 4) {
    if (readLe16(bytes, offset) == generator && readLeS16(bytes, offset + 2) == expectedAmount) {
      return true;
    }
  }
  return false;
}

bool soundFontBagAt(const std::vector<u8>& bytes, std::string_view chunkId, size_t index, u16 genIndex, u16 modIndex) {
  const auto chunkOffset = asciiOffset(bytes, chunkId);
  const auto size = chunkSize(bytes, chunkId);
  const auto offset = chunkOffset + 8 + (index * 4);
  if (offset + 4 > chunkOffset + 8 + size) {
    return false;
  }

  return readLe16(bytes, offset) == genIndex && readLe16(bytes, offset + 2) == modIndex;
}

bool soundFontImodContains(const std::vector<u8>& bytes, u16 source, u16 destination, s16 amount) {
  const auto chunkOffset = asciiOffset(bytes, "imod");
  const auto size = chunkSize(bytes, "imod");
  const auto payloadOffset = chunkOffset + 8;
  for (size_t offset = payloadOffset; offset + 10 <= payloadOffset + size; offset += 10) {
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 2) == destination &&
        readLeS16(bytes, offset + 4) == amount) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 destination, s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset + 4) == destination && readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 source, u16 destination, s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 4) == destination &&
        readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool dlsArt2ContainsConnection(const std::vector<u8>& bytes, u16 source, u16 control, u16 destination,
                               s32 expectedScale) {
  const auto chunkOffset = asciiOffset(bytes, "art2");
  const auto payloadOffset = chunkOffset + 8;
  const auto connectionCount = readLe32(bytes, payloadOffset + 4);
  for (u32 i = 0; i < connectionCount; ++i) {
    const auto offset = payloadOffset + 8 + (static_cast<size_t>(i) * 12);
    if (readLe16(bytes, offset) == source && readLe16(bytes, offset + 2) == control &&
        readLe16(bytes, offset + 4) == destination && readLeS32(bytes, offset + 8) == expectedScale) {
      return true;
    }
  }
  return false;
}

bool sameRange(SourceRange lhs, SourceRange rhs) {
  return lhs.source == rhs.source && lhs.offset == rhs.offset && lhs.size == rhs.size;
}

const Diagnostic& diagnosticWithMessage(const std::vector<Diagnostic>& diagnostics, std::string_view message) {
  const auto found = std::ranges::find_if(
      diagnostics, [message](const Diagnostic& diagnostic) { return diagnostic.message == message; });
  if (found == diagnostics.end()) {
    throw std::runtime_error("expected diagnostic was not found");
  }
  return *found;
}

void expectDiagnosticRange(const std::vector<Diagnostic>& diagnostics, std::string_view message,
                           SourceRange expectedRange) {
  const auto& diagnostic = diagnosticWithMessage(diagnostics, message);
  expect(diagnostic.range.has_value(), "diagnostic should preserve a source range");
  expect(sameRange(*diagnostic.range, expectedRange), "diagnostic should preserve the expected source range");
}

[[nodiscard]] bool canScanProbeSequence(const SourceFile&, std::span<const u8> bytes) {
  return !bytes.empty() && bytes[0] == 0xaa;
}

[[nodiscard]] ScanResult scanProbeSequence(const ScanInput& input) {
  const auto assetId = input.ids.nextAssetId();
  const auto collectionId = input.ids.nextCollectionId();
  const auto itemId = input.ids.nextItemId();
  const auto childItemId = input.ids.nextItemId();
  const auto assetRange = input.reader.range(0, input.reader.size());

  SequenceProgramAsset sequence{
      .metadata =
          AssetMetadata{
              .id = assetId,
              .format = "ProbeSequence",
              .name = input.source.name,
              .range = assetRange,
              .items =
                  ItemTree{
                      .root = itemId,
                      .nodes = {ItemNode{
                                    .id = itemId,
                                    .kind = ItemKind::Sequence,
                                    .detailKind = "probe-sequence",
                                    .name = input.source.name,
                                    .range = assetRange,
                                    .children = {ItemId{9999}},
                                },
                                ItemNode{
                                    .id = childItemId,
                                    .parent = itemId,
                                    .kind = ItemKind::Header,
                                    .detailKind = "probe-header",
                                    .name = "Header",
                                    .range = input.reader.range(0, 1),
                                }},
                  },
          },
      .program =
          SequenceProgram{
              .dialect = DialectId{.value = "probe"},
              .timebase = Timebase{.ppqn = 48},
          },
  };

  ScanResult result;
  result.assets.emplace_back(std::move(sequence));
  result.collections.push_back(Collection{
      .id = collectionId,
      .name = input.source.name,
      .sequence = assetId,
  });
  result.diagnostics.push_back(Diagnostic{
      .severity = Severity::Info,
      .message = "probe sequence scanned",
      .range = assetRange,
  });

  if (!input.source.virtualized) {
    result.extractedSources.push_back(ExtractedSource{
        .file = SourceFile{.name = input.source.name + ".child"},
        .bytes = {0xbb, 0x01},
        .origin = input.reader.range(0, 1),
    });
  }

  return result;
}

[[nodiscard]] FormatModule probeSequenceModule() {
  return FormatModule{
      .name = "ProbeSequence",
      .canScan = canScanProbeSequence,
      .scan = scanProbeSequence,
  };
}

[[nodiscard]] bool canScanProbeMisc(const SourceFile& source, std::span<const u8> bytes) {
  return source.virtualized && !bytes.empty() && bytes[0] == 0xbb;
}

[[nodiscard]] ScanResult scanProbeMisc(const ScanInput& input) {
  return ScanResult{
      .assets = {MiscAsset{
          .metadata =
              AssetMetadata{
                  .format = "ProbeMisc",
                  .name = input.source.name,
                  .range = input.reader.range(0, input.reader.size()),
              },
          .payload = {input.reader.u8At(0), input.reader.u8At(1)},
      }},
  };
}

[[nodiscard]] FormatModule probeMiscModule() {
  return FormatModule{
      .name = "ProbeMisc",
      .canScan = canScanProbeMisc,
      .scan = scanProbeMisc,
  };
}

struct ProbeSequenceContext {
  double velocity = 0.75;
};

struct ProbeTrackState {
  u32 program = 0;
};

struct ProbeProgramCommand {
  u8 program = 0;

  static constexpr std::string_view kind = "probe.program";
  static constexpr std::string_view name = "Program";

  static ProbeProgramCommand parse(CommandReader& in) {
    return ProbeProgramCommand{.program = in.u8("program")};
  }

  void describe(CommandInfo& out) const {
    out.field("program", static_cast<u64>(program));
  }

  Effects execute(ProbeTrackState& state, Emit& out, VmApi&, const ProbeSequenceContext&) const {
    state.program = program;
    out.instrument(InstrumentPerformanceEvent{
        .program = program,
    });
    return Effects::none();
  }
};

struct ProbeNoteCommand {
  u8 key = 0;
  u8 duration = 0;

  static constexpr std::string_view kind = "probe.note";
  static constexpr std::string_view name = "Note";

  static ProbeNoteCommand parse(CommandReader& in) {
    return ProbeNoteCommand{
        .key = in.u8("key"),
        .duration = in.u8("duration"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("key", static_cast<u64>(key));
    out.field("duration", static_cast<u64>(duration));
  }

  Effects execute(ProbeTrackState& state, Emit& out, VmApi&, const ProbeSequenceContext& context) const {
    // This mirrors a source driver using the current track program as a key bank.
    out.note(NotePerformanceEvent{
        .key = static_cast<double>(state.program * 12 + key),
        .velocity = context.velocity,
        .durationTicks = duration,
    });
    return Effects::wait(duration);
  }
};

struct ProbeJumpCommand {
  Address destination;

  static constexpr std::string_view kind = "probe.jump";
  static constexpr std::string_view name = "Jump";

  static ProbeJumpCommand parse(CommandReader& in) {
    return ProbeJumpCommand{.destination = in.le16Address("destination")};
  }

  void describe(CommandInfo& out) const {
    out.field("destination", destination);
  }

  Effects execute(ProbeTrackState&, Emit&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.jump(destination)};
  }
};

struct ProbeCallCommand {
  Address destination;

  static constexpr std::string_view kind = "probe.call";
  static constexpr std::string_view name = "Call";

  static ProbeCallCommand parse(CommandReader& in) {
    return ProbeCallCommand{.destination = in.le16Address("destination")};
  }

  void describe(CommandInfo& out) const {
    out.field("destination", destination);
  }

  Effects execute(ProbeTrackState&, Emit&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.call(destination)};
  }
};

struct ProbeReturnCommand {
  static constexpr std::string_view kind = "probe.return";
  static constexpr std::string_view name = "Return";

  static ProbeReturnCommand parse(CommandReader&) {
    return ProbeReturnCommand{};
  }

  Effects execute(ProbeTrackState&, Emit&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.return_()};
  }
};

struct ProbeRepeatCommand {
  u8 slot = 0;
  u8 count = 0;
  Address destination;

  static constexpr std::string_view kind = "probe.repeat";
  static constexpr std::string_view name = "Repeat";

  static ProbeRepeatCommand parse(CommandReader& in) {
    return ProbeRepeatCommand{
        .slot = in.u8("slot"),
        .count = in.u8("count"),
        .destination = in.le16Address("destination"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("slot", static_cast<u64>(slot));
    out.field("count", static_cast<u64>(count));
    out.field("destination", destination);
  }

  Effects execute(ProbeTrackState&, Emit&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.repeatUntil(slot, count, destination)};
  }
};

struct ProbeEndCommand {
  static constexpr std::string_view kind = "probe.end";
  static constexpr std::string_view name = "End";

  static ProbeEndCommand parse(CommandReader&) {
    return ProbeEndCommand{};
  }

  Effects execute(ProbeTrackState&, Emit&, VmApi& vm, const ProbeSequenceContext&) const {
    return Effects{.step = vm.end()};
  }
};

[[nodiscard]] SequenceDialect probeSequenceDialect(SequenceProgramBehavior behavior = {}) {
  return SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext>("probe", ProbeSequenceContext{.velocity = 0.5})
      .timebase(Timebase{.ppqn = 48})
      .defaultBehavior(behavior)
      .commands<ProbeProgramCommand, ProbeNoteCommand, ProbeJumpCommand, ProbeCallCommand, ProbeReturnCommand,
                ProbeRepeatCommand, ProbeEndCommand>();
}

[[nodiscard]] SourceRange probeRange(u64 offset, u64 size) {
  return SourceRange{
      .source = SourceId{0},
      .offset = offset,
      .size = size,
  };
}

template <class Command, size_t Size>
const SourceCommand& addProbeCommand(TrackProgramBuilder& builder, const SequenceDialect& dialect, Address address,
                                     SourceRange range, const std::array<u8, Size>& bytes) {
  const auto* handler = dialect.handlerForKind(Command::kind);
  if (handler == nullptr) {
    throw std::runtime_error("probe command handler was not registered");
  }
  return builder.add<Command>(handler->id, handler->kind, address, range, std::span<const u8>{bytes});
}

void formatRegistryStoresCopyableModuleValues() {
  FormatRegistry registry;
  registry.add(probeSequenceModule());

  const FormatRegistry copy = registry;
  const std::array<u8, 1> probeBytes{0xaa};
  expect(copy.modules().size() == 1, "format registry should copy registered module values");
  expect(copy.modules()[0].name == std::string_view("ProbeSequence"),
         "format registry should preserve copied module names");
  expect(copy.modules()[0].canScan(SourceFile{}, probeBytes),
         "format registry should preserve copied module scan predicates");

  bool threw = false;
  try {
    registry.add(FormatModule{
        .name = "Broken",
    });
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "format registry should reject incomplete module values");
}

void sequenceDialectRegistryStoresCopyableDialectValues() {
  SequenceDialectRegistry registry;
  registry.add(probeSequenceDialect());

  const SequenceDialectRegistry copy = registry;
  const auto* dialect = copy.find("probe");
  expect(dialect != nullptr, "sequence dialect registry should copy registered dialect values");
  expect(dialect->handlerForKind(ProbeNoteCommand::kind) != nullptr,
         "sequence dialect registry should preserve copied command handlers");
  expect(copy.find("Missing") == nullptr, "sequence dialect registry should return null for a missing dialect");
  expect(copy.contains("probe"), "sequence dialect registry should report copied dialect keys");

  bool threw = false;
  try {
    registry.add(SequenceDialect{});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "sequence dialect registry should reject dialects with empty IDs");
}

void byteReaderChecksBoundsAndEndian() {
  const std::vector<u8> bytes{0x00, 0x34, 0x12, 0x78, 0x56};
  const ByteReader reader(SourceId{7}, bytes);

  expect(reader.has(1, 4), "reader should report valid four-byte range");
  expect(!reader.has(4, 2), "reader should reject range past end");
  expect(reader.u8At(1) == 0x34, "reader should read u8");
  expect(reader.le16(1) == 0x1234, "reader should read little-endian u16");
  expect(reader.be16(1) == 0x3412, "reader should read big-endian u16");
  expect(reader.le32(1) == 0x56781234, "reader should read little-endian u32");
  expect(reader.be32(1) == 0x34127856, "reader should read big-endian u32");

  bool threw = false;
  try {
    static_cast<void>(reader.u8At(5));
  } catch (const std::out_of_range&) {
    threw = true;
  }
  expect(threw, "reader should throw on out-of-range access");
}

void sourceCommandsPreserveBytesOperandsAndDialectDisplay() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};
  const std::array<u8, 2> programBytes{0x80, 0x05};
  const SourceCommand& command =
      addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()),
                                           programBytes);

  expect(track.commands.size() == 1, "track builder should append one source command");
  expect(track.commandBytes.size() == programBytes.size(), "track builder should pool command bytes");
  expect(track.bytesFor(command)[0] == 0x80 && track.bytesFor(command)[1] == 0x05,
         "source command should point back to its stored bytes");

  const auto operands = track.operandsFor(command);
  expect(operands.size() == 1, "source command should retain decoded operands");
  expect(operands[0].name == "program", "decoded operand should preserve its source name");
  expect(std::get<u64>(operands[0].value) == 5, "decoded operand should preserve its raw value");
  expect(operands[0].range.offset == 1 && operands[0].range.size == 1,
         "decoded operand should preserve its source range");

  const CommandInfo info = dialect.describe(track, command);
  expect(info.name == "Program", "dialect display should use the registered command name");
  expect(info.detailKind == "probe.program", "dialect display should use the registered command kind");
  expect(info.fields.size() == 1 && info.fields[0].name == "program" && info.fields[0].value == "5",
         "dialect display should be derived by replaying the format-local command parser");

  ItemTree itemTree;
  ScanIdAllocator ids;
  ItemTreeBuilder items(itemTree, ids);
  const ItemId root = items.add(std::nullopt, ItemKind::Track, "probe.track", "Track", probeRange(0, 0));
  const ItemId commandItem = addSourceCommandItem(items, root, dialect, track, command);
  const auto* item = itemById(itemTree, commandItem);
  expect(item != nullptr && item->kind == ItemKind::Command, "source command helper should add a command item");
  expect(item->detailKind == "probe.program" && item->name == "Program",
         "source command helper should reuse dialect display metadata");
  expect(item->description == "program 5", "source command helper should format command fields consistently");
  expect(sameRange(item->range, command.range), "source command helper should preserve the command source range");
  expect(itemById(itemTree, root)->children == std::vector<ItemId>{commandItem},
         "source command helper should attach command items under the requested parent");

  bool rejectedTrailingBytes = false;
  try {
    const std::array<u8, 3> trailingProgramBytes{0x80, 0x05, 0xaa};
    static_cast<void>(addProbeCommand<ProbeProgramCommand>(
        builder, dialect, Address{2}, probeRange(2, trailingProgramBytes.size()), trailingProgramBytes));
  } catch (const std::invalid_argument&) {
    rejectedTrailingBytes = true;
  }
  expect(rejectedTrailingBytes, "track builder should reject command bytes not consumed by the local parser");
  expect(track.commands.size() == 1, "rejected command bytes should not mutate the track program");
}

void sequenceVmExecutesSourceCommandsAndStopsAtPlayOnceLoop() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{2},
      .sourceTrackNumber = 7,
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()),
                                       programBytes);
  const CommandId noteCommandId =
      addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes).id;
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{8}, probeRange(8, endBytes.size()), endBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "sequence VM should render the probe sequence without diagnostics");
  expect(performance.tracks.size() == 1, "sequence VM should render one performance track");
  const PerformanceTrack& renderedTrack = performance.tracks[0];
  expect(renderedTrack.id == TrackId{2} && renderedTrack.sourceTrackNumber == 7,
         "performance track should preserve source track identity");
  expect(renderedTrack.endTick == 12, "default play-once loop policy should stop at the first repeated command");
  expect(renderedTrack.events.size() == 2, "VM should emit program and note events before the loop repeats");

  const auto* instrument = std::get_if<InstrumentPerformanceEvent>(&renderedTrack.events[0]);
  expect(instrument != nullptr && instrument->program == 5,
         "program command should emit a target-neutral instrument event");
  expect(instrument->header.sourceCommand == CommandId{0} && instrument->header.tick == 0,
         "instrument event should link to the source command and tick");

  const auto* note = std::get_if<NotePerformanceEvent>(&renderedTrack.events[1]);
  expect(note != nullptr, "note command should emit a target-neutral note event");
  expect(note->key == 64.0 && note->velocity == 0.5 && note->durationTicks == 12,
         "note event should use driver state and dialect context while staying MIDI-neutral");
  expect(note->header.sourceCommand == noteCommandId && note->header.tick == 0,
         "note event should link back to the source command that emitted it");
  expect(trackById(program, TrackId{2}) == &program.tracks[0],
         "sequence program helper should resolve tracks by stable track id");
  expect(sourceCommandById(program.tracks[0], noteCommandId) == &program.tracks[0].commands[1],
         "sequence program helper should resolve source commands by stable command id");
  expect(sourceCommandForEvent(program, note->header) == &program.tracks[0].commands[1],
         "performance event source links should resolve back to source commands");

  const auto noteEvents = performanceEventsForCommand(renderedTrack, noteCommandId);
  expect(noteEvents.size() == 1 && noteEvents[0] == &renderedTrack.events[1],
         "performance helper should collect events emitted by one source command");
  expect(performanceTrackById(performance, TrackId{2}) == &performance.tracks[0],
         "performance helper should resolve rendered tracks by stable track id");
  expect(sourceCommandForEvent(program, PerformanceEventHeader{.sourceCommand = CommandId{99}, .track = TrackId{2}}) ==
             nullptr,
         "performance source-link helper should return null for a missing command");
}

void sequenceVmPreservesLoopsAsPerformanceMarkers() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{2},
      .sourceTrackNumber = 7,
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()),
                                       programBytes);
  const CommandId noteCommand =
      addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes).id;
  const CommandId jumpCommand =
      addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes).id;

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program, dialect);
  expect(performance.diagnostics.empty(), "preserve-loop VM fixture should not report diagnostics");
  expect(performance.tracks.size() == 1, "preserve-loop VM fixture should render one track");
  const PerformanceTrack& renderedTrack = performance.tracks[0];
  expect(renderedTrack.endTick == 12, "preserve-loop VM should stop after discovering the first runtime loop");

  const auto countNotesAt = [&](u64 tick) {
    return std::ranges::count_if(renderedTrack.events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == tick;
    });
  };
  expect(countNotesAt(0) == 1 && countNotesAt(12) == 0,
         "preserve-loop VM should emit one pass without replaying the loop body");

  const auto markerAt = [&](std::string_view text, u64 tick) -> const MarkerPerformanceEvent* {
    for (const auto& event : renderedTrack.events) {
      const auto* marker = std::get_if<MarkerPerformanceEvent>(&event);
      if (marker != nullptr && marker->text == text && marker->header.tick == tick) {
        return marker;
      }
    }
    return nullptr;
  };
  const MarkerPerformanceEvent* loopStart = markerAt("Loop Start", 0);
  const MarkerPerformanceEvent* loopEnd = markerAt("Loop End", 12);
  expect(loopStart != nullptr && loopStart->header.sourceCommand == noteCommand,
         "preserve-loop VM should link loop-start marker to the repeated command");
  expect(loopEnd != nullptr && loopEnd->header.sourceCommand == jumpCommand,
         "preserve-loop VM should link loop-end marker to the command that jumped back");

  const MidiSequence midi = PerformanceMidiRenderer().render(performance);
  const auto countMidiMarkers = [&](std::string_view text, u64 tick) {
    return std::ranges::count_if(midi.tracks[0].events, [text, tick](const MidiEvent& event) {
      const auto* marker = std::get_if<Marker>(&event);
      return marker != nullptr && marker->text == text && marker->tick == tick;
    });
  };
  expect(countMidiMarkers("Loop Start", 0) == 1 && countMidiMarkers("Loop End", 12) == 1,
         "performance MIDI renderer should preserve neutral loop markers");
}

void sequenceVmUsesDialectCommandLimitDefault() {
  const SequenceDialect dialect = probeSequenceDialect(SequenceProgramBehavior{
      .defaultLoopPolicy = LoopPolicy::Default,
      .commandLimit = 2,
  });
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 2> programBytes{0x80, 0x05};
  const std::array<u8, 3> noteBytes{0x90, 0x04, 0x0c};
  const std::array<u8, 3> jumpBytes{0xfe, 0x02, 0x00};
  addProbeCommand<ProbeProgramCommand>(builder, dialect, Address{0}, probeRange(0, programBytes.size()),
                                       programBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{2}, probeRange(2, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeJumpCommand>(builder, dialect, Address{5}, probeRange(5, jumpBytes.size()), jumpBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::Preserve).render(program, dialect);
  expect(performance.diagnostics.size() == 1 &&
             performance.diagnostics[0].message == "Sequence VM command limit reached",
         "sequence VM should use dialect command limit when the program has no override");
  expect(performance.tracks[0].events.size() == 2,
         "dialect command limit should stop execution before the looping jump command");
  expect(performance.tracks[0].endTick == 12, "command-limit stop should preserve ticks from commands already run");
}

void sequenceVmAllowsRepeatedCallsToSameSubroutine() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> callBytes{0xc0, 0x0a, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  const std::array<u8, 3> noteBytes{0x90, 0x05, 0x04};
  const std::array<u8, 1> returnBytes{0xfd};
  addProbeCommand<ProbeCallCommand>(builder, dialect, Address{0}, probeRange(0, callBytes.size()), callBytes);
  addProbeCommand<ProbeCallCommand>(builder, dialect, Address{3}, probeRange(3, callBytes.size()), callBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{6}, probeRange(6, endBytes.size()), endBytes);
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{10}, probeRange(10, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeReturnCommand>(builder, dialect, Address{13}, probeRange(13, returnBytes.size()), returnBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "sequence VM repeated-call fixture should not report diagnostics");
  expect(performance.tracks[0].events.size() == 2,
         "sequence VM should allow two call sites to reuse the same subroutine");
  expect(performance.tracks[0].endTick == 8, "sequence VM should return from both subroutine calls");

  for (u64 tick : {0ULL, 4ULL}) {
    const bool found = std::ranges::any_of(performance.tracks[0].events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == tick && note->durationTicks == 4;
    });
    expect(found, "sequence VM should emit the shared subroutine note at tick " + std::to_string(tick));
  }
}

void sequenceVmReplaysFiniteRepeatBlocks() {
  const SequenceDialect dialect = probeSequenceDialect();
  TrackProgram track{
      .id = TrackId{0},
      .startAddress = Address{0},
  };
  TrackProgramBuilder builder{track};

  const std::array<u8, 3> noteBytes{0x90, 0x00, 0x0c};
  const std::array<u8, 5> repeatBytes{0xf0, 0x00, 0x03, 0x00, 0x00};
  const std::array<u8, 1> endBytes{0xff};
  addProbeCommand<ProbeNoteCommand>(builder, dialect, Address{0}, probeRange(0, noteBytes.size()), noteBytes);
  addProbeCommand<ProbeRepeatCommand>(builder, dialect, Address{3}, probeRange(3, repeatBytes.size()), repeatBytes);
  addProbeCommand<ProbeEndCommand>(builder, dialect, Address{8}, probeRange(8, endBytes.size()), endBytes);

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "sequence VM finite repeat fixture should not report diagnostics");
  expect(performance.tracks[0].events.size() == 3, "sequence VM should replay a finite repeat block");
  expect(performance.tracks[0].endTick == 36, "sequence VM should advance through each repeated note");

  for (u64 tick : {0ULL, 12ULL, 24ULL}) {
    const bool found = std::ranges::any_of(performance.tracks[0].events, [tick](const PerformanceEvent& event) {
      const auto* note = std::get_if<NotePerformanceEvent>(&event);
      return note != nullptr && note->header.tick == tick;
    });
    expect(found, "sequence VM should emit repeated notes at each playback tick");
  }
}

void projectSessionScansValuesAndVirtualSources() {
  Session session;
  session.formats().add(probeSequenceModule());
  session.formats().add(probeMiscModule());

  const auto sourceId = session.addSource(SourceFile{.name = "probe.spc"}, {0xaa, 0x34, 0x12});
  expect(sourceId == SourceId{0}, "first source should get SourceId 0");

  Project project = session.scan();
  expect(project.sources.size() == 2, "scan should include extracted virtual source");
  expect(project.sources[1].virtualized, "extracted source should be virtualized");
  expect(project.sources[1].origin.has_value() && project.sources[1].origin->source == sourceId &&
             project.sources[1].origin->offset == 0 && project.sources[1].origin->size == 1,
         "extracted virtual source should preserve its origin range");
  expect(project.assets.size() == 2, "scan should produce sequence and misc assets");
  expect(project.collections.size() == 1, "scan should produce one collection");
  expect(project.diagnostics.size() == 1, "scan should preserve module diagnostics");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets[0]);
  expect(sequence != nullptr, "first asset should be a sequence");
  expect(sequence->metadata.id == AssetId{0}, "sequence should keep allocated asset id");
  expect(assetById(project, sequence->metadata.id) == &project.assets[0],
         "project snapshot should find an asset by stable id");
  expect(assetById<SequenceProgramAsset>(project, sequence->metadata.id) == sequence,
         "project snapshot should find a sequence program asset by stable id");
  expect(assetById<MiscAsset>(project, sequence->metadata.id) == nullptr,
         "project snapshot should reject asset id lookups with the wrong value type");
  expect(assetById(project, AssetId{99}) == nullptr, "project snapshot should return null for a missing asset id");
  expect(assetById<SequenceProgramAsset>(project, AssetId{99}) == nullptr,
         "project snapshot should return null for a missing asset id");
  expect(sequence->metadata.items.nodes.size() == 2, "sequence should expose item tree");
  expect(sequence->metadata.items.root == sequence->metadata.items.nodes[0].id,
         "scanner should preserve valid item tree root");
  expect(sequence->metadata.items.nodes[0].children == std::vector<ItemId>{sequence->metadata.items.nodes[1].id},
         "scanner should rebuild item children from parent links");
  expect(itemById(sequence->metadata.items, sequence->metadata.items.nodes[0].id) == &sequence->metadata.items.nodes[0],
         "item tree should find its root item by stable id");
  expect(itemById(sequence->metadata.items, sequence->metadata.items.nodes[1].id) == &sequence->metadata.items.nodes[1],
         "item tree should find child items by stable id");
  expect(itemById(sequence->metadata.items, ItemId{99}) == nullptr,
         "item tree should return null for a missing item id");
  expect(project.collections[0].sequence == sequence->metadata.id, "collection should reference sequence asset");
  expect(collectionById(project, project.collections[0].id) == &project.collections[0],
         "project snapshot should find a collection by stable id");
  expect(collectionById(project, CollectionId{99}) == nullptr,
         "project snapshot should return null for a missing collection id");

  const auto* misc = std::get_if<MiscAsset>(&project.assets[1]);
  expect(misc != nullptr, "second asset should be misc from virtual source");
  expect(metadata(project.assets[1]).id == AssetId{1}, "missing asset id should be assigned");

  project = session.scan();
  expect(project.sources.size() == 2, "rescan should replace, not duplicate, virtual tail sources");
}

void projectCollectionAssetResolutionProvidesTypedExportInputs() {
  Project project;
  project.assets.emplace_back(SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{0},
              .format = "Probe",
              .name = "Sequence",
          },
      .program =
          SequenceProgram{
              .dialect = DialectId{.value = "probe"},
              .timebase = Timebase{.ppqn = 48},
          },
  });
  project.assets.emplace_back(InstrumentSetAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Instruments",
          },
  });
  project.assets.emplace_back(SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Samples",
          },
  });
  project.assets.emplace_back(MiscAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{3},
              .format = "Probe",
              .name = "Misc",
          },
  });
  project.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Full",
      .sequence = AssetId{0},
      .instrumentSets = {AssetId{1}, AssetId{41}},
      .sampleCollections = {AssetId{2}, AssetId{42}},
      .miscAssets = {AssetId{3}, AssetId{43}},
  });
  project.collections.push_back(Collection{
      .id = CollectionId{1},
      .name = "Samples Only",
      .sampleCollections = {AssetId{2}},
  });

  const auto full = resolveCollectionAssets(project, CollectionId{0});
  expect(full.collection == &project.collections[0], "collection asset resolver should preserve the collection");
  expect(full.sequenceProgram == std::get_if<SequenceProgramAsset>(&project.assets[0]),
         "collection asset resolver should resolve the typed sequence program asset");
  expect(
      full.instrumentSets.size() == 1 && full.instrumentSets[0] == std::get_if<InstrumentSetAsset>(&project.assets[1]),
      "collection asset resolver should resolve typed instrument set assets");
  expect(full.sampleCollections.size() == 1 &&
             full.sampleCollections[0] == std::get_if<SampleCollectionAsset>(&project.assets[2]),
         "collection asset resolver should resolve typed sample collection assets");
  expect(full.miscAssets.size() == 1 && full.miscAssets[0] == std::get_if<MiscAsset>(&project.assets[3]),
         "collection asset resolver should resolve typed misc assets");
  expect(full.diagnostics.sequence.empty(), "valid sequence references should not produce diagnostics");
  expect(full.diagnostics.instrumentSets.size() == 1,
         "collection asset resolver should report broken instrument references separately");
  expect(full.diagnostics.sampleCollections.size() == 1,
         "collection asset resolver should report broken sample references separately");
  expect(full.diagnostics.miscAssets.size() == 1,
         "collection asset resolver should report broken misc references separately");
  expect(full.diagnostics.all().size() == 3, "collection asset resolver should aggregate reference diagnostics");

  const auto samplesOnly = resolveCollectionAssets(project, CollectionId{1});
  expect(samplesOnly.collection == &project.collections[1],
         "collection asset resolver should resolve sample-only collections");
  expect(samplesOnly.sequenceProgram == nullptr, "sample-only collections should not report a sequence asset");
  expect(samplesOnly.diagnostics.sequence.empty(),
         "absent optional sequence references should not be treated as broken references");
  expect(samplesOnly.sampleCollections.size() == 1,
         "sample-only collections should still resolve their sample collections");

  const auto missing = resolveCollectionAssets(project, CollectionId{99});
  expect(missing.collection == nullptr, "missing collection resolver result should not expose a collection");
  expect(missing.diagnostics.collection.size() == 1,
         "missing collection resolver result should report a collection diagnostic");
}

void projectSessionAddsSourceFromPath() {
  const auto path = std::filesystem::temp_directory_path() / "vgmtrans-value-core-source-load.bin";
  std::filesystem::remove(path);
  {
    std::ofstream out(path, std::ios::binary);
    out.put(static_cast<char>(0xaa));
    out.put(static_cast<char>(0x34));
    out.put(static_cast<char>(0x12));
  }

  Session session;
  session.formats().add(probeSequenceModule());

  const auto sourceId = session.addSourceFromPath(path);
  expect(sourceId == SourceId{0}, "path source should get SourceId 0");
  expect(session.sources().source(sourceId).name == path.filename().string(),
         "path source should use the filename as source name");
  expect(session.sources().source(sourceId).path == path, "path source should preserve filesystem path");
  const std::array<u8, 3> expectedBytes{0xaa, 0x34, 0x12};
  expect(std::ranges::equal(session.sources().bytes(sourceId), expectedBytes),
         "path source should preserve file bytes");

  const Project project = session.scan();
  expect(project.collections.size() == 1, "path source should scan through registered modules");
  expect(project.sources.front().path == path, "project snapshot should preserve path source metadata");

  std::filesystem::remove(path);
}

void projectSessionExportsAllCollections() {
  Session session;
  session.formats().add(probeSequenceModule());

  session.addSource(SourceFile{.name = "first.probe"}, {0xaa});
  session.addSource(SourceFile{.name = "second.probe"}, {0xaa});
  const Project project = session.scan();
  expect(project.collections.size() == 2, "probe sources should produce two collections");

  const auto exports = session.exportAllCollections(ExportRequest{
      .kinds = {ExportKind::Midi},
  });
  expect(exports.size() == project.collections.size(), "all-collection export should cover every collection");

  for (size_t i = 0; i < exports.size(); ++i) {
    expect(exports[i].collection == project.collections[i].id,
           "all-collection export should preserve collection ids in project order");
    expect(exports[i].artifacts.size() == 1, "probe MIDI export should return one artifact per collection");
    expect(exports[i].artifacts[0].filename == project.collections[i].name + ".mid",
           "collection export should keep collection-derived artifact names");
    expect(exports[i].artifacts[0].mediaType == "audio/midi", "collection export should keep artifact media types");
    expect(!exports[i].artifacts[0].diagnostics.empty(),
           "collection export diagnostics should stay attached to the artifact");
  }
}

void snesBrrDecoderProducesPcm() {
  const std::vector<u8> sourceBytes{0x01, 0, 0, 0, 0, 0, 0, 0, 0};
  const Sample sample{
      .name = "zero",
      .codec = AudioCodec::SnesBrr,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 0, .size = sourceBytes.size()},
      .sampleRate = 32000,
  };

  const auto registry = SampleDecoderRegistry::withDefaultDecoders();
  const auto copy = registry;
  const auto decoded = registry.decode(sample, sourceBytes);
  expect(decoded.has_value(), "BRR decoder should decode a valid sample");
  expect(decoded->sampleRate == 32000, "decoded sample should preserve sample rate");
  expect(decoded->pcm.size() == 16, "one BRR block should decode to 16 samples");
  expect(std::ranges::all_of(decoded->pcm, [](s16 sample) { return sample == 0; }),
         "zero BRR block should decode to silence");

  const Sample invalidRange = Sample{
      .name = "invalid",
      .codec = AudioCodec::SnesBrr,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 8, .size = 9},
  };
  expect(!copy.decode(invalidRange, sourceBytes).has_value(), "BRR decoder should reject invalid source ranges");

  bool threw = false;
  try {
    SampleDecoderRegistry custom;
    custom.add(SampleDecoder{.codec = AudioCodec::SnesBrr});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "sample decoder registry should reject incomplete decoder values");
}

void midiExporterWritesStandardMidiFile() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {MidiTrack{
          .name = "Lead",
          .events =
              {
                  Tempo{.tick = 0, .microsecondsPerQuarter = 500000},
                  ProgramChange{.tick = 0, .channel = 0, .program = 5},
                  Volume{.tick = 0, .channel = 0, .value = 100},
                  NoteDuration{.tick = 0, .channel = 0, .key = 60, .velocity = 100, .duration = 24},
                  Pan{.tick = 12, .channel = 0, .value = 64},
                  EndOfTrack{.tick = 24},
              },
      }},
  };

  const std::vector<u8> expected{
      'M',  'T',  'h',  'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 'M',
      'T',  'r',  'k',  0x00, 0x00, 0x00, 0x26, 0x00, 0xff, 0x03, 0x04, 'L',  'e',  'a',  'd',
      0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20, 0x00, 0xc0, 0x05, 0x00, 0xb0, 0x07, 0x64, 0x00,
      0x90, 0x3c, 0x64, 0x0c, 0xb0, 0x0a, 0x40, 0x0c, 0x80, 0x3c, 0x40, 0x00, 0xff, 0x2f, 0x00,
  };

  const auto exported = MidiExporter().exportMidi(midiSequence);
  expect(exported == expected, "MIDI exporter should write expected SMF bytes");
}

void performanceMidiRendererExtendsPreviousSameKeyNotes() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .id = TrackId{0},
          .sourceTrackNumber = 2,
          .endTick = 24,
          .events =
              {
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 0},
                      .key = 60.0,
                      .velocity = 0.75,
                      .durationTicks = 12,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 12},
                      .key = 60.0,
                      .velocity = 0.5,
                      .durationTicks = 6,
                      .extendsPrevious = true,
                  },
                  NotePerformanceEvent{
                      .header = PerformanceEventHeader{.tick = 18},
                      .key = 62.0,
                      .velocity = 0.5,
                      .durationTicks = 6,
                      .extendsPrevious = true,
                  },
              },
      }},
  };

  const MidiSequence midiSequence = PerformanceMidiRenderer().render(performance);
  expect(midiSequence.tracks.size() == 1, "performance renderer should preserve tracks");
  const auto& events = midiSequence.tracks[0].events;
  const auto firstNote = std::get_if<NoteDuration>(&events[0]);
  const auto secondNote = std::get_if<NoteDuration>(&events[1]);
  expect(firstNote != nullptr && firstNote->tick == 0 && firstNote->key == 60 && firstNote->duration == 18,
         "performance renderer should extend a previous same-key note");
  expect(secondNote != nullptr && secondNote->tick == 18 && secondNote->key == 62 && secondNote->duration == 6,
         "performance renderer should emit a new note when no matching previous key exists");
  expect(std::get<EndOfTrack>(events.back()).tick == 24, "performance renderer should preserve track end ticks");
}

void modulationAnalysisReportsObservedMidiControllerRanges() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks =
          {
              MidiTrack{
                  .name = "Lead",
                  .events =
                      {
                          VibratoDepth{.tick = 0, .channel = 0, .value = 0},
                          VibratoDepth{.tick = 12, .channel = 0, .value = 82},
                          VibratoFrequency{.tick = 12, .channel = 0, .value = 17},
                          TremoloDepth{.tick = 24, .channel = 0, .value = 40},
                          TremoloFrequency{.tick = 24, .channel = 0, .value = 5},
                      },
              },
              MidiTrack{
                  .name = "Pad",
                  .events =
                      {
                          VibratoFrequency{.tick = 0, .channel = 1, .value = 29},
                          TremoloFrequency{.tick = 0, .channel = 1, .value = 9},
                      },
              },
          },
  };

  const auto usage = analyzeMidiModulationUsage(midiSequence);
  expect(hasMidiModulationUsage(usage), "MIDI modulation analysis should report observed controller modulation");
  expect(usage.tracks.size() == 2, "MIDI modulation analysis should preserve track-level results");
  expect(usage.vibratoDepth.observed && usage.vibratoDepth.min == 0 && usage.vibratoDepth.max == 82,
         "MIDI modulation analysis should report global vibrato depth controller range");
  expect(usage.vibratoRate.observed && usage.vibratoRate.min == 17 && usage.vibratoRate.max == 29,
         "MIDI modulation analysis should report global vibrato rate controller range");
  expect(usage.tremoloDepth.observed && usage.tremoloDepth.min == 40 && usage.tremoloDepth.max == 40,
         "MIDI modulation analysis should report global tremolo depth controller range");
  expect(usage.tremoloRate.observed && usage.tremoloRate.min == 5 && usage.tremoloRate.max == 9,
         "MIDI modulation analysis should report global tremolo rate controller range");
  expect(usage.tracks[0].trackIndex == 0 && usage.tracks[0].vibratoDepth.max == 82 &&
             usage.tracks[0].vibratoRate.max == 17,
         "MIDI modulation analysis should keep first track modulation ranges separate");
  expect(usage.tracks[1].trackIndex == 1 && !usage.tracks[1].vibratoDepth.observed &&
             usage.tracks[1].vibratoRate.max == 29,
         "MIDI modulation analysis should keep second track modulation ranges separate");
}

void modulationAnalysisReportsObservedPerformanceRanges() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks =
          {
              PerformanceTrack{
                  .id = TrackId{0},
                  .sourceTrackNumber = 0,
                  .events =
                      {
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::VibratoDepth,
                              .amount = 0.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 12},
                              .target = ModulationPerformanceTarget::VibratoDepth,
                              .amount = 82.0 / 127.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 12},
                              .target = ModulationPerformanceTarget::VibratoRate,
                              .amount = 17.0 / 127.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 24},
                              .target = ModulationPerformanceTarget::TremoloDepth,
                              .amount = 40.0 / 127.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 24},
                              .target = ModulationPerformanceTarget::TremoloRate,
                              .amount = 5.0 / 127.0,
                          },
                      },
              },
              PerformanceTrack{
                  .id = TrackId{1},
                  .sourceTrackNumber = 1,
                  .events =
                      {
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::VibratoRate,
                              .amount = 29.0 / 127.0,
                          },
                          ModulationPerformanceEvent{
                              .header = PerformanceEventHeader{.tick = 0},
                              .target = ModulationPerformanceTarget::TremoloRate,
                              .amount = 9.0 / 127.0,
                          },
                      },
              },
          },
  };

  const auto usage = analyzePerformanceModulationUsage(performance);
  expect(hasMidiModulationUsage(usage), "performance modulation analysis should report observed driver modulation");
  expect(usage.tracks.size() == 2, "performance modulation analysis should preserve track-level results");
  expect(usage.vibratoDepth.observed && usage.vibratoDepth.min == 0 && usage.vibratoDepth.max == 82,
         "performance modulation analysis should report global vibrato depth range");
  expect(usage.vibratoRate.observed && usage.vibratoRate.min == 17 && usage.vibratoRate.max == 29,
         "performance modulation analysis should report global vibrato rate range");
  expect(usage.tremoloDepth.observed && usage.tremoloDepth.min == 40 && usage.tremoloDepth.max == 40,
         "performance modulation analysis should report global tremolo depth range");
  expect(usage.tremoloRate.observed && usage.tremoloRate.min == 5 && usage.tremoloRate.max == 9,
         "performance modulation analysis should report global tremolo rate range");
  expect(usage.tracks[0].trackIndex == 0 && usage.tracks[0].vibratoDepth.max == 82 &&
             usage.tracks[0].vibratoRate.max == 17,
         "performance modulation analysis should keep first track modulation ranges separate");
  expect(usage.tracks[1].trackIndex == 1 && !usage.tracks[1].vibratoDepth.observed &&
             usage.tracks[1].vibratoRate.max == 29,
         "performance modulation analysis should keep second track modulation ranges separate");
}

void observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators() {
  MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks =
          {
              MidiTrack{
                  .name = "Lead",
                  .events =
                      {
                          VibratoDepth{.tick = 0, .channel = 0, .value = 0},
                          VibratoDepth{.tick = 6, .channel = 0, .value = 41},
                          VibratoDepth{.tick = 12, .channel = 0, .value = 82},
                          VibratoFrequency{.tick = 18, .channel = 0, .value = 17},
                          TremoloDepth{.tick = 24, .channel = 0, .value = 40},
                          TremoloFrequency{.tick = 30, .channel = 0, .value = 5},
                          TremoloFrequency{.tick = 36, .channel = 0, .value = 9},
                      },
              },
              MidiTrack{
                  .name = "Pad",
                  .events =
                      {
                          VibratoFrequency{.tick = 0, .channel = 1, .value = 29},
                      },
              },
          },
  };

  const auto usage = analyzeMidiModulationUsage(midiSequence);
  expect(scaledMidiModulationControllerValue(41, &usage.vibratoDepth, ModulationScalingPolicy::FullFormatRange) == 41,
         "full-range modulation scaling should leave MIDI controller values unchanged");

  applyMidiModulationScaling(midiSequence, usage, ModulationScalingPolicy::ObservedSequenceRange);

  const auto& leadEvents = midiSequence.tracks[0].events;
  expect(std::get<VibratoDepth>(leadEvents[0]).value == 0,
         "observed modulation scaling should preserve zero controller values");
  expect(std::get<VibratoDepth>(leadEvents[1]).value == 64,
         "observed modulation scaling should expand intermediate controller values");
  expect(std::get<VibratoDepth>(leadEvents[2]).value == 127,
         "observed modulation scaling should expand the observed maximum to full MIDI controller range");
  expect(std::get<VibratoFrequency>(leadEvents[3]).value == 74,
         "observed modulation scaling should use global rate range across tracks");
  expect(std::get<TremoloDepth>(leadEvents[4]).value == 127,
         "observed modulation scaling should expand tremolo depth controllers");
  expect(
      std::get<TremoloFrequency>(leadEvents[5]).value == 71 && std::get<TremoloFrequency>(leadEvents[6]).value == 127,
      "observed modulation scaling should expand tremolo rate controllers");

  const SynthModulator defaultTremoloRate{
      .destination = SynthDestination::TremoloRate,
      .amount = 180,
  };
  const SynthModulator explicitVibratoDepth{
      .source = SynthSource::NoteOnVelocity,
      .destination = SynthDestination::VibratoDepth,
      .amount = 300,
  };
  expect(scaledSynthModulatorAmount(defaultTremoloRate, &usage, ModulationScalingPolicy::ObservedSequenceRange) == 13,
         "observed modulation scaling should reduce default synth modulator amounts");
  expect(
      scaledSynthModulatorAmount(explicitVibratoDepth, &usage, ModulationScalingPolicy::ObservedSequenceRange) == 300,
      "observed modulation scaling should not change explicit-source synth modulator amounts");
}

void wavExporterWritesPcm16RiffFile() {
  const DecodedSample sample{
      .sampleRate = 8000,
      .channels = 1,
      .pcm = {-32768, 0, 32767},
  };

  const std::vector<u8> expected{
      'R',  'I',  'F',  'F',  0x2a, 0x00, 0x00, 0x00, 'W',  'A',  'V',  'E',  'f',  'm',  't',  ' ',  0x10,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x40, 0x1f, 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x02, 0x00,
      0x10, 0x00, 'd',  'a',  't',  'a',  0x06, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0xff, 0x7f,
  };

  expect(WavExporter().exportPcm16(sample) == expected, "WAV exporter should write expected PCM16 RIFF bytes");
}

void soundFontExporterWritesSfbkRiffFile() {
  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  SampleCollectionAsset sampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Probe Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = sourceId, .offset = 0, .size = 9},
                  .sampleRate = 16000,
                  .loop = Loop{.enabled = true, .start = 0, .length = 16},
              }},
          },
  };
  InstrumentSetAsset instrumentSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Probe Instruments",
          },
      .instruments = {Instrument{
          .bank = 1,
          .program = 5,
          .name = "Lead",
          .regions = {Region{
              .keyRange = KeyRange{.low = 24, .high = 96},
              .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
              .tuning = Tuning{.cents = 125},
              .envelope =
                  Envelope{
                      .attack = 1'000'000,
                      .decay = 2'000'000,
                      .sustain = 500,
                      .release = 250'000,
                  },
              .pan = 1.0,
          }},
          .generators =
              {
                  SynthGenerator{.destination = SynthDestination::VibratoDepth, .amount = 120},
                  SynthGenerator{.destination = SynthDestination::VibratoRate, .amount = 240},
              },
          .modulators =
              {
                  SynthModulator{
                      .source = SynthSource::NoteOnVelocity,
                      .destination = SynthDestination::VibratoDepth,
                      .amount = 300,
                  },
                  SynthModulator{
                      .source = SynthSource::ChannelPressure,
                      .destination = SynthDestination::VibratoRate,
                      .amount = 0,
                  },
                  SynthModulator{
                      .destination = SynthDestination::TremoloRate,
                      .amount = 180,
                  },
              },
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&instrumentSet};
  const std::array<const SampleCollectionAsset*, 1> samples{&sampleCollection};
  const MidiModulationUsage midiModulationUsage{
      .vibratoDepth = ObservedValueRange{.observed = true, .min = 4, .max = 38},
      .vibratoRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
      .tremoloDepth = ObservedValueRange{.observed = true, .min = 2, .max = 24},
      .tremoloRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
  };
  const auto result = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = samples,
          .midiModulationUsage = &midiModulationUsage,
          .modulationScaling = ModulationScalingPolicy::ObservedSequenceRange,
      },
      sources);

  expect(result.diagnostics.empty(), "SoundFont export should not report diagnostics for valid values");
  expect(result.bytes.size() > 44, "SoundFont export should produce RIFF bytes");
  expect(std::vector<u8>(result.bytes.begin(), result.bytes.begin() + 4) == std::vector<u8>{'R', 'I', 'F', 'F'},
         "SoundFont export should start with RIFF");
  expect(readLe32(result.bytes, 4) == result.bytes.size() - 8, "SoundFont RIFF size should match file size");
  expect(std::vector<u8>(result.bytes.begin() + 8, result.bytes.begin() + 12) == std::vector<u8>{'s', 'f', 'b', 'k'},
         "SoundFont RIFF type should be sfbk");
  expect(containsAscii(result.bytes, "INFO"), "SoundFont export should include INFO list");
  expect(containsAscii(result.bytes, "sdta"), "SoundFont export should include sample data list");
  expect(containsAscii(result.bytes, "pdta"), "SoundFont export should include preset data list");
  expect(containsAscii(result.bytes, "smpl"), "SoundFont export should include smpl chunk");
  expect(containsAscii(result.bytes, "phdr"), "SoundFont export should include phdr chunk");
  expect(containsAscii(result.bytes, "inst"), "SoundFont export should include inst chunk");
  expect(containsAscii(result.bytes, "shdr"), "SoundFont export should include shdr chunk");
  expect(containsAscii(result.bytes, "Lead"), "SoundFont export should include instrument name");
  expect(containsAscii(result.bytes, "Zero"), "SoundFont export should include sample name");
  expect(chunkSize(result.bytes, "smpl") == 124, "SoundFont smpl chunk should include PCM and SF2 padding samples");
  expect(chunkSize(result.bytes, "pgen") == 12,
         "SoundFont pgen chunk should include reverb, instrument, and terminal generators");
  expect(soundFontBagAt(result.bytes, "pbag", 1, 2, 0),
         "SoundFont terminal preset bag should include both preset generators");
  expect(soundFontPgenContainsAmount(result.bytes, 16, 250),
         "SoundFont export should write default preset reverb send");
  expect(chunkSize(result.bytes, "ibag") == 12, "SoundFont ibag chunk should include a global generator zone");
  expect(soundFontBagAt(result.bytes, "ibag", 0, 0, 0), "SoundFont global zone should start at generator index 0");
  expect(soundFontBagAt(result.bytes, "ibag", 1, 2, 3),
         "SoundFont region zone should start after instrument generators and modulators");
  expect(soundFontBagAt(result.bytes, "ibag", 2, 16, 3),
         "SoundFont terminal bag should include all generators and modulators");
  expect(chunkSize(result.bytes, "imod") == 40, "SoundFont imod chunk should include modulators plus terminal");
  expect(soundFontImodContains(result.bytes, 2, 6, 300),
         "SoundFont export should write explicit velocity-to-vibrato modulator");
  expect(soundFontImodContains(result.bytes, 13, 24, 0),
         "SoundFont export should write explicit channel-pressure-to-vibrato-rate modulator");
  expect(soundFontImodContains(result.bytes, 203, 22, 17),
         "SoundFont export should scale default tremolo-rate modulator from observed MIDI usage");
  expect(chunkSize(result.bytes, "igen") == 68, "SoundFont igen chunk should include global and region generators");
  expect(chunkSize(result.bytes, "shdr") == 92, "SoundFont shdr chunk should include one sample and terminal record");
  expect(soundFontIgenContainsAmount(result.bytes, 6, 120),
         "SoundFont export should write instrument vibrato depth generator");
  expect(soundFontIgenContainsAmount(result.bytes, 24, 240),
         "SoundFont export should write instrument vibrato rate generator");
  expect(soundFontIgenContainsAmount(result.bytes, 34, 0),
         "SoundFont export should write attackVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 35, -32768),
         "SoundFont export should write holdVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 36, 1200),
         "SoundFont export should write decayVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 37, 60),
         "SoundFont export should write sustainVolEnv from Region envelope");
  expect(soundFontIgenContainsAmount(result.bytes, 38, -2400),
         "SoundFont export should write releaseVolEnv from Region envelope");
}

void dlsExporterWritesDlsRiffFile() {
  SourceStore sources;
  const auto sourceId = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  SampleCollectionAsset sampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Probe Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = SourceRange{.source = sourceId, .offset = 0, .size = 9},
                  .sampleRate = 16000,
                  .loop = Loop{.enabled = true, .start = 0, .length = 16},
              }},
          },
  };
  InstrumentSetAsset instrumentSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Probe Instruments",
          },
      .instruments = {Instrument{
          .bank = 1,
          .program = 5,
          .name = "Lead",
          .regions = {Region{
              .keyRange = KeyRange{.low = 24, .high = 96},
              .sample = SampleRef{.collection = sampleCollection.metadata.id, .index = 0},
              .tuning = Tuning{.cents = 125},
              .envelope =
                  Envelope{
                      .attack = 1'000'000,
                      .decay = 2'000'000,
                      .sustain = 500,
                      .release = 250'000,
                  },
              .pan = 1.0,
          }},
          .generators =
              {
                  SynthGenerator{.destination = SynthDestination::VibratoDepth, .amount = 120},
                  SynthGenerator{.destination = SynthDestination::VibratoRate, .amount = 240},
              },
          .modulators =
              {
                  SynthModulator{
                      .source = SynthSource::NoteOnVelocity,
                      .destination = SynthDestination::VibratoDepth,
                      .amount = 300,
                  },
                  SynthModulator{
                      .source = SynthSource::ChannelPressure,
                      .destination = SynthDestination::VibratoRate,
                      .amount = 0,
                  },
                  SynthModulator{
                      .destination = SynthDestination::TremoloRate,
                      .amount = 180,
                  },
              },
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&instrumentSet};
  const std::array<const SampleCollectionAsset*, 1> samples{&sampleCollection};
  const MidiModulationUsage midiModulationUsage{
      .vibratoDepth = ObservedValueRange{.observed = true, .min = 4, .max = 38},
      .vibratoRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
      .tremoloDepth = ObservedValueRange{.observed = true, .min = 2, .max = 24},
      .tremoloRate = ObservedValueRange{.observed = true, .min = 5, .max = 12},
  };
  const auto result = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = samples,
          .midiModulationUsage = &midiModulationUsage,
          .modulationScaling = ModulationScalingPolicy::ObservedSequenceRange,
      },
      sources);

  expect(result.diagnostics.empty(), "DLS export should not report diagnostics for valid values");
  expect(result.bytes.size() > 44, "DLS export should produce RIFF bytes");
  expect(std::vector<u8>(result.bytes.begin(), result.bytes.begin() + 4) == std::vector<u8>{'R', 'I', 'F', 'F'},
         "DLS export should start with RIFF");
  expect(readLe32(result.bytes, 4) == result.bytes.size() - 8, "DLS RIFF size should match file size");
  expect(std::vector<u8>(result.bytes.begin() + 8, result.bytes.begin() + 12) == std::vector<u8>{'D', 'L', 'S', ' '},
         "DLS RIFF type should be DLS");
  expect(containsAscii(result.bytes, "colh"), "DLS export should include collection header");
  expect(containsAscii(result.bytes, "lins"), "DLS export should include instrument list");
  expect(containsAscii(result.bytes, "ptbl"), "DLS export should include pool table");
  expect(containsAscii(result.bytes, "wvpl"), "DLS export should include wave pool");
  expect(containsAscii(result.bytes, "wave"), "DLS export should include wave list");
  expect(containsAscii(result.bytes, "rgnh"), "DLS export should include region header");
  expect(containsAscii(result.bytes, "wsmp"), "DLS export should include sample metadata");
  expect(containsAscii(result.bytes, "wlnk"), "DLS export should include wave link");
  expect(containsAscii(result.bytes, "art2"), "DLS export should include region articulation");
  expect(containsAscii(result.bytes, "Lead"), "DLS export should include instrument name");
  expect(containsAscii(result.bytes, "Zero"), "DLS export should include sample name");
  expect(chunkSize(result.bytes, "colh") == 4, "DLS colh chunk should store one u32 count");
  expect(chunkSize(result.bytes, "ptbl") == 12, "DLS ptbl chunk should include one pool cue");
  expect(chunkSize(result.bytes, "data") == 32, "DLS data chunk should include decoded PCM bytes");
  expect(chunkSize(result.bytes, "art2") == 140,
         "DLS art2 chunk should include pan, envelope, generator, and modulator connections");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0206, 0),
         "DLS export should write EG1 attack time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x020c, std::numeric_limits<s32>::min()),
         "DLS export should write EG1 hold time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0207, 78643200),
         "DLS export should write EG1 decay time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x020a, 61425937),
         "DLS export should write EG1 sustain level from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0209, -157286400),
         "DLS export should write EG1 release time from Region envelope");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0009, 0x0003, 7864320),
         "DLS export should write instrument vibrato depth generator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0000, 0x0114, 15728640),
         "DLS export should write instrument vibrato rate generator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0009, 0x0002, 0x0003, 19660800),
         "DLS export should write explicit velocity-to-vibrato modulator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0008, 0x0114, 0),
         "DLS export should write explicit channel-pressure-to-vibrato-rate modulator");
  expect(dlsArt2ContainsConnection(result.bytes, 0x0008, 0x0104, 1114112),
         "DLS export should scale default tremolo-rate modulator from observed MIDI usage");
}

void exportDiagnosticsPreserveSourceRanges() {
  SourceStore sources;
  const auto validSource = sources.add(SourceFile{.name = "zero.brr"}, {0x01, 0, 0, 0, 0, 0, 0, 0, 0});

  const SourceRange missingSampleRange{.source = SourceId{99}, .offset = 0x12, .size = 9};
  SampleCollectionAsset missingSampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{2},
              .format = "Probe",
              .name = "Missing Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Missing",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = missingSampleRange,
              }},
          },
  };

  Project project;
  project.assets.push_back(missingSampleCollection);
  project.collections.push_back(Collection{
      .id = CollectionId{0},
      .name = "Probe",
      .sampleCollections = {missingSampleCollection.metadata.id},
  });

  SequenceDialectRegistry dialects;
  const auto wavArtifacts =
      exportCollection(project, sources, CollectionId{0}, ExportRequest{.kinds = {ExportKind::Wav}}, dialects);
  expect(wavArtifacts.size() == 1, "WAV export should return one artifact for one sample");
  expectDiagnosticRange(wavArtifacts[0].diagnostics, "Sample source was not found", missingSampleRange);

  const std::array<const SampleCollectionAsset*, 1> missingSamples{&missingSampleCollection};
  const auto sf2MissingSample = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .sampleCollections = missingSamples,
      },
      sources);
  expectDiagnosticRange(sf2MissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const auto dlsMissingSample = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .sampleCollections = missingSamples,
      },
      sources);
  expectDiagnosticRange(dlsMissingSample.diagnostics, "Sample source was not found", missingSampleRange);

  const SourceRange sampleRange{.source = validSource, .offset = 0, .size = 9};
  const SourceRange regionRange{.source = validSource, .offset = 0x40, .size = 6};
  SampleCollectionAsset validSampleCollection{
      .metadata =
          AssetMetadata{
              .id = AssetId{3},
              .format = "Probe",
              .name = "Valid Samples",
          },
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .name = "Zero",
                  .codec = AudioCodec::SnesBrr,
                  .encodedData = sampleRange,
                  .sampleRate = 16000,
              }},
          },
  };
  InstrumentSetAsset badRegionSet{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = "Probe",
              .name = "Bad Region Set",
          },
      .instruments = {Instrument{
          .bank = 0,
          .program = 0,
          .name = "Lead",
          .regions = {Region{
              .sample = SampleRef{.collection = validSampleCollection.metadata.id, .index = 9},
              .range = regionRange,
          }},
      }},
  };

  const std::array<const InstrumentSetAsset*, 1> instrumentSets{&badRegionSet};
  const std::array<const SampleCollectionAsset*, 1> validSamples{&validSampleCollection};
  const auto sf2BadRegion = SoundFontExporter().exportSoundFont(
      SoundFontInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = validSamples,
      },
      sources);
  expectDiagnosticRange(sf2BadRegion.diagnostics, "Region sample reference was not found", regionRange);

  const auto dlsBadRegion = DlsExporter().exportDls(
      DlsInput{
          .name = "Probe",
          .instrumentSets = instrumentSets,
          .sampleCollections = validSamples,
      },
      sources);
  expectDiagnosticRange(dlsBadRegion.diagnostics, "Region sample reference was not found", regionRange);
}

}  // namespace

int main() {
  try {
    formatRegistryStoresCopyableModuleValues();
    sequenceDialectRegistryStoresCopyableDialectValues();
    byteReaderChecksBoundsAndEndian();
    sourceCommandsPreserveBytesOperandsAndDialectDisplay();
    sequenceVmExecutesSourceCommandsAndStopsAtPlayOnceLoop();
    sequenceVmPreservesLoopsAsPerformanceMarkers();
    sequenceVmUsesDialectCommandLimitDefault();
    sequenceVmAllowsRepeatedCallsToSameSubroutine();
    sequenceVmReplaysFiniteRepeatBlocks();
    projectSessionScansValuesAndVirtualSources();
    projectCollectionAssetResolutionProvidesTypedExportInputs();
    projectSessionAddsSourceFromPath();
    projectSessionExportsAllCollections();
    snesBrrDecoderProducesPcm();
    midiExporterWritesStandardMidiFile();
    performanceMidiRendererExtendsPreviousSameKeyNotes();
    modulationAnalysisReportsObservedMidiControllerRanges();
    modulationAnalysisReportsObservedPerformanceRanges();
    observedModulationScalingRescalesMidiControllersAndDefaultSynthModulators();
    wavExporterWritesPcm16RiffFile();
    soundFontExporterWritesSfbkRiffFile();
    dlsExporterWritesDlsRiffFile();
    exportDiagnosticsPreserveSourceRanges();
    capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
    capcomSnesModuleScansSpcThroughVirtualAramSource();
    capcomSnesInstrumentTableSkipsBlankSlotsLikeLegacy();
    capcomSnesNoteStateCommandsAreTypedAndInterpreted();
    capcomSnesSourceDialectDecodesAndRendersDriverCommands();
    capcomSnesPanPerformanceCarriesGainCompensation();
    capcomSnesDialectEmitsSourceOnlyDriverSemantics();
    capcomSnesDialectEmitsPortamentoFromPreviousSourceKey();
    capcomSnesDialectExecutesRepeatUntilCommand();
    capcomSnesV1DialectPreservesUnknownOneByteEvents();
    ndsSequenceDialectDecodesAndRendersNoteWaitCommands();
    ndsSequenceDialectExecutesCallAndReturn();
    ndsSequenceDialectDiscoversSecondaryTrackStarts();
    ndsSequenceDialectPreservesIgnoredNoOpOperands();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
