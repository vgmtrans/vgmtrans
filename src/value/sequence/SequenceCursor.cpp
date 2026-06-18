/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceCursor.h"

#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] SourceValue unsignedValue(auto value) {
  return SourceValue{static_cast<u64>(value)};
}

[[nodiscard]] SourceValue signedValue(auto value) {
  return SourceValue{static_cast<s64>(value)};
}

[[nodiscard]] SourceRange addressTargetRange(SourceRange commandRange, Address address) {
  if (!commandRange.source.valid()) {
    return {};
  }
  return SourceRange{.source = commandRange.source, .offset = address.value, .size = 1};
}

}  // namespace

RepeatBreakFlow::RepeatBreakFlow(CommandFlow flow, bool taken) : flow_(flow), taken_(taken) {
}

VmCommandCursor::VmCommandCursor(CommandPhase phase, SourceRange commandRange, std::span<const ::u8> bytes,
                                 SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics,
                                 std::vector<CommandOperand>* operands)
    : phase_(phase),
      commandRange_(commandRange),
      bytes_(bytes),
      sourceMap_(sourceMap),
      diagnostics_(diagnostics),
      operands_(operands) {
  if (bytes_.empty()) {
    markTruncated("opcode", commandRange_);
  }
}

VmCommandCursor& VmCommandCursor::name(std::string_view displayName) {
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  annotationBuilder().label(displayName);
  if (!kindOverridden_) {
    annotationBuilder().kind(sourceLocalKind(displayName));
  }
  return *this;
}

VmCommandCursor& VmCommandCursor::kind(std::string_view localKindOverride) {
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  kindOverridden_ = true;
  annotationBuilder().kind(localKindOverride);
  return *this;
}

VmCommandCursor& VmCommandCursor::semantic(SequenceSemantic semantic) {
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  annotationBuilder().sequenceSemantic(semantic);
  return *this;
}

CursorReadValue<::u8> VmCommandCursor::u8(std::string_view name) {
  const size_t begin = position_;
  if (!canRead(1, name)) {
    return CursorReadValue<::u8>{.range = rangeAt(begin, 0), .valid = false};
  }
  const ::u8 value = readByte(name);
  const auto range = rangeAt(begin, 1);
  recordField(name, range, unsignedValue(value));
  return CursorReadValue<::u8>{.value = value, .range = range};
}

CursorReadValue<s8> VmCommandCursor::s8(std::string_view name) {
  const size_t begin = position_;
  if (!canRead(1, name)) {
    return CursorReadValue<::s8>{.range = rangeAt(begin, 0), .valid = false};
  }
  const auto signedByte = static_cast<::s8>(readByte(name));
  const auto range = rangeAt(begin, 1);
  recordField(name, range, signedValue(signedByte), SourceValueDisplay::SignedDecimal);
  return CursorReadValue<::s8>{.value = signedByte, .range = range};
}

CursorReadValue<u16> VmCommandCursor::u16le(std::string_view name) {
  const size_t begin = position_;
  if (!canRead(2, name)) {
    return CursorReadValue<u16>{.range = rangeAt(begin, 0), .valid = false};
  }
  const u16 low = readByte(name);
  const u16 high = readByte(name);
  const auto value = static_cast<u16>(low | (high << 8));
  const auto range = rangeAt(begin, 2);
  recordField(name, range, unsignedValue(value));
  return CursorReadValue<u16>{.value = value, .range = range};
}

CursorReadValue<u16> VmCommandCursor::u16be(std::string_view name) {
  const size_t begin = position_;
  if (!canRead(2, name)) {
    return CursorReadValue<u16>{.range = rangeAt(begin, 0), .valid = false};
  }
  const u16 high = readByte(name);
  const u16 low = readByte(name);
  const auto value = static_cast<u16>((high << 8) | low);
  const auto range = rangeAt(begin, 2);
  recordField(name, range, unsignedValue(value));
  return CursorReadValue<u16>{.value = value, .range = range};
}

CursorReadValue<u32> VmCommandCursor::u24le(std::string_view name) {
  const size_t begin = position_;
  if (!canRead(3, name)) {
    return CursorReadValue<u32>{.range = rangeAt(begin, 0), .valid = false};
  }
  const u32 low = readByte(name);
  const u32 middle = readByte(name);
  const u32 high = readByte(name);
  const u32 value = low | (middle << 8) | (high << 16);
  const auto range = rangeAt(begin, 3);
  recordField(name, range, unsignedValue(value));
  return CursorReadValue<u32>{.value = value, .range = range};
}

