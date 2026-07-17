/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/CommandSourceMap.h"

#include "value/model/SourceMap.h"

#include <limits>
#include <optional>
#include <string_view>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::optional<Address> operandAddress(const SemanticOperand& operand) {
  if (const auto* address = std::get_if<Address>(&operand.value)) {
    return *address;
  }
  if (const auto* value = std::get_if<u64>(&operand.value)) {
    return Address{*value};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<u32> operandUnsigned32(const SemanticOperand& operand) {
  const auto* value = std::get_if<u64>(&operand.value);
  if (value == nullptr || *value > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  return static_cast<u32>(*value);
}

[[nodiscard]] std::optional<SourceLinkRole> linkRole(SemanticOperandRole role) {
  switch (role) {
    case SemanticOperandRole::JumpTarget:
      return SourceLinkRole::JumpTarget;
    case SemanticOperandRole::CallTarget:
      return SourceLinkRole::CallTarget;
    case SemanticOperandRole::LoopTarget:
      return SourceLinkRole::LoopTarget;
    case SemanticOperandRole::RepeatTarget:
      return SourceLinkRole::RepeatTarget;
    default:
      return std::nullopt;
  }
}

void projectOperand(AnnotationBuilder& annotation, const SemanticOperand& operand) {
  if (operand.name.empty()) {
    return;
  }

  if (operand.encodedValue) {
    const std::string_view encodedName =
        operand.encodedName.empty() ? std::string_view{operand.name} : std::string_view{operand.encodedName};
    if (operand.range.valid()) {
      annotation.field(encodedName, operand.range, semanticOperandSourceValue(*operand.encodedValue),
                       operand.encodedDisplay);
    }
    annotation.derived(operand.name, semanticOperandSourceValue(operand.value), operand.display);
    return;
  }

  if (operand.range.valid()) {
    annotation.field(operand.name, operand.range, semanticOperandSourceValue(operand.value), operand.display);
  } else {
    annotation.derived(operand.name, semanticOperandSourceValue(operand.value), operand.display);
  }
}

}  // namespace

SourceAnnotationId projectDecodedCommand(SourceMapBuilder* sourceMap, const DecodedBytecodeCommand& command,
                                         std::optional<SourceAnnotationId> parent) {
  if (sourceMap == nullptr || !command.range.valid()) {
    return {};
  }

  auto annotation =
      sourceMap->command(command.presentation.label, command.range, command.presentation.semantic)
          .kind(command.presentation.localKind)
          .detailKind(command.presentation.detailKind)
          .playbackStatus(command.presentation.playback)
          .field("opcode", SourceRange{.source = command.range.source, .offset = command.range.offset, .size = 1},
                 command.opcode, SourceValueDisplay::Hex);
  if (parent) {
    annotation.parent(*parent);
  }

  std::optional<u32> instrumentBank;
  std::optional<u32> instrumentProgram;
  for (const auto& operand : command.operands) {
    projectOperand(annotation, operand);

    if (const auto role = linkRole(operand.role)) {
      if (const auto destination = operandAddress(operand)) {
        annotation.link(
            *role, SourceTarget{SourceRange{.source = command.range.source, .offset = destination->value, .size = 1}});
      }
    }
    if (operand.role == SemanticOperandRole::InstrumentBank) {
      instrumentBank = operandUnsigned32(operand);
    } else if (operand.role == SemanticOperandRole::InstrumentProgram) {
      instrumentProgram = operandUnsigned32(operand);
    } else if (operand.role == SemanticOperandRole::Instrument) {
      if (const auto instrument = operandUnsigned32(operand)) {
        annotation.link(SourceLinkRole::UsesInstrument, SourceTarget{ObjectRefs::instrumentIndex(*instrument)},
                        "Instrument");
      }
    }
  }

  if (instrumentBank && instrumentProgram) {
    annotation.link(SourceLinkRole::UsesInstrument,
                    SourceTarget{ObjectRefs::instrumentProgram(*instrumentBank, *instrumentProgram)}, "Instrument");
  }
  return annotation.id();
}

}  // namespace vgmtrans::core
