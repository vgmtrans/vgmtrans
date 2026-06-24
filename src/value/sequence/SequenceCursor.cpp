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

[[nodiscard]] SourceRange addressTargetRange(SourceRange commandRange, Address address) {
  if (!commandRange.source.valid()) {
    return {};
  }
  return SourceRange{.source = commandRange.source, .offset = address.value, .size = 1};
}

[[nodiscard]] std::string hexBytes(std::span<const ::u8> bytes) {
  static constexpr char kDigits[] = "0123456789ABCDEF";

  std::string out;
  out.reserve(bytes.size() * 3);
  for (const ::u8 byte : bytes) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 0x0f]);
  }
  return out;
}

}  // namespace

RepeatBreakFlow::RepeatBreakFlow(CommandFlow flow, bool taken) : flow_(flow), taken_(taken) {
}

RepeatUntilFlow::RepeatUntilFlow(CommandFlow flow, bool fallsThrough)
    : flow_(flow), fallsThrough_(fallsThrough) {
}

VmCommandCursor::VmCommandCursor(CommandPhase phase, SourceRange commandRange, std::span<const ::u8> bytes,
                                 SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics)
    : phase_(phase), commandRange_(commandRange), bytes_(bytes), sourceMap_(sourceMap), diagnostics_(diagnostics) {
  if (bytes_.empty()) {
    markTruncated("opcode", commandRange_);
  }
}

CursorCommandMetadata VmCommandCursor::metadata(std::string_view kindPrefix) const {
  return CursorCommandMetadata{
      .name = displayName_,
      .detailKind = kindPrefix.empty() ? localKind_ : std::string(kindPrefix) + "." + localKind_,
      .semantic = semantic_,
      .playbackStatus = playbackStatus_,
  };
}

u32 VmCommandCursor::absolutePosition() const noexcept {
  return static_cast<u32>(commandRange_.offset + position_);
}

