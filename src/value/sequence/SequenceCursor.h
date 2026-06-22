/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/SourceMap.h"
#include "value/sequence/SequenceDialect.h"

#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::core {

enum class CommandPhase {
  Decode,
  Render,
};

enum class FlowKind : ::u8 {
  Next,
  Wait,
  Stop,
  End,
  Jump,
  Call,
  Return,
  ConditionalBranch,
  LoopCandidate,
  DeclaredLoop,
  CountedRepeatUntil,
  CountedRepeatBreak,
};

template <class T>
struct ReadValue {
  T value{};
  SourceRange range;
  bool valid = true;

  [[nodiscard]] explicit operator bool() const noexcept { return valid; }
  [[nodiscard]] operator T() const noexcept { return value; }
};

struct CommandFlow {
  FlowKind kind = FlowKind::Next;
  u32 waitTicks = 0;
  std::optional<Address> destination;
  ::u8 repeatSlot = 0;
  u32 repeatTotalPlays = 0;
  bool truncated = false;
  // Render mode can ask the VM to resolve a repeat branch before returning
  // here. Decode mode leaves this empty and records only static targets.
  std::optional<Effects> resolvedEffects;
};

class RepeatBreakFlow {
public:
  RepeatBreakFlow(CommandFlow flow, bool taken);

  [[nodiscard]] bool taken() const noexcept { return taken_; }
  [[nodiscard]] const CommandFlow& flow() const noexcept { return flow_; }
  [[nodiscard]] operator CommandFlow() const noexcept { return flow_; }

private:
  CommandFlow flow_;
  bool taken_ = false;
};

// VmCommandCursor is the readable command-authoring surface. It records source
// facts while reading bytes and returns VM-neutral flow decisions; the VM still
// owns playback policy such as loop counts and call stack behavior.
class VmCommandCursor {
public:
  VmCommandCursor(CommandPhase phase, SourceRange commandRange, std::span<const ::u8> bytes,
                  SourceMapBuilder* sourceMap = nullptr, std::vector<Diagnostic>* diagnostics = nullptr,
                  std::vector<CommandOperand>* operands = nullptr, CommandReferences* references = nullptr);

  [[nodiscard]] CommandPhase phase() const noexcept { return phase_; }
  [[nodiscard]] SourceId source() const noexcept { return commandRange_.source; }
  [[nodiscard]] Address address() const noexcept { return Address{static_cast<u32>(commandRange_.offset)}; }
  [[nodiscard]] u32 absolutePosition() const noexcept;
  [[nodiscard]] Address addressAtCursor() const noexcept { return Address{absolutePosition()}; }
  [[nodiscard]] ::u8 opcode() const noexcept { return bytes_.empty() ? 0 : bytes_.front(); }
  [[nodiscard]] SourceRange commandRange() const noexcept { return commandRange_; }
  [[nodiscard]] SourceAnnotationId annotation() const noexcept { return annotation_; }
  [[nodiscard]] size_t position() const noexcept { return position_; }
  [[nodiscard]] bool failed() const noexcept { return failed_; }
  [[nodiscard]] CommandKind commandKind(std::string_view kindPrefix) const;

  VmCommandCursor& name(std::string_view displayName);
  VmCommandCursor& name(std::string_view displayName, SequenceSemantic semantic);
  VmCommandCursor& name(std::string_view displayName, SequenceSemantic semantic, CommandPlaybackStatus status);
  VmCommandCursor& kind(std::string_view localKindOverride);
  VmCommandCursor& semantic(SequenceSemantic semantic);
  VmCommandCursor& playbackStatus(CommandPlaybackStatus status);
  VmCommandCursor& sourceOnly();
  VmCommandCursor& noOp();

  [[nodiscard]] ReadValue<::u8> u8(std::string_view name);
  [[nodiscard]] ReadValue<::s8> s8(std::string_view name);
  [[nodiscard]] ReadValue<u16> u16le(std::string_view name);
  [[nodiscard]] ReadValue<u16> u16be(std::string_view name);
  [[nodiscard]] ReadValue<s16> s16le(std::string_view name);
  [[nodiscard]] ReadValue<s16> s16be(std::string_view name);
  [[nodiscard]] ReadValue<u32> u24le(std::string_view name);
  [[nodiscard]] ReadValue<u32> u24be(std::string_view name);
  [[nodiscard]] ReadValue<u32> varLen(std::string_view name);
  [[nodiscard]] ReadValue<Address> address16be(std::string_view name);
  [[nodiscard]] ReadValue<Address> address16le(std::string_view name);
  [[nodiscard]] ReadValue<Address> le24RelativeAddress(std::string_view name, Address base);
  [[nodiscard]] ReadValue<std::string> rawBytes(std::string_view name, size_t size);

