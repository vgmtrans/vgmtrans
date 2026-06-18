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
  LoopCandidate,
  DeclaredLoop,
  CountedRepeatUntil,
  CountedRepeatBreak,
};

struct CommandFlow {
  FlowKind kind = FlowKind::Next;
  u32 waitTicks = 0;
  std::optional<Address> destination;
  ::u8 repeatSlot = 0;
  u32 repeatTotalPlays = 0;
  bool truncated = false;
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

template <class T>
struct CursorReadValue {
  T value{};
  SourceRange range;
  bool valid = true;

  [[nodiscard]] explicit operator bool() const noexcept { return valid; }
  [[nodiscard]] operator T() const noexcept { return value; }
};

// VmCommandCursor is the readable command-authoring surface. It records source
// facts while reading bytes and returns VM-neutral flow decisions; the VM still
// owns playback policy such as loop counts and call stack behavior.
class VmCommandCursor {
public:
  VmCommandCursor(CommandPhase phase, SourceRange commandRange, std::span<const ::u8> bytes,
                  SourceMapBuilder* sourceMap = nullptr, std::vector<Diagnostic>* diagnostics = nullptr);

  [[nodiscard]] CommandPhase phase() const noexcept { return phase_; }
  [[nodiscard]] SourceId source() const noexcept { return commandRange_.source; }
  [[nodiscard]] Address address() const noexcept { return Address{static_cast<u32>(commandRange_.offset)}; }
  [[nodiscard]] ::u8 opcode() const noexcept { return bytes_.empty() ? 0 : bytes_.front(); }
  [[nodiscard]] SourceRange commandRange() const noexcept { return commandRange_; }
  [[nodiscard]] SourceAnnotationId annotation() const noexcept { return annotation_; }
  [[nodiscard]] bool failed() const noexcept { return failed_; }

  VmCommandCursor& name(std::string_view displayName);
  VmCommandCursor& kind(std::string_view localKindOverride);
  VmCommandCursor& semantic(SequenceSemantic semantic);

  [[nodiscard]] CursorReadValue<::u8> u8(std::string_view name);
  [[nodiscard]] CursorReadValue<s8> s8(std::string_view name);
  [[nodiscard]] CursorReadValue<u16> u16le(std::string_view name);
  [[nodiscard]] CursorReadValue<u16> u16be(std::string_view name);
  [[nodiscard]] CursorReadValue<u32> u24le(std::string_view name);
  [[nodiscard]] CursorReadValue<u32> u24be(std::string_view name);
  [[nodiscard]] CursorReadValue<u32> varLen(std::string_view name);
  [[nodiscard]] CursorReadValue<Address> address16be(std::string_view name);
  [[nodiscard]] CursorReadValue<Address> address16le(std::string_view name);
  [[nodiscard]] CursorReadValue<Address> le24RelativeAddress(std::string_view name, Address base);

  VmCommandCursor& derived(std::string_view name, SourceValue value,
                           SourceValueDisplay display = SourceValueDisplay::Default);
  VmCommandCursor& detail(std::string_view name, SourceValue value,
                          SourceValueDisplay display = SourceValueDisplay::Default);
  VmCommandCursor& target(Address address, SourceLinkRole role);
  VmCommandCursor& warning(std::string_view message);
  VmCommandCursor& error(std::string_view message);
  VmCommandCursor& unsupported(std::string_view message);

  [[nodiscard]] CommandFlow next();
  [[nodiscard]] CommandFlow wait(u32 ticks);
  [[nodiscard]] CommandFlow stop();
  [[nodiscard]] CommandFlow end();
  [[nodiscard]] CommandFlow jump(Address destination);
  [[nodiscard]] CommandFlow call(Address destination);
  [[nodiscard]] CommandFlow ret();
  [[nodiscard]] CommandFlow loopCandidate(Address destination);
  [[nodiscard]] CommandFlow declaredLoop(Address destination);
  [[nodiscard]] CommandFlow countedRepeatUntil(::u8 slot, u32 totalPlays, Address destination);
  [[nodiscard]] RepeatBreakFlow countedRepeatBreak(::u8 slot, Address destination, bool taken = false);

private:
  [[nodiscard]] AnnotationBuilder annotationBuilder();
  void ensureAnnotation();
  void recordOpcode();
  [[nodiscard]] SourceRange rangeAt(size_t begin, size_t size) const;
  [[nodiscard]] bool canRead(size_t size, std::string_view field);
  void markTruncated(std::string_view field, SourceRange range);
  [[nodiscard]] ::u8 readByte(std::string_view field);
  void recordField(std::string_view name, SourceRange range, SourceValue value,
                   SourceValueDisplay display = SourceValueDisplay::Default);
  [[nodiscard]] CommandFlow flow(FlowKind kind, u32 waitTicks = 0,
                                 std::optional<Address> destination = std::nullopt);

  CommandPhase phase_ = CommandPhase::Decode;
  SourceRange commandRange_;
  std::span<const ::u8> bytes_;
  SourceMapBuilder* sourceMap_ = nullptr;
  std::vector<Diagnostic>* diagnostics_ = nullptr;
  size_t position_ = 1;
  SourceAnnotationId annotation_;
  bool opcodeRecorded_ = false;
  bool kindOverridden_ = false;
  bool failed_ = false;
  SourceRange failureRange_;
};

[[nodiscard]] Effects effectsFromCommandFlow(const CommandFlow& flow);

}  // namespace vgmtrans::core