VmCommandCursor& VmCommandCursor::name(std::string_view displayName) {
  displayName_ = std::string(displayName);
  if (!kindOverridden_) {
    localKind_ = sourceLocalKind(displayName);
  }
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

VmCommandCursor& VmCommandCursor::name(std::string_view displayName, SequenceSemantic semantic) {
  return name(displayName).semantic(semantic);
}

VmCommandCursor& VmCommandCursor::name(std::string_view displayName, SequenceSemantic semantic,
                                       CommandPlaybackStatus status) {
  return name(displayName, semantic).playbackStatus(status);
}

VmCommandCursor& VmCommandCursor::kind(std::string_view localKindOverride) {
  localKind_ = std::string(localKindOverride);
  kindOverridden_ = true;
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  annotationBuilder().kind(localKindOverride);
  return *this;
}

VmCommandCursor& VmCommandCursor::semantic(SequenceSemantic semantic) {
  semantic_ = semantic;
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  annotationBuilder().sequenceSemantic(semantic);
  return *this;
}

VmCommandCursor& VmCommandCursor::playbackStatus(CommandPlaybackStatus status) {
  playbackStatus_ = status;
  playbackStatusOverridden_ = true;
  writePlaybackStatus(status);
  return *this;
}

VmCommandCursor& VmCommandCursor::sourceOnly() {
  return playbackStatus(CommandPlaybackStatus::SourceOnly);
}

VmCommandCursor& VmCommandCursor::noOp() {
  return playbackStatus(CommandPlaybackStatus::NoOp);
}

ReadValue<::u8> VmCommandCursor::u8(std::string_view name) {
  const size_t begin = position_;
  ::u8 value = 0;
  const bool valid = readByte(name, value);
  const auto range = rangeAt(begin, 1);
  if (valid) {
    recordField(name, range, makeSourceValue(value));
  }
  return ReadValue<::u8>{.value = value, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<::s8> VmCommandCursor::s8(std::string_view name) {
  const size_t begin = position_;
  ::u8 byte = 0;
  const bool valid = readByte(name, byte);
  const auto signedByte = static_cast<::s8>(byte);
  const auto range = rangeAt(begin, 1);
  if (valid) {
    recordField(name, range, makeSourceValue(signedByte), SourceValueDisplay::SignedDecimal);
  }
  return ReadValue<::s8>{
      .value = signedByte, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<u16> VmCommandCursor::u16le(std::string_view name) {
  const size_t begin = position_;
  ::u8 lowByte = 0;
  ::u8 highByte = 0;
  const bool valid = readByte(name, lowByte) && readByte(name, highByte);
  const u16 low = lowByte;
  const u16 high = highByte;
  const auto value = static_cast<u16>(low | (high << 8));
  const auto range = rangeAt(begin, 2);
  if (valid) {
    recordField(name, range, makeSourceValue(value));
  }
  return ReadValue<u16>{.value = value, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<u16> VmCommandCursor::u16be(std::string_view name) {
  const size_t begin = position_;
  ::u8 highByte = 0;
  ::u8 lowByte = 0;
  const bool valid = readByte(name, highByte) && readByte(name, lowByte);
  const u16 high = highByte;
  const u16 low = lowByte;
  const auto value = static_cast<u16>((high << 8) | low);
  const auto range = rangeAt(begin, 2);
  if (valid) {
    recordField(name, range, makeSourceValue(value));
  }
  return ReadValue<u16>{.value = value, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<s16> VmCommandCursor::s16le(std::string_view name) {
  const size_t begin = position_;
  ::u8 lowByte = 0;
  ::u8 highByte = 0;
  const bool valid = readByte(name, lowByte) && readByte(name, highByte);
  const u16 low = lowByte;
  const u16 high = highByte;
  const auto value = static_cast<s16>(low | (high << 8));
  const auto range = rangeAt(begin, 2);
  if (valid) {
    recordField(name, range, makeSourceValue(value), SourceValueDisplay::SignedDecimal);
  }
  return ReadValue<s16>{.value = value, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<s16> VmCommandCursor::s16be(std::string_view name) {
  const size_t begin = position_;
  ::u8 highByte = 0;
  ::u8 lowByte = 0;
  const bool valid = readByte(name, highByte) && readByte(name, lowByte);
  const u16 high = highByte;
  const u16 low = lowByte;
  const auto value = static_cast<s16>((high << 8) | low);
  const auto range = rangeAt(begin, 2);
  if (valid) {
    recordField(name, range, makeSourceValue(value), SourceValueDisplay::SignedDecimal);
  }
  return ReadValue<s16>{.value = value, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<u32> VmCommandCursor::u24le(std::string_view name) {
  const size_t begin = position_;
  ::u8 lowByte = 0;
  ::u8 middleByte = 0;
  ::u8 highByte = 0;
  const bool valid = readByte(name, lowByte) && readByte(name, middleByte) && readByte(name, highByte);
  const u32 low = lowByte;
  const u32 middle = middleByte;
  const u32 high = highByte;
  const u32 value = low | (middle << 8) | (high << 16);
  const auto range = rangeAt(begin, 3);
  if (valid) {
    recordField(name, range, makeSourceValue(value));
  }
  return ReadValue<u32>{.value = value, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<u32> VmCommandCursor::u24be(std::string_view name) {
  const size_t begin = position_;
  ::u8 highByte = 0;
  ::u8 middleByte = 0;
  ::u8 lowByte = 0;
  const bool valid = readByte(name, highByte) && readByte(name, middleByte) && readByte(name, lowByte);
  const u32 high = highByte;
  const u32 middle = middleByte;
  const u32 low = lowByte;
  const u32 value = (high << 16) | (middle << 8) | low;
  const auto range = rangeAt(begin, 3);
  if (valid) {
    recordField(name, range, makeSourceValue(value));
  }
  return ReadValue<u32>{.value = value, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<u32> VmCommandCursor::varLen(std::string_view name) {
  const size_t begin = position_;
  u32 value = 0;
  while (true) {
    ::u8 byte = 0;
    if (!readByte(name, byte)) {
      return ReadValue<u32>{.value = value, .range = rangeAt(begin, position_ - begin), .valid = false};
    }
    value = (value << 7) + (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      break;
    }
  }
  const auto range = rangeAt(begin, position_ - begin);
  recordField(name, range, makeSourceValue(value));
  return ReadValue<u32>{.value = value, .range = range, .valid = true};
}

ReadValue<Address> VmCommandCursor::address16be(std::string_view name) {
  const size_t begin = position_;
  ::u8 highByte = 0;
  ::u8 lowByte = 0;
  const bool valid = readByte(name, highByte) && readByte(name, lowByte);
  const u16 high = highByte;
  const u16 low = lowByte;
  const Address address{static_cast<u32>((high << 8) | low)};
  const auto range = rangeAt(begin, 2);
  if (valid) {
    recordField(name, range, makeSourceValue(address.value), SourceValueDisplay::Address);
  }
  return ReadValue<Address>{
      .value = address, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<Address> VmCommandCursor::address16le(std::string_view name) {
  const size_t begin = position_;
  ::u8 lowByte = 0;
  ::u8 highByte = 0;
  const bool valid = readByte(name, lowByte) && readByte(name, highByte);
  const u16 low = lowByte;
  const u16 high = highByte;
  const Address address{static_cast<u32>(low | (high << 8))};
  const auto range = rangeAt(begin, 2);
  if (valid) {
    recordField(name, range, makeSourceValue(address.value), SourceValueDisplay::Address);
  }
  return ReadValue<Address>{
      .value = address, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<Address> VmCommandCursor::le24RelativeAddress(std::string_view name, Address base) {
  const size_t begin = position_;
  ::u8 lowByte = 0;
  ::u8 middleByte = 0;
  ::u8 highByte = 0;
  const bool valid = readByte(name, lowByte) && readByte(name, middleByte) && readByte(name, highByte);
  const u32 low = lowByte;
  const u32 middle = middleByte;
  const u32 high = highByte;
  const Address address{base.value + (low | (middle << 8) | (high << 16))};
  const auto range = rangeAt(begin, 3);
  if (valid) {
    recordField(name, range, makeSourceValue(address.value), SourceValueDisplay::Address);
  }
  return ReadValue<Address>{
      .value = address, .range = valid ? range : rangeAt(begin, position_ - begin), .valid = valid};
}

ReadValue<std::string> VmCommandCursor::rawBytes(std::string_view name, size_t size) {
  const size_t begin = position_;
  if (failed_) {
    return ReadValue<std::string>{.range = rangeAt(begin, 0), .valid = false};
  }

  const size_t readBegin = std::min(begin, bytes_.size());
  const size_t available = bytes_.size() - readBegin;
  const size_t consumed = std::min(size, available);
  const auto range = rangeAt(begin, consumed);
  const auto value = hexBytes(bytes_.subspan(readBegin, consumed));
  position_ += consumed;

  if (consumed > 0) {
    recordField(name, range, makeSourceValue(value), SourceValueDisplay::Hex);
  }
  if (consumed != size) {
    markTruncated(name, range);
    return ReadValue<std::string>{.value = value, .range = range, .valid = false};
  }
  return ReadValue<std::string>{.value = value, .range = range, .valid = true};
}

VmCommandCursor& VmCommandCursor::derived(std::string_view name, SourceValue value, SourceValueDisplay display) {
  recordField(name, SourceRange{}, std::move(value), display);
  return *this;
}

VmCommandCursor& VmCommandCursor::target(Address address, SourceLinkRole role) {
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  annotationBuilder().link(role, SourceTarget{addressTargetRange(commandRange_, address)});
  return *this;
}

VmCommandCursor& VmCommandCursor::instrumentRef(u32 bank, u32 program) {
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  annotationBuilder().link(SourceLinkRole::UsesInstrument, SourceTarget{ObjectRefs::instrumentProgram(bank, program)},
                           "Instrument");
  return *this;
}

VmCommandCursor& VmCommandCursor::sampleRef(u32 sampleIndex) {
  if (sourceMap_ == nullptr) {
    return *this;
  }
  ensureAnnotation();
  annotationBuilder().link(SourceLinkRole::UsesSample, SourceTarget{ObjectRefs::sampleIndex(sampleIndex)}, "Sample");
  return *this;
}

VmCommandCursor& VmCommandCursor::warning(std::string_view message) {
  queueDiagnostic(Severity::Warning, message);
  return *this;
}

VmCommandCursor& VmCommandCursor::error(std::string_view message) {
  queueDiagnostic(Severity::Error, message);
  return *this;
}

VmCommandCursor& VmCommandCursor::unsupported(std::string_view message) {
  semantic(SequenceSemantic::Unsupported);
  playbackStatus(CommandPlaybackStatus::Unsupported);
  warning(message);
  return *this;
}

void VmCommandCursor::finalizeDiagnostics(SourceRange commandRange) {
  if (diagnostics_ == nullptr) {
    pendingDiagnostics_.clear();
    return;
  }

  for (auto& diagnostic : pendingDiagnostics_) {
    diagnostics_->push_back(Diagnostic{
        .severity = diagnostic.severity,
        .message = std::move(diagnostic.message),
        .range = diagnostic.range.value_or(commandRange),
        .annotation = annotation_.valid() ? std::optional<SourceAnnotationId>{annotation_} : std::nullopt,
    });
  }
  pendingDiagnostics_.clear();
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
  defaultSemantic(SequenceSemantic::End);
  defaultPlaybackStatus(CommandPlaybackStatus::StopsPlayback);
  return flow(FlowKind::End);
}

CommandFlow VmCommandCursor::preserve(std::string_view displayName, size_t operandBytes,
                                      std::string_view kindOverride) {
  name(displayName, SequenceSemantic::Meta).sourceOnly();
  if (!kindOverride.empty()) {
    kind(kindOverride);
  }
  if (operandBytes > 0) {
    static_cast<void>(rawBytes("bytes", operandBytes));
  }
  return next();
}

CommandFlow VmCommandCursor::jump(Address destination) {
  defaultSemantic(SequenceSemantic::Jump);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  target(destination, SourceLinkRole::JumpTarget);
  return flow(FlowKind::Jump, 0, destination);
}

CommandFlow VmCommandCursor::call(Address destination) {
  defaultSemantic(SequenceSemantic::Call);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  target(destination, SourceLinkRole::CallTarget);
  return flow(FlowKind::Call, 0, destination);
}

CommandFlow VmCommandCursor::invalidJump(Address destination, std::string_view message) {
  defaultSemantic(SequenceSemantic::Jump);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  warning(message);
  return flow(FlowKind::End);
}

CommandFlow VmCommandCursor::invalidCall(Address destination, std::string_view message) {
  defaultSemantic(SequenceSemantic::Call);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  warning(message);
  return flow(FlowKind::End);
}

CommandFlow VmCommandCursor::ret() {
  defaultSemantic(SequenceSemantic::Return);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  return flow(FlowKind::Return);
}

CommandFlow VmCommandCursor::conditionalBranch(Address destination) {
  defaultSemantic(SequenceSemantic::Jump);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  target(destination, SourceLinkRole::JumpTarget);
  return flow(FlowKind::ConditionalBranch, 0, destination);
}

CommandFlow VmCommandCursor::loopCandidate(Address destination) {
  defaultSemantic(SequenceSemantic::Jump);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  target(destination, SourceLinkRole::LoopTarget);
  return flow(FlowKind::LoopCandidate, 0, destination);
}

CommandFlow VmCommandCursor::declaredLoop(Address destination) {
  defaultSemantic(SequenceSemantic::Loop);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  target(destination, SourceLinkRole::LoopTarget);
  return flow(FlowKind::DeclaredLoop, 0, destination);
}

CommandFlow VmCommandCursor::countedRepeatUntil(::u8 slot, u32 totalPlays, Address destination) {
  defaultSemantic(SequenceSemantic::Repeat);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
  target(destination, SourceLinkRole::RepeatTarget);
  auto result = flow(FlowKind::CountedRepeatUntil, 0, destination);
  result.repeatSlot = slot;
  result.repeatTotalPlays = totalPlays;
  return result;
}

RepeatBreakFlow VmCommandCursor::countedRepeatBreak(::u8 slot, Address destination, bool taken) {
  defaultSemantic(SequenceSemantic::RepeatBreak);
  defaultPlaybackStatus(CommandPlaybackStatus::AffectsControlFlow);
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
  annotationBuilder().field("opcode", rangeAt(0, 1), bytes_.front(), SourceValueDisplay::Hex);
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

bool VmCommandCursor::requireRead(size_t size, std::string_view field) {
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
        .annotation = annotation_.valid() ? std::optional<SourceAnnotationId>{annotation_} : std::nullopt,
    });
  }
}

void VmCommandCursor::queueDiagnostic(Severity severity, std::string_view message, std::optional<SourceRange> range) {
  if (diagnostics_ == nullptr) {
    return;
  }
  pendingDiagnostics_.push_back(PendingDiagnostic{
      .severity = severity,
      .message = std::string(message),
      .range = range,
  });
}

bool VmCommandCursor::readByte(std::string_view field, ::u8& out) {
  if (!requireRead(1, field)) {
    return false;
  }
  out = bytes_[position_];
  ++position_;
  return true;
}

void VmCommandCursor::recordField(std::string_view name, SourceRange range, SourceValue value,
                                  SourceValueDisplay display) {
  if (sourceMap_ != nullptr) {
    ensureAnnotation();
    annotationBuilder().field(name, range, std::move(value), display);
  }
}

void VmCommandCursor::defaultSemantic(SequenceSemantic semantic) {
  if (semantic_ == SequenceSemantic::Unknown) {
    this->semantic(semantic);
  }
}

void VmCommandCursor::writePlaybackStatus(CommandPlaybackStatus status) {
  if (sourceMap_ == nullptr) {
    return;
  }
  ensureAnnotation();
  annotationBuilder().playbackStatus(status);
}

void VmCommandCursor::defaultPlaybackStatus(CommandPlaybackStatus status) {
  if (!playbackStatusOverridden_) {
    playbackStatus_ = status;
    writePlaybackStatus(status);
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
  if (flow.resolvedEffects) {
    return *flow.resolvedEffects;
  }

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
    case FlowKind::ConditionalBranch:
      return Effects::none();
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
  if (flow.resolvedEffects) {
    return *flow.resolvedEffects;
  }

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