CursorReadValue<u32> VmCommandCursor::u24be(std::string_view name) {
  const size_t begin = position_;
  if (!canRead(3, name)) {
    return CursorReadValue<u32>{.range = rangeAt(begin, 0), .valid = false};
  }
  const u32 high = readByte(name);
  const u32 middle = readByte(name);
  const u32 low = readByte(name);
  const u32 value = (high << 16) | (middle << 8) | low;
  const auto range = rangeAt(begin, 3);
  recordField(name, range, unsignedValue(value));
  return CursorReadValue<u32>{.value = value, .range = range};
}

CursorReadValue<u32> VmCommandCursor::varLen(std::string_view name) {
  const size_t begin = position_;
  u32 value = 0;
  while (true) {
    if (!canRead(1, name)) {
      return CursorReadValue<u32>{.range = rangeAt(begin, position_ - begin), .valid = false};
    }
    const ::u8 byte = readByte(name);
    value = (value << 7) + (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      break;
    }
  }
  const auto range = rangeAt(begin, position_ - begin);
  recordField(name, range, unsignedValue(value));
  return CursorReadValue<u32>{.value = value, .range = range};
}

CursorReadValue<Address> VmCommandCursor::address16be(std::string_view name) {
  const size_t begin = position_;
  if (!canRead(2, name)) {
    return CursorReadValue<Address>{.range = rangeAt(begin, 0), .valid = false};
  }
  const u16 high = readByte(name);
  const u16 low = readByte(name);
  const Address address{static_cast<u32>((high << 8) | low)};
  const auto range = rangeAt(begin, 2);
  recordField(name, range, unsignedValue(address.value), SourceValueDisplay::Address);
  return CursorReadValue<Address>{.value = address, .range = range};
}

CursorReadValue<Address> VmCommandCursor::address16le(std::string_view name) {
  const size_t begin = position_;
  if (!canRead(2, name)) {
    return CursorReadValue<Address>{.range = rangeAt(begin, 0), .valid = false};
  }
  const u16 low = readByte(name);
  const u16 high = readByte(name);
  const Address address{static_cast<u32>(low | (high << 8))};
  const auto range = rangeAt(begin, 2);
  recordField(name, range, unsignedValue(address.value), SourceValueDisplay::Address);
  return CursorReadValue<Address>{.value = address, .range = range};
}

CursorReadValue<Address> VmCommandCursor::le24RelativeAddress(std::string_view name, Address base) {
  const size_t begin = position_;
  if (!canRead(3, name)) {
    return CursorReadValue<Address>{.range = rangeAt(begin, 0), .valid = false};
  }
  const u32 low = readByte(name);
  const u32 middle = readByte(name);
  const u32 high = readByte(name);
  const Address address{base.value + (low | (middle << 8) | (high << 16))};
  const auto range = rangeAt(begin, 3);
  recordField(name, range, unsignedValue(address.value), SourceValueDisplay::Address);
  return CursorReadValue<Address>{.value = address, .range = range};
}

VmCommandCursor& VmCommandCursor::derived(std::string_view name, SourceValue value, SourceValueDisplay display) {
  recordField(name, SourceRange{}, std::move(value), display);
  return *this;
}

VmCommandCursor& VmCommandCursor::detail(std::string_view name, SourceValue value, SourceValueDisplay display) {
  return derived(name, std::move(value), display);
}

VmCommandCursor& VmCommandCursor::target(Address address, SourceLinkRole role) {
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  annotationBuilder().link(role, SourceTarget{addressTargetRange(commandRange_, address)});
  return *this;
}

VmCommandCursor& VmCommandCursor::warning(std::string_view message) {
  if (diagnostics_ != nullptr) {
    diagnostics_->push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = std::string(message),
        .range = commandRange_,
    });
  }
  return *this;
}

VmCommandCursor& VmCommandCursor::error(std::string_view message) {
  if (diagnostics_ != nullptr) {
    diagnostics_->push_back(Diagnostic{
        .severity = Severity::Error,
        .message = std::string(message),
        .range = commandRange_,
    });
  }
  return *this;
}

