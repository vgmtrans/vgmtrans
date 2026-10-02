/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"

namespace vgmtrans::formats::wolf_team_snes::detail {

struct CommandShape {
  u8 size = 0;
  bool terminatesStream = false;
  bool strongValidationSignal = false;
};

[[nodiscard]] inline CommandShape commandShape(Variant variant, const LateTraits& late, u8 opcode) {
  // Sizes are the values returned by the command handlers in the Wolf Team
  // disassemblies. Keeping them here makes discovery and decoding share one grammar.
  if (opcode < 0x80) {
    return {4, false, true};
  }

  if (!isSegmentedVariant(variant)) {
    switch (opcode) {
      case 0x90:
      case 0x93:
      case 0x9b:
      case 0xa2:
      case 0xa3:
      case 0xad:
      case 0xae:
      case 0xb0:
      case 0xb2:
        return {2, false, true};
      case 0x91:
      case 0xfd:
        return {1, true, true};
      case 0x92:
        return {1, false, true};
      case 0x94:
      case 0x95:
      case 0x97:
      case 0x98:
      case 0x99:
      case 0x9a:
      case 0xaa:
      case 0xaf:
        return {3, false, true};
      case 0x96:
        return {static_cast<u8>(late.programChangeHasDelay ? 3 : 2), false, true};
      case 0x9c:
        return {4, false, true};
      default:
        return {};
    }
  }

  const bool middle = isMiddleSegmentedVariant(variant);
  if (middle) {
    if (opcode < 0xe0) {
      return {};
    }
    switch (opcode) {
      case 0xe0:
      case 0xe3:
      case 0xe4:
      case 0xec:
      case 0xf0:
      case 0xf2:
      case 0xf4:
        return {2, false, true};
      case 0xe1:
      case 0xe2:
      case 0xe6:
      case 0xe7:
      case 0xee:
      case 0xef:
        return {3, false, true};
      case 0xe5:
      case 0xe8:
      case 0xe9:
        return {4, false, true};
      case 0xf1:
      case 0xf8:
      case 0xfd:
        return {1, true, true};
      case 0xf9:
        return {1, false, true};
      default:
        return {};
    }
  }

  switch (opcode) {
    case 0xe0:
    case 0xe3:
    case 0xe4:
    case 0xec:
    case 0xef:
    case 0xf0:
    case 0xf4:
    case 0xf7:
      return {2, false, true};
    case 0xe1:
    case 0xe2:
    case 0xe7:
    case 0xee:
      return {3, false, true};
    case 0xe5:
      return {4, false, true};
    case 0xf8:
    case 0xfd:
      return {1, true, true};
    case 0xf9:
      return {1, false, true};
    default:
      return {1, false, false};
  }
}

}  // namespace vgmtrans::formats::wolf_team_snes::detail
