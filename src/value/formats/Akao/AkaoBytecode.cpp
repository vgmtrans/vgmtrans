/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoBytecode.h"

#include "value/formats/Akao/AkaoVersion.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

[[nodiscard]] bool hasBytes(ByteReader reader, u32 offset, u32 size, u32 sequenceEnd) {
  return offset <= sequenceEnd && size <= sequenceEnd - offset && reader.has(offset, size);
}

[[nodiscard]] s16 leS16(ByteReader reader, u32 offset) {
  return static_cast<s16>(reader.le16(offset));
}

[[nodiscard]] double tempoBpm(AkaoPs1Version version, u16 tempo) {
  if (tempo == 0) {
    return 1.0;
  }
  const u16 freq = version == AkaoPs1Version::Version1_0 ? 0x43d1 : 0x44e8;
  return 60.0 / (kAkaoPpqn * (65536.0 / tempo) * (freq / (33868800.0 / 8)));
}

}  // namespace

bool isAkaoSubEventPrefix(AkaoPs1Version version, u8 status) {
  return (isVersion3OrLater(version) && status == 0xfe) ||
         ((version == AkaoPs1Version::Version1_2 || version == AkaoPs1Version::Version2) && status == 0xfc);
}

bool isAkaoNoteOpcode(AkaoPs1Version version, u8 status) {
  return status <= 0x99 || (isVersion3OrLater(version) && status >= 0xf0 && status <= 0xfd);
}

u32 akaoRelativeDestination(u32 operandOffset, s16 relative, AkaoPs1Version version) {
  return static_cast<u32>(static_cast<s64>(operandOffset) + relative + (isVersion3OrLater(version) ? 0 : 2));
}

u32 akaoDirectOperandBytes(AkaoPs1Version version, u8 status) {
  switch (status) {
    case 0xa1:
    case 0xa2:
    case 0xa3:
    case 0xa5:
    case 0xa8:
    case 0xaa:
    case 0xac:
    case 0xad:
    case 0xae:
    case 0xaf:
    case 0xb1:
    case 0xb2:
    case 0xb5:
    case 0xb7:
    case 0xb9:
    case 0xbb:
    case 0xbd:
    case 0xbf:
    case 0xc0:
    case 0xc1:
    case 0xc9:
    case 0xce:
    case 0xcf:
    case 0xd2:
    case 0xd3:
    case 0xd8:
    case 0xd9:
    case 0xda:
    case 0xdc:
      return 1;
    case 0xa4:
    case 0xa9:
    case 0xab:
    case 0xb0:
    case 0xbc:
    case 0xdd:
    case 0xde:
    case 0xdf:
      return 2;
    case 0xb4:
    case 0xb8:
      return 3;
    case 0xe1:
      return version == AkaoPs1Version::Version3_2 ? 1 : 0;
    case 0xe4:
    case 0xe5:
    case 0xe6:
      return version == AkaoPs1Version::Version3_2 ? 2 : 0;
    case 0xe8:
    case 0xea:
    case 0xee:
      return version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1 ? 2 : 0;
    case 0xe9:
    case 0xeb:
    case 0xef:
    case 0xf0:
    case 0xf1:
      return version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1 ? 3 : 0;
    case 0xec:
      return version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1 ? 2 : 0;
    case 0xf2:
      return version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1 ? 1 : 0;
    case 0xf4:
    case 0xf6:
    case 0xf7:
      return version == AkaoPs1Version::Version1_0 ? 2 : 0;
    case 0xf8:
      return version == AkaoPs1Version::Version1_0 ? 1 : 0;
    case 0xfc:
      return version == AkaoPs1Version::Version1_1 ? 2 : 0;
    case 0xfd:
    case 0xfe:
      return version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1 ? 2 : 0;
    default:
      return 0;
  }
}

