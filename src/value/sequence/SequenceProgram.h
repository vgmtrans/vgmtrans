/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"
#include "value/model/SourceMap.h"

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vgmtrans::core {

struct DialectId {
  std::string value;

  [[nodiscard]] bool valid() const noexcept { return !value.empty(); }
  friend bool operator==(const DialectId&, const DialectId&) noexcept = default;
};

struct ByteSpan {
  u32 offset = 0;
  u32 size = 0;
};

// Where decoding can continue after an opcode. Walkers use this before playback
// so jumps and calls can point at commands that have already been decoded.
struct DecodeFlow {
  enum class Kind {
    Fallthrough,
    Jump,
    Call,
    Return,
    Terminal,
  };

  Kind kind = Kind::Fallthrough;
  std::optional<Address> fallthrough;
  std::vector<Address> staticTargets;
  bool terminal = false;

  [[nodiscard]] static DecodeFlow fallthroughTo(Address next) {
    return DecodeFlow{
        .kind = Kind::Fallthrough,
        .fallthrough = next,
    };
  }

  [[nodiscard]] static DecodeFlow jump(Address destination) {
    return DecodeFlow{
        .kind = Kind::Jump,
        .staticTargets = {destination},
    };
  }

  [[nodiscard]] static DecodeFlow call(Address destination, Address returnAddress) {
    return DecodeFlow{
        .kind = Kind::Call,
        .fallthrough = returnAddress,
        .staticTargets = {destination},
    };
  }

  [[nodiscard]] static DecodeFlow return_() { return DecodeFlow{.kind = Kind::Return}; }

  [[nodiscard]] static DecodeFlow terminalFlow() {
    return DecodeFlow{
        .kind = Kind::Terminal,
        .terminal = true,
    };
  }

  [[nodiscard]] bool unconditionalJump() const noexcept { return kind == Kind::Jump && !staticTargets.empty(); }
  [[nodiscard]] bool callTarget() const noexcept { return kind == Kind::Call && !staticTargets.empty(); }
};

// Format-local numeric IDs keep the shared sequence core independent of every
// driver's command vocabulary while still giving executors a typed, byte-free
// instruction stream.
struct SemanticCommandKind {
  u32 value = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  friend constexpr bool operator==(SemanticCommandKind, SemanticCommandKind) noexcept = default;
};

struct SemanticOperandId {
  u32 value = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  friend constexpr bool operator==(SemanticOperandId, SemanticOperandId) noexcept = default;
};

using SemanticOperandValue = std::variant<bool, u64, s64, double, Address>;

// The role is intentionally small and format-independent. Format-local operand
// IDs remain the executor's precise vocabulary; roles let generic analysis and
// SourceMap projection recognize the few relationships shared by all drivers.
enum class SemanticOperandRole : u8 {
  Value,
  NoteKey,
  Duration,
  Pitch,
  Level,
  Pan,
  Modulation,
  State,
  Count,
  Address,
  JumpTarget,
  CallTarget,
  LoopTarget,
  RepeatTarget,
  Instrument,
  InstrumentBank,
  InstrumentProgram,
};

struct SemanticOperand {
  SemanticOperandId id;
  // value is the resolved value consumed by execution. encodedValue is present
  // when the source representation differs; the SourceMap then shows both
  // without making playback repeat the conversion.
  SemanticOperandValue value;
  SourceRange range;
  std::string name;
  SourceValueDisplay display = SourceValueDisplay::Default;
  SemanticOperandRole role = SemanticOperandRole::Value;
  std::optional<SemanticOperandValue> encodedValue;
  std::string encodedName;
  SourceValueDisplay encodedDisplay = SourceValueDisplay::Default;
};

[[nodiscard]] SourceValue semanticOperandSourceValue(const SemanticOperandValue& value);

// One decoded source opcode. Source bytes are retained only for legacy dialects.
// Semantic dialects execute kind/operands/flow and therefore cannot reparse the
// source during playback.
struct SourceCommand {
  CommandId id;
  u8 opcode = 0;
  Address address;
  u32 encodedSize = 0;
  SourceRange range;
  SourceAnnotationId annotation;
  ByteSpan bytes;
  SemanticCommandKind kind;
  std::vector<SemanticOperand> operands;
  DecodeFlow flow;
};

[[nodiscard]] const SemanticOperand* semanticOperand(const SourceCommand& command, SemanticOperandId id);

// Maps source addresses to command indexes. The VM uses this for jumps, calls,
// and finding the next command by source address when vector order is different.
struct AddressIndex {
  std::unordered_map<u64, u32> commandByAddress;

  void add(Address address, u32 commandIndex);
  [[nodiscard]] std::optional<u32> find(Address address) const;
};

struct TrackProgram {
  TrackId id;
  u32 sourceTrackNumber = 0;
  Address startAddress;
  std::vector<SourceCommand> commands;
  AddressIndex addressIndex;

  // Command bytes are pooled at track scope so the parsed snapshot avoids one
  // heap allocation per command.
  std::vector<u8> commandBytes;

  [[nodiscard]] std::span<const u8> bytesFor(const SourceCommand& command) const;
};

// Driver settings that affect playback but are not individual source commands,
// such as loop policy or initial channel state.
struct SequenceProgramBehavior {
  LoopPolicy defaultLoopPolicy = LoopPolicy::Default;
  // Zero means "use the next default": program -> dialect -> VM fallback.
  u32 commandLimit = 0;
  // Some legacy drivers rely on channel defaults that are not source opcodes.
  // Keep them in behavior so formats opt in explicitly and exporters can emit
  // stable initialization without attaching it to a fake source command.
  std::optional<double> initialLevel;
  std::optional<double> initialReverbSend;
  std::optional<u8> initialMonoModeChannels;
  std::optional<u8> initialPitchBendRangeSemitones;
  // Some tick-by-tick drivers stop every track as soon as any track exhausts
  // the export loop budget. Formats opt in when that global stop is part of
  // the source driver's playback model.
  bool stopAllTracksAtFirstLoop = false;
};

// Driver/profile selection belongs to the parsed program, not the registered
// executor. A single dialect can therefore execute every version of a format.
struct SequenceProgramConfig {
  u32 profile = 0;
};

struct SequenceProgram {
  DialectId dialect;
  Timebase timebase;
  Address sourceBaseAddress;
  SequenceProgramConfig config;
  SequenceProgramBehavior behavior;
  std::vector<TrackProgram> tracks;
};

[[nodiscard]] const TrackProgram* trackById(const SequenceProgram& program, TrackId id);
[[nodiscard]] const SourceCommand* sourceCommandById(const TrackProgram& track, CommandId id);

struct SequenceProgramAsset {
  AssetMetadata metadata;
  SequenceProgram program;
};

class TrackProgramBuilder {
public:
  explicit TrackProgramBuilder(TrackProgram& track);

  const SourceCommand& addDecoded(Address address, SourceRange range, std::span<const u8> bytes,
                                  SourceAnnotationId annotation = {}, DecodeFlow flow = {});
  const SourceCommand& addSemantic(Address address, u8 opcode, u32 encodedSize, SourceRange range,
                                   SemanticCommandKind kind, std::vector<SemanticOperand> operands, DecodeFlow flow,
                                   SourceAnnotationId annotation = {});

private:
  TrackProgram& track_;
};

}  // namespace vgmtrans::core
