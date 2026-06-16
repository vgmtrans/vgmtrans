/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

namespace {

void bytecodeMapRejectsIncompatibleHandlerReuse() {
  SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext> builder("probe-bytecode", ProbeSequenceContext{});
  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> map{"probe-bytecode", builder};

  map.op<0x10, ProbeProgramCommand>("Shared", suffix("shared"));

  bool threw = false;
  try {
    map.op<0x11, ProbeNoteCommand>("Shared", suffix("shared"));
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "bytecode map should reject one kind reused for different command types");
}

void bytecodeMapRejectsOpcodeRangeOverlap() {
  SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext> builder("probe-bytecode", ProbeSequenceContext{});
  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> map{"probe-bytecode", builder};

  map.op<0x12, ProbeProgramCommand>("Program");
  map.range<0x10, 0x20, ProbeNoteCommand>("Note");
  map.unknown<ProbeEndCommand>("End");

  bool threw = false;
  try {
    static_cast<void>(map.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "bytecode map should reject overlapping exact opcode and range declarations");
}

void bytecodeMapRequiresFallbackCommand() {
  SequenceDialectBuilder<ProbeTrackState, ProbeSequenceContext> builder("probe-bytecode", ProbeSequenceContext{});
  BytecodeMapBuilder<ProbeTrackState, ProbeSequenceContext> map{"probe-bytecode", builder};

  map.op<0x10, ProbeProgramCommand>("Program");

  bool threw = false;
  try {
    static_cast<void>(map.finish());
  } catch (const std::logic_error&) {
    threw = true;
  }
  expect(threw, "bytecode map should require an unknown or truncated fallback command");
}


void formatRegistryStoresCopyableModuleValues() {
  FormatRegistry registry;
  registry.add(probeSequenceModule());
  registry.add(FormatModule{
      .name = std::string("DynamicProbe"),
      .canScan = canScanProbeSequence,
      .scan = scanProbeSequence,
  });

  const FormatRegistry copy = registry;
  const std::array<u8, 1> probeBytes{0xaa};
  expect(copy.modules().size() == 2, "format registry should copy registered module values");
  expect(copy.modules()[0].name == "ProbeSequence",
         "format registry should preserve copied module names");
  expect(copy.modules()[1].name == "DynamicProbe",
         "format registry should own dynamically registered module names");
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


}  // namespace

void runValueRegistryTests() {
  bytecodeMapRejectsIncompatibleHandlerReuse();
  bytecodeMapRejectsOpcodeRangeOverlap();
  bytecodeMapRequiresFallbackCommand();
  formatRegistryStoresCopyableModuleValues();
  sequenceDialectRegistryStoresCopyableDialectValues();
}