VmCommandCursor& VmCommandCursor::unsupported(std::string_view message) {
  semantic(SequenceSemantic::Unsupported);
  warning(message);
  return *this;
}

CommandFlow VmCommandCursor::next() {
  return flow(FlowKind::Next);
}

CommandFlow VmCommandCursor::wait(u32 ticks) {
  return flow(FlowKind::Wait, ticks);
}

CommandFlow VmCommandCursor::stop() {
  return flow(FlowKind::Stop);
}

CommandFlow VmCommandCursor::end() {
  return flow(FlowKind::End);
}

CommandFlow VmCommandCursor::jump(Address destination) {
  target(destination, SourceLinkRole::JumpTarget);
  return flow(FlowKind::Jump, 0, destination);
}

CommandFlow VmCommandCursor::call(Address destination) {
  target(destination, SourceLinkRole::CallTarget);
  return flow(FlowKind::Call, 0, destination);
}

CommandFlow VmCommandCursor::ret() {
  return flow(FlowKind::Return);
}

CommandFlow VmCommandCursor::loopCandidate(Address destination) {
  target(destination, SourceLinkRole::LoopTarget);
  return flow(FlowKind::LoopCandidate, 0, destination);
}

CommandFlow VmCommandCursor::declaredLoop(Address destination) {
  target(destination, SourceLinkRole::LoopTarget);
  return flow(FlowKind::DeclaredLoop, 0, destination);
}

CommandFlow VmCommandCursor::countedRepeatUntil(::u8 slot, u32 totalPlays, Address destination) {
  target(destination, SourceLinkRole::RepeatTarget);
  auto result = flow(FlowKind::CountedRepeatUntil, 0, destination);
  result.repeatSlot = slot;
  result.repeatTotalPlays = totalPlays;
  return result;
}

RepeatBreakFlow VmCommandCursor::countedRepeatBreak(::u8 slot, Address destination, bool taken) {
  target(destination, SourceLinkRole::RepeatTarget);
  auto result = flow(FlowKind::CountedRepeatBreak, 0, destination);
  result.repeatSlot = slot;
  return RepeatBreakFlow{result, failed_ ? false : taken};
}

AnnotationBuilder VmCommandCursor::annotationBuilder() {
  ensureAnnotation();
  if (sourceMap_ == nullptr) {
    return {};
  }
  return AnnotationBuilder{*sourceMap_, annotation_};
}

void VmCommandCursor::ensureAnnotation() {
  if (sourceMap_ == nullptr || annotation_.valid()) {
    return;
  }
  auto annotation = sourceMap_->command("Unknown Command", commandRange_);
  annotation_ = annotation.id();
  recordOpcode();
}

void VmCommandCursor::recordOpcode() {
  if (opcodeRecorded_ || bytes_.empty() || sourceMap_ == nullptr || !annotation_.valid()) {
    return;
  }
  opcodeRecorded_ = true;
  annotationBuilder().field("opcode", rangeAt(0, 1), unsignedValue(bytes_.front()), SourceValueDisplay::Hex);
}

SourceRange VmCommandCursor::rangeAt(size_t begin, size_t size) const {
  if (!commandRange_.valid()) {
    return {};
  }
  if (begin > std::numeric_limits<u64>::max() - commandRange_.offset) {
    return commandRange_;
  }
  return SourceRange{
      .source = commandRange_.source,
      .offset = commandRange_.offset + begin,
      .size = static_cast<u64>(size),
  };
}

bool VmCommandCursor::canRead(size_t size, std::string_view field) {
  if (failed_) {
    return false;
  }
  if (position_ > bytes_.size() || size > bytes_.size() - position_) {
    markTruncated(field, rangeAt(position_, 0));
    return false;
  }
  return true;
}

void VmCommandCursor::markTruncated(std::string_view field, SourceRange range) {
  if (failed_) {
    return;
  }
  failed_ = true;
  failureRange_ = range;
  if (sourceMap_ != nullptr) {
    ensureAnnotation();
    annotationBuilder().derived("truncated", true, SourceValueDisplay::Boolean);
  }
  if (diagnostics_ != nullptr) {
    diagnostics_->push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = "Truncated sequence command while reading " + std::string(field),
        .range = range,
    });
  }
}