u32 akaoSubOperandBytes(AkaoPs1Version version, u8 sub) {
  switch (sub) {
    case 0x00:
    case 0x02:
    case 0x14:
    case 0x15:
    case 0x16:
      return version == AkaoPs1Version::Version3_0 || version == AkaoPs1Version::Version3_1 ||
                     version == AkaoPs1Version::Version3_2
                 ? (sub == 0x14 ? 1 : 2)
                 : 2;
    case 0x01:
    case 0x03:
    case 0x07:
    case 0x08:
    case 0x09:
      return 3;
    case 0x04:
      return isVersion3OrLater(version) ? 0 : 2;
    case 0x0a:
    case 0x0e:
    case 0x10:
    case 0x12:
    case 0x19:
    case 0x1c:
      if (sub == 0x0e && version == AkaoPs1Version::Version3_2) {
        return 2;
      }
      if (sub == 0x12 || sub == 0x19) {
        return 2;
      }
      return 1;
    case 0x06:
    case 0x0c:
    case 0x0f:
    case 0x17:
    case 0x18:
      return sub == 0x0f && version == AkaoPs1Version::Version3_2 ? 0 : 2;
    default:
      return 0;
  }
}

AkaoCommandEffect inspectAkaoCommand(ByteReader reader, u32 offset, AkaoPs1Version version, u32 sequenceEnd) {
  AkaoCommandEffect effect{};
  if (!hasBytes(reader, offset, 1, sequenceEnd)) {
    effect.terminal = true;
    return effect;
  }

  const u8 status = reader.u8At(offset);
  if (isAkaoNoteOpcode(version, status)) {
    effect.size = isVersion3OrLater(version) && status >= 0xf0 ? 2 : 1;
    return effect;
  }
  if (status >= 0x9a && status <= 0x9f) {
    effect.terminal = true;
    return effect;
  }
  if (status == 0xa0) {
    effect.terminal = true;
    return effect;
  }

  if (isAkaoSubEventPrefix(version, status)) {
    if (!hasBytes(reader, offset, 2, sequenceEnd)) {
      effect.terminal = true;
      return effect;
    }
    const u8 sub = reader.u8At(offset + 1);
    const u32 operands = akaoSubOperandBytes(version, sub);
    effect.size = 2 + operands;
    const u32 operandOffset = offset + 2;
    if (sub == 0x04 && !isVersion3OrLater(version) && hasBytes(reader, operandOffset, 2, sequenceEnd)) {
      effect.drumInstrumentOffset = akaoRelativeDestination(operandOffset, leS16(reader, operandOffset), version);
    } else if (sub == 0x06 && hasBytes(reader, operandOffset, 2, sequenceEnd)) {
      effect.jump = akaoRelativeDestination(operandOffset, leS16(reader, operandOffset), version);
    } else if (sub == 0x07 && hasBytes(reader, operandOffset + 1, 2, sequenceEnd)) {
      effect.branch = akaoRelativeDestination(operandOffset + 1, leS16(reader, operandOffset + 1), version);
    } else if ((sub == 0x08 || sub == 0x09) && hasBytes(reader, operandOffset + 1, 2, sequenceEnd)) {
      effect.branch = akaoRelativeDestination(operandOffset + 1, leS16(reader, operandOffset + 1), version);
    } else if (sub == 0x0a) {
      effect.usesIndividualArts = true;
      if (hasBytes(reader, operandOffset, 1, sequenceEnd)) {
        effect.individualArtId = reader.u8At(operandOffset);
      }
    } else if (sub == 0x0e && version == AkaoPs1Version::Version3_2 && hasBytes(reader, operandOffset, 2, sequenceEnd)) {
      effect.branch = akaoRelativeDestination(operandOffset, leS16(reader, operandOffset), version);
    } else if (sub == 0x14) {
      if (isVersion3OrLater(version)) {
        effect.usesIndividualArts = false;
      } else if (hasBytes(reader, operandOffset, 2, sequenceEnd)) {
        effect.customInstrumentOffset = akaoRelativeDestination(operandOffset, leS16(reader, operandOffset), version);
      }
    }
    return effect;
  }

  const u32 operands = akaoDirectOperandBytes(version, status);
  effect.size = 1 + operands;
  const u32 operandOffset = offset + 1;
  switch (status) {
    case 0xa1:
    case 0xf2:
      effect.usesIndividualArts = true;
      if (hasBytes(reader, operandOffset, 1, sequenceEnd)) {
        effect.individualArtId = reader.u8At(operandOffset);
      }
      break;
    case 0xec:
      if ((version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) &&
          hasBytes(reader, operandOffset, 2, sequenceEnd)) {
        effect.drumInstrumentOffset = akaoRelativeDestination(operandOffset, leS16(reader, operandOffset), version);
      }
      break;
    case 0xee:
      if ((version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) &&
          hasBytes(reader, operandOffset, 2, sequenceEnd)) {
        effect.jump = akaoRelativeDestination(operandOffset, leS16(reader, operandOffset), version);
      }
      break;
    case 0xef:
    case 0xf0:
    case 0xf1:
      if ((version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) &&
          hasBytes(reader, operandOffset + 1, 2, sequenceEnd)) {
        effect.branch = akaoRelativeDestination(operandOffset + 1, leS16(reader, operandOffset + 1), version);
      }
      break;
    case 0xf4:
      if (version == AkaoPs1Version::Version1_0) {
        effect.usesIndividualArts = true;
        if (hasBytes(reader, operandOffset, 1, sequenceEnd)) {
          effect.individualArtId = reader.u8At(operandOffset);
        }
      }
      break;
    case 0xfc:
      if (version == AkaoPs1Version::Version1_1 && hasBytes(reader, operandOffset, 2, sequenceEnd)) {
        effect.customInstrumentOffset = akaoRelativeDestination(operandOffset, leS16(reader, operandOffset), version);
      }
      break;
    default:
      break;
  }
  return effect;
}

