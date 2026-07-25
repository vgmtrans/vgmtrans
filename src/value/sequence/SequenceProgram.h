/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"
#include "value/model/SourceMap.h"

#include <limits>
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

using SemanticOperandValue = std::variant<bool, u64, s64, double, Address, std::string>;

struct CommandAction {
  static constexpr u32 kInvalidExecutor = std::numeric_limits<u32>::max();

  // CompilerCursor assigns this slot from a generated typed thunk. Arguments
  // contain its literal constants in evaluation order; deferred state-member
  // reads live in the thunk's type. Playback never looks up source-field names
  // and never needs the encoded command bytes.
  u32 executor = kInvalidExecutor;
  std::vector<SemanticOperandValue> arguments;

  [[nodiscard]] bool valid() const noexcept { return executor != kInvalidExecutor; }
};

struct CommandExecution {
  // One source command may perform several small actions. Keeping them ordered
  // lets format code state each effect honestly (for example, set state and
  // then emit a controller) without inventing a hidden compound operation.
  std::vector<CommandAction> actions;

  [[nodiscard]] bool valid() const noexcept { return !actions.empty(); }
};

// The role is intentionally small and format-independent. Operand names are the
// executor's precise vocabulary; roles let generic analysis and SourceMap
// projection recognize the few relationships shared by all drivers.
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
  InstrumentTablePointer,
};

struct SemanticOperand {
  // These values describe the source command for annotations and analysis.
  // Compiler-cursor playback uses CommandAction::arguments instead. The
  // optional encoded form is useful only when showing both raw and resolved
  // values materially improves the source presentation.
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

// One decoded source opcode. Commands retain source metadata, discovery flow,
// and source-free execution data. Encoded bytes remain in SourceStore and are
// reached through range when a view needs them.
struct SourceCommand {
  CommandId id;
  u8 opcode = 0;
  Address address;
  u32 encodedSize = 0;
  SourceRange range;
  SourceAnnotationId annotation;
  std::vector<SemanticOperand> operands;
  DecodeFlow flow;
  CommandExecution execution;
};

// Operand names are presentation vocabulary for source inspection and analysis.
// This lookup is not part of compiler-cursor playback.
[[nodiscard]] const SemanticOperand* semanticOperand(const SourceCommand& command, std::string_view name);

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
};

// Positional pan needs a source-domain law to define its channel gains.
// Unspecified is an internal sentinel used while program and dialect behavior
// are being resolved; emitting positional pan with it is an error.
enum class PanLaw {
  Unspecified,
  ConstantSum,
  EqualPower,
};

// Driver settings that affect playback but are not individual source commands,
// such as loop policy or initial channel state.
struct SequenceProgramBehavior {
  LoopPolicy defaultLoopPolicy = LoopPolicy::Default;
  // Zero means "use the next default": program -> dialect -> VM fallback.
  u32 commandLimit = 0;
  // Formats that emit a normalized pan position declare its law once here.
  // Formats with exact left/right gains should emit StereoBalance instead.
  PanLaw panLaw = PanLaw::Unspecified;
  // Some drivers rely on channel defaults that are not source opcodes. Keep
  // them in behavior so formats opt in explicitly and exporters can emit stable
  // initialization without attaching it to a fake source command.
  std::optional<double> initialLevel;
  std::optional<double> initialReverbSend;
  std::optional<u8> initialMonoModeChannels;
  std::optional<u8> initialPitchBendRangeSemitones;
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

  const SourceCommand& addSemantic(Address address, u8 opcode, u32 encodedSize, SourceRange range,
                                   std::vector<SemanticOperand> operands, DecodeFlow flow,
                                   SourceAnnotationId annotation = {}, CommandExecution execution = {});

private:
  TrackProgram& track_;
};

}  // namespace vgmtrans::core