::u8 VmCommandCursor::readByte(std::string_view field) {
  if (!canRead(1, field)) {
    return 0;
  }
  const ::u8 value = bytes_[position_];
  ++position_;
  return value;
}

void VmCommandCursor::recordField(std::string_view name, SourceRange range, SourceValue value,
                                  SourceValueDisplay display) {
  recordOperand(name, range, value, display);
  if (sourceMap_ != nullptr) {
    ensureAnnotation();
    annotationBuilder().field(name, range, std::move(value), display);
  }
}

void VmCommandCursor::recordOperand(std::string_view name, SourceRange range, const SourceValue& value,
                                    SourceValueDisplay display) {
  if (operands_ == nullptr) {
    return;
  }
  if (const auto* unsignedValue = std::get_if<u64>(&value)) {
    if (display == SourceValueDisplay::Address) {
      operands_->push_back(CommandOperand{.name = std::string(name), .value = Address{static_cast<u32>(*unsignedValue)},
                                          .range = range});
    } else {
      operands_->push_back(CommandOperand{.name = std::string(name), .value = *unsignedValue, .range = range});
    }
  } else if (const auto* signedValue = std::get_if<s64>(&value)) {
    operands_->push_back(CommandOperand{.name = std::string(name), .value = *signedValue, .range = range});
  } else if (const auto* text = std::get_if<std::string>(&value)) {
    operands_->push_back(CommandOperand{.name = std::string(name), .value = *text, .range = range});
  } else if (const auto* boolValue = std::get_if<bool>(&value)) {
    operands_->push_back(CommandOperand{.name = std::string(name), .value = static_cast<u64>(*boolValue), .range = range});
  }
}

CommandFlow VmCommandCursor::flow(FlowKind kind, u32 waitTicks, std::optional<Address> destination) {
  if (failed_) {
    return CommandFlow{
        .kind = FlowKind::Stop,
        .truncated = true,
    };
  }
  return CommandFlow{
      .kind = kind,
      .waitTicks = waitTicks,
      .destination = destination,
  };
}

Effects effectsFromCommandFlow(const CommandFlow& flow) {
  if (flow.truncated) {
    return Effects{.step = Step::end()};
  }

  switch (flow.kind) {
    case FlowKind::Next:
      return Effects::none();
    case FlowKind::Wait:
      return Effects::wait(flow.waitTicks);
    case FlowKind::Stop:
    case FlowKind::End:
      return Effects{.step = Step::end()};
    case FlowKind::Jump:
      if (!flow.destination) {
        return Effects{.step = Step::end()};
      }
      return Effects{.step = Step::jump(*flow.destination)};
    case FlowKind::Call:
      if (!flow.destination) {
        return Effects{.step = Step::end()};
      }
      return Effects{.step = Step::call(*flow.destination)};
    case FlowKind::Return:
      return Effects{.step = Step::return_()};
    case FlowKind::LoopCandidate:
      if (!flow.destination) {
        return Effects{.step = Step::end()};
      }
      return Effects{.step = Step::jump(*flow.destination, JumpSemantics::LoopCandidate)};
    case FlowKind::DeclaredLoop:
      if (!flow.destination) {
        return Effects{.step = Step::end()};
      }
      return Effects{.step = Step::jump(*flow.destination, JumpSemantics::DeclaredLoop)};
    case FlowKind::CountedRepeatUntil:
    case FlowKind::CountedRepeatBreak:
      if (!flow.destination) {
        return Effects{.step = Step::end()};
      }
      return Effects{.step = Step::jump(*flow.destination, JumpSemantics::FiniteRepeat)};
  }
  return Effects::none();
}

Effects effectsFromCommandFlow(const CommandFlow& flow, VmApi& vm) {
  switch (flow.kind) {
    case FlowKind::CountedRepeatUntil:
      if (!flow.destination) {
        return Effects{.step = Step::end()};
      }
      return vm.countedRepeatUntil(flow.repeatSlot, flow.repeatTotalPlays, *flow.destination);
    case FlowKind::CountedRepeatBreak:
      if (!flow.destination) {
        return Effects{.step = Step::end()};
      }
      return vm.countedRepeatBreak(flow.repeatSlot, *flow.destination).effects;
    default:
      return effectsFromCommandFlow(flow);
  }
}

}  // namespace vgmtrans::core