void analyzeAkaoTrack(ByteReader reader, AkaoSequenceAnalysis& analysis, u32 start) {
  const u32 sequenceEnd = analysis.header.offset + analysis.header.length;
  std::vector<u32> pending{start};
  std::set<u32> visited;
  size_t commands = 0;

  while (!pending.empty() && commands < kAkaoMaxAnalysisCommands) {
    u32 offset = pending.back();
    pending.pop_back();
    while (offset < sequenceEnd && visited.insert(offset).second && commands++ < kAkaoMaxAnalysisCommands) {
      const auto command = inspectAkaoCommand(reader, offset, analysis.header.version, sequenceEnd);
      analysis.usesIndividualArts = analysis.usesIndividualArts || command.usesIndividualArts;
      if (command.individualArtId && *command.individualArtId != 0) {
        analysis.individualArtIds.insert(*command.individualArtId);
      }
      if (command.customInstrumentOffset) {
        analysis.customInstrumentOffsets.insert(*command.customInstrumentOffset);
      }
      if (command.drumInstrumentOffset) {
        analysis.drumInstrumentOffsets.insert(*command.drumInstrumentOffset);
      }
      if (command.branch && *command.branch < sequenceEnd) {
        pending.push_back(*command.branch);
      }
      if (command.terminal || command.size == 0) {
        break;
      }
      if (command.jump && *command.jump < sequenceEnd) {
        offset = *command.jump;
      } else {
        offset += command.size;
      }
    }
  }
}

u32 akaoTempoMicrosPerQuarter(AkaoPs1Version version, u16 tempo) {
  const double bpm = tempoBpm(version, tempo);
  return static_cast<u32>(std::clamp(std::llround(60000000.0 / std::max(1.0, bpm)), 1ll,
                                     static_cast<long long>(std::numeric_limits<u32>::max())));
}

}  // namespace vgmtrans::formats::akao