  VmCommandCursor& derived(std::string_view name, SourceValue value,
                           SourceValueDisplay display = SourceValueDisplay::Default);
  template <class T>
  VmCommandCursor& derived(std::string_view name, T&& value, SourceValueDisplay display = SourceValueDisplay::Default) {
    return derived(name, makeSourceValue(std::forward<T>(value)), display);
  }
  VmCommandCursor& detail(std::string_view name, SourceValue value,
                          SourceValueDisplay display = SourceValueDisplay::Default);
  template <class T>
  VmCommandCursor& detail(std::string_view name, T&& value, SourceValueDisplay display = SourceValueDisplay::Default) {
    return detail(name, makeSourceValue(std::forward<T>(value)), display);
  }
  VmCommandCursor& target(Address address, SourceLinkRole role);
  VmCommandCursor& instrumentRef(u32 bank, u32 program);
  VmCommandCursor& sampleRef(u32 sampleIndex);
  VmCommandCursor& warning(std::string_view message);
  VmCommandCursor& error(std::string_view message);
  VmCommandCursor& unsupported(std::string_view message);
  void finalizeDiagnostics(SourceRange commandRange);

  [[nodiscard]] CommandFlow next();
  [[nodiscard]] CommandFlow wait(u32 ticks);
  [[nodiscard]] CommandFlow stop();
  [[nodiscard]] CommandFlow end();
  [[nodiscard]] CommandFlow preserve(std::string_view displayName, size_t operandBytes = 0,
                                     std::string_view kindOverride = {});
  [[nodiscard]] CommandFlow jump(Address destination);
  [[nodiscard]] CommandFlow call(Address destination);
  [[nodiscard]] CommandFlow invalidJump(Address destination, std::string_view message);
  [[nodiscard]] CommandFlow invalidCall(Address destination, std::string_view message);
  [[nodiscard]] CommandFlow ret();
  [[nodiscard]] CommandFlow conditionalBranch(Address destination);
  [[nodiscard]] CommandFlow loopCandidate(Address destination);
  [[nodiscard]] CommandFlow declaredLoop(Address destination);
  [[nodiscard]] CommandFlow countedRepeatUntil(::u8 slot, u32 totalPlays, Address destination);
  [[nodiscard]] RepeatBreakFlow countedRepeatBreak(::u8 slot, Address destination, bool taken = false);

private:
  struct PendingDiagnostic {
    Severity severity = Severity::Info;
    std::string message;
    std::optional<SourceRange> range;
  };

  [[nodiscard]] AnnotationBuilder annotationBuilder();
  void ensureAnnotation();
  void recordOpcode();
  [[nodiscard]] SourceRange rangeAt(size_t begin, size_t size) const;
  [[nodiscard]] bool requireRead(size_t size, std::string_view field);
  void markTruncated(std::string_view field, SourceRange range);
  void queueDiagnostic(Severity severity, std::string_view message, std::optional<SourceRange> range = std::nullopt);
  [[nodiscard]] bool readByte(std::string_view field, ::u8& out);
  void recordField(std::string_view name, SourceRange range, SourceValue value,
                   SourceValueDisplay display = SourceValueDisplay::Default);
  void recordOperand(std::string_view name, SourceRange range, const SourceValue& value, SourceValueDisplay display);
  void defaultSemantic(SequenceSemantic semantic);
  void defaultPlaybackStatus(CommandPlaybackStatus status);
  [[nodiscard]] CommandFlow flow(FlowKind kind, u32 waitTicks = 0, std::optional<Address> destination = std::nullopt);

  CommandPhase phase_ = CommandPhase::Decode;
  SourceRange commandRange_;
  std::span<const ::u8> bytes_;
  SourceMapBuilder* sourceMap_ = nullptr;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
  std::vector<CommandOperand>* operands_ = nullptr;
  CommandReferences* references_ = nullptr;
  std::vector<PendingDiagnostic> pendingDiagnostics_;
  size_t position_ = 1;
  SourceAnnotationId annotation_;
  bool opcodeRecorded_ = false;
  bool kindOverridden_ = false;
  bool failed_ = false;
  SourceRange failureRange_;
  std::string displayName_ = "Unknown Command";
  std::string localKind_ = "unknown-command";
  SequenceSemantic semantic_ = SequenceSemantic::Unknown;
  CommandPlaybackStatus playbackStatus_ = CommandPlaybackStatus::AffectsPlayback;
  bool playbackStatusOverridden_ = false;
};

[[nodiscard]] Effects effectsFromCommandFlow(const CommandFlow& flow);
[[nodiscard]] Effects effectsFromCommandFlow(const CommandFlow& flow, VmApi& vm);

}  // namespace vgmtrans::core
