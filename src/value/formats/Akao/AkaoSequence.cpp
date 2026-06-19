/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoSequence.h"

#include "value/base/LevelScale.h"
#include "value/sequence/SequenceCursorDialect.h"
#include "value/sequence/bytecode/BytecodeWalkers.h"

#include <fmt/format.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

constexpr u32 kAkaoSignature = 0x414B414F;
constexpr u32 kAkaoPpqn = 0x30;
constexpr u8 kNoteVelocity = 127;
constexpr size_t kMaxAnalysisCommands = 65536;
constexpr size_t kMaxTrackCommands = 262144;

constexpr u16 kDeltaTimeTable[] = {192, 96, 48, 24, 12, 6, 3, 32, 16, 8, 4};

struct AkaoContext {
  AkaoPs1Version version = AkaoPs1Version::Unknown;
};

struct AkaoTrackState {
  u8 octave = 4;
  bool slur = false;
  bool legato = false;
  bool drum = false;
  bool useOneTimeDuration = false;
  u8 oneTimeDuration = 0;
  u16 lastDeltaTime = 0;
  u16 fixedDuration = 0;
};

struct CommandEffect {
  u32 size = 1;
  bool terminal = false;
  std::optional<u32> jump;
  std::optional<u32> branch;
  bool usesIndividualArts = false;
  std::optional<u32> individualArtId;
  std::optional<u32> customInstrumentOffset;
  std::optional<u32> drumInstrumentOffset;
};

[[nodiscard]] std::string lowerCopy(std::string text) {
  std::ranges::transform(text, text.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

[[nodiscard]] bool containsAny(std::string_view text, std::initializer_list<std::string_view> needles) {
  return std::ranges::any_of(needles, [text](std::string_view needle) { return text.find(needle) != text.npos; });
}

[[nodiscard]] bool hasBytes(ByteReader reader, u32 offset, u32 size, u32 sequenceEnd) {
  return offset <= sequenceEnd && size <= sequenceEnd - offset && reader.has(offset, size);
}

[[nodiscard]] s16 leS16(ByteReader reader, u32 offset) {
  return static_cast<s16>(reader.le16(offset));
}

[[nodiscard]] bool isSubEventPrefix(AkaoPs1Version version, u8 status) {
  return (isVersion3OrLater(version) && status == 0xfe) ||
         ((version == AkaoPs1Version::Version1_2 || version == AkaoPs1Version::Version2) && status == 0xfc);
}

[[nodiscard]] bool isNoteOpcode(AkaoPs1Version version, u8 status) {
  return status <= 0x99 || (isVersion3OrLater(version) && status >= 0xf0 && status <= 0xfd);
}

[[nodiscard]] u32 relativeDestination(u32 operandOffset, s16 relative, AkaoPs1Version version) {
  return static_cast<u32>(static_cast<s64>(operandOffset) + relative + (isVersion3OrLater(version) ? 0 : 2));
}

[[nodiscard]] u32 directOperandBytes(AkaoPs1Version version, u8 status) {
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

[[nodiscard]] u32 subOperandBytes(AkaoPs1Version version, u8 sub) {
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

[[nodiscard]] CommandEffect inspectAkaoCommand(ByteReader reader, u32 offset, AkaoPs1Version version, u32 sequenceEnd) {
  CommandEffect effect{};
  if (!hasBytes(reader, offset, 1, sequenceEnd)) {
    effect.terminal = true;
    return effect;
  }

  const u8 status = reader.u8At(offset);
  if (isNoteOpcode(version, status)) {
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

  if (isSubEventPrefix(version, status)) {
    if (!hasBytes(reader, offset, 2, sequenceEnd)) {
      effect.terminal = true;
      return effect;
    }
    const u8 sub = reader.u8At(offset + 1);
    const u32 operands = subOperandBytes(version, sub);
    effect.size = 2 + operands;
    const u32 operandOffset = offset + 2;
    if (sub == 0x04 && !isVersion3OrLater(version) && hasBytes(reader, operandOffset, 2, sequenceEnd)) {
      effect.drumInstrumentOffset = relativeDestination(operandOffset, leS16(reader, operandOffset), version);
    } else if (sub == 0x06 && hasBytes(reader, operandOffset, 2, sequenceEnd)) {
      effect.jump = relativeDestination(operandOffset, leS16(reader, operandOffset), version);
    } else if (sub == 0x07 && hasBytes(reader, operandOffset + 1, 2, sequenceEnd)) {
      effect.branch = relativeDestination(operandOffset + 1, leS16(reader, operandOffset + 1), version);
    } else if ((sub == 0x08 || sub == 0x09) && hasBytes(reader, operandOffset + 1, 2, sequenceEnd)) {
      effect.branch = relativeDestination(operandOffset + 1, leS16(reader, operandOffset + 1), version);
    } else if (sub == 0x0a) {
      effect.usesIndividualArts = true;
      if (hasBytes(reader, operandOffset, 1, sequenceEnd)) {
        effect.individualArtId = reader.u8At(operandOffset);
      }
    } else if (sub == 0x0e && version == AkaoPs1Version::Version3_2 && hasBytes(reader, operandOffset, 2, sequenceEnd)) {
      effect.branch = relativeDestination(operandOffset, leS16(reader, operandOffset), version);
    } else if (sub == 0x14) {
      if (isVersion3OrLater(version)) {
        effect.usesIndividualArts = false;
      } else if (hasBytes(reader, operandOffset, 2, sequenceEnd)) {
        effect.customInstrumentOffset = relativeDestination(operandOffset, leS16(reader, operandOffset), version);
      }
    }
    return effect;
  }

  const u32 operands = directOperandBytes(version, status);
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
        effect.drumInstrumentOffset = relativeDestination(operandOffset, leS16(reader, operandOffset), version);
      }
      break;
    case 0xee:
      if ((version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) &&
          hasBytes(reader, operandOffset, 2, sequenceEnd)) {
        effect.jump = relativeDestination(operandOffset, leS16(reader, operandOffset), version);
      }
      break;
    case 0xef:
    case 0xf0:
    case 0xf1:
      if ((version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) &&
          hasBytes(reader, operandOffset + 1, 2, sequenceEnd)) {
        effect.branch = relativeDestination(operandOffset + 1, leS16(reader, operandOffset + 1), version);
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
        effect.customInstrumentOffset = relativeDestination(operandOffset, leS16(reader, operandOffset), version);
      }
      break;
    default:
      break;
  }
  return effect;
}

void analyzeTrack(ByteReader reader, AkaoSequenceAnalysis& analysis, u32 start) {
  const u32 sequenceEnd = analysis.header.offset + analysis.header.length;
  std::vector<u32> pending{start};
  std::set<u32> visited;
  size_t commands = 0;

  while (!pending.empty() && commands < kMaxAnalysisCommands) {
    u32 offset = pending.back();
    pending.pop_back();
    while (offset < sequenceEnd && visited.insert(offset).second && commands++ < kMaxAnalysisCommands) {
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

[[nodiscard]] double tempoBpm(AkaoPs1Version version, u16 tempo) {
  if (tempo == 0) {
    return 1.0;
  }
  const u16 freq = version == AkaoPs1Version::Version1_0 ? 0x43d1 : 0x44e8;
  return 60.0 / (kAkaoPpqn * (65536.0 / tempo) * (freq / (33868800.0 / 8)));
}

[[nodiscard]] u32 tempoMicrosPerQuarter(AkaoPs1Version version, u16 tempo) {
  const double bpm = tempoBpm(version, tempo);
  return static_cast<u32>(std::clamp(std::llround(60000000.0 / std::max(1.0, bpm)), 1ll,
                                     static_cast<long long>(std::numeric_limits<u32>::max())));
}

template <class Runtime>
[[nodiscard]] CommandFlow preserve(Runtime&, VmCommandCursor& cmd, std::string_view name, u32 operands,
                                   std::string_view kind = {}) {
  return cmd.preserve(name, operands, kind);
}

template <class Runtime>
[[nodiscard]] CommandFlow readSubEvent(Runtime& rt, VmCommandCursor& cmd, u8 sub) {
  const AkaoPs1Version version = rt.context.version;
  switch (sub) {
    case 0x00: {
      cmd.name("Tempo", SequenceSemantic::Tempo);
      const u16 raw = cmd.u16le("tempo");
      rt.tempo(tempoMicrosPerQuarter(version, raw));
      return cmd.next();
    }
    case 0x04:
      if (isVersion3OrLater(version)) {
        cmd.name("Drum Kit On", SequenceSemantic::Program);
        rt.instrument(127, 127, true);
        rt.state.drum = true;
        return cmd.next();
      }
      return preserve(rt, cmd, "Drum Kit On", 2, "drum-kit-on");
    case 0x05:
      cmd.name("Drum Kit Off", SequenceSemantic::Program);
      rt.state.drum = false;
      return cmd.next();
    case 0x06: {
      cmd.name("Jump");
      const u32 operandOffset = static_cast<u32>(cmd.commandRange().offset + cmd.position());
      const s16 relative = static_cast<s16>(static_cast<u16>(cmd.u16le("relative")));
      const Address destination{relativeDestination(operandOffset, relative, version)};
      cmd.target(destination, SourceLinkRole::JumpTarget);
      return destination.value <= cmd.address().value ? cmd.loopCandidate(destination) : cmd.jump(destination);
    }
    case 0x0a: {
      cmd.name("Program Change w/o Attack", SequenceSemantic::Program);
      const u8 art = cmd.u8("articulation");
      cmd.derived("bank", 0).derived("program", art).instrumentRef(0, art);
      rt.instrument(0, art, true);
      return cmd.next();
    }
    case 0x0e:
      if (version == AkaoPs1Version::Version3_2) {
        cmd.name("Play Pattern");
        const u32 operandOffset = static_cast<u32>(cmd.commandRange().offset + cmd.position());
        const s16 relative = static_cast<s16>(static_cast<u16>(cmd.u16le("relative")));
        const Address destination{relativeDestination(operandOffset, relative, version)};
        cmd.target(destination, SourceLinkRole::JumpTarget);
        return cmd.call(destination);
      }
      return preserve(rt, cmd, "Unknown FE 0E", subOperandBytes(version, sub), "unknown-fe-0e");
    case 0x0f:
      if (version == AkaoPs1Version::Version3_2) {
        cmd.name("End Pattern");
        return cmd.ret();
      }
      return preserve(rt, cmd, "Unknown FE 0F", subOperandBytes(version, sub), "unknown-fe-0f");
    case 0x14:
      if (isVersion3OrLater(version)) {
        cmd.name("Program Change (Key-Split Instrument)", SequenceSemantic::Program);
        const u8 program = cmd.u8("program");
        cmd.derived("bank", 1).derived("program", program).instrumentRef(1, program);
        rt.instrument(1, program, true);
        return cmd.next();
      }
      return preserve(rt, cmd, "Program Change (Key-Split Instrument)", 2, "key-split-program");
    default:
      return preserve(rt, cmd, fmt::format("Sub Event {:02X}", sub), subOperandBytes(version, sub), "sub-event");
  }
}

struct AkaoCommandReader {
  template <class Runtime>
  static CommandFlow read(Runtime& rt, VmCommandCursor& cmd) {
    const AkaoPs1Version version = rt.context.version;
    const u8 status = cmd.opcode();
    if (isNoteOpcode(version, status)) {
      const bool noteWithLength = isVersion3OrLater(version) && status >= 0xf0 && status <= 0xfd;
      const u8 noteByte = noteWithLength ? static_cast<u8>((status - 0xf0) * 11) : status;
      const bool rest = noteByte >= 0x8f;
      const bool tie = !rest && noteByte >= 0x83;
      u32 delta = kDeltaTimeTable[noteByte % 11];
      if (noteWithLength) {
        delta = cmd.u8("duration");
      }
      if (rt.state.useOneTimeDuration) {
        delta = rt.state.oneTimeDuration;
        rt.state.useOneTimeDuration = false;
      }
      if (rt.state.fixedDuration != 0) {
        delta = rt.state.fixedDuration;
      }
      rt.state.lastDeltaTime = static_cast<u16>(delta);
      u32 sounding = delta;
      if (!isVersion3OrLater(version) && !rt.state.slur && !rt.state.legato) {
        sounding = delta > 2 ? delta - 2 : 0;
      }

      if (rest) {
        cmd.name("Rest", SequenceSemantic::Rest);
        return cmd.wait(delta);
      }
      if (tie) {
        cmd.name("Tie", SequenceSemantic::Note);
        return cmd.wait(delta);
      }

      const u8 relativeKey = noteByte / 11;
      const u8 key = rt.state.drum && !isVersion3OrLater(version) ? static_cast<u8>(24 + relativeKey)
                                                                  : static_cast<u8>(rt.state.octave * 12 + relativeKey);
      cmd.name("Note", SequenceSemantic::Note).derived("key", key, SourceValueDisplay::MidiNote);
      rt.note(key, LevelScale::linearFromMidi7(kNoteVelocity), std::max<u32>(1, sounding));
      return cmd.wait(delta);
    }

    if (status >= 0x9a && status <= 0x9f) {
      cmd.unsupported("Undefined Akao event");
      return cmd.stop();
    }

    if (isSubEventPrefix(version, status)) {
      const u8 sub = cmd.u8("sub_event");
      return readSubEvent(rt, cmd, sub);
    }

    switch (status) {
      case 0xa0:
        cmd.name("End", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback);
        return cmd.end();
      case 0xa1: {
        cmd.name("Program", SequenceSemantic::Program);
        const u8 art = cmd.u8("articulation");
        cmd.derived("bank", 0).derived("program", art).instrumentRef(0, art);
        rt.instrument(0, art, true);
        return cmd.next();
      }
      case 0xa2:
        cmd.name("Next Note Length");
        rt.state.oneTimeDuration = cmd.u8("duration");
        rt.state.useOneTimeDuration = true;
        return cmd.next();
      case 0xa3:
        cmd.name("Volume", SequenceSemantic::Level);
        rt.level(LevelScale::linearFromMidi7(cmd.u8("volume")));
        return cmd.next();
      case 0xa5:
        cmd.name("Octave");
        rt.state.octave = cmd.u8("octave");
        return cmd.next();
      case 0xa6:
        cmd.name("Octave Up");
        ++rt.state.octave;
        return cmd.next();
      case 0xa7:
        cmd.name("Octave Down");
        if (rt.state.octave > 0) {
          --rt.state.octave;
        }
        return cmd.next();
      case 0xa8:
        cmd.name("Expression", SequenceSemantic::Level);
        rt.expression(LevelScale::linearFromMidi7(cmd.u8("expression")));
        return cmd.next();
      case 0xaa:
        cmd.name("Pan", SequenceSemantic::Pan);
        rt.pan((static_cast<double>(cmd.u8("pan")) / 63.5) - 1.0);
        return cmd.next();
      case 0xcc:
        cmd.name("Slur On");
        rt.state.slur = true;
        return cmd.next();
      case 0xcd:
        cmd.name("Slur Off");
        rt.state.slur = false;
        return cmd.next();
      case 0xd0:
        cmd.name("Legato On");
        rt.state.legato = true;
        return cmd.next();
      case 0xd1:
        cmd.name("Legato Off");
        rt.state.legato = false;
        return cmd.next();
      case 0xe8:
        if (version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) {
          cmd.name("Tempo", SequenceSemantic::Tempo);
          const u16 raw = cmd.u16le("tempo");
          rt.tempo(tempoMicrosPerQuarter(version, raw));
          return cmd.next();
        }
        break;
      case 0xec:
        if (version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) {
          return preserve(rt, cmd, "Drum Kit On", 2, "drum-kit-on");
        }
        break;
      case 0xed:
        if (version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) {
          cmd.name("Drum Kit Off", SequenceSemantic::Program);
          rt.state.drum = false;
          return cmd.next();
        }
        break;
      case 0xee:
        if (version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) {
          cmd.name("Jump");
          const u32 operandOffset = static_cast<u32>(cmd.commandRange().offset + cmd.position());
          const s16 relative = static_cast<s16>(static_cast<u16>(cmd.u16le("relative")));
          const Address destination{relativeDestination(operandOffset, relative, version)};
          cmd.target(destination, SourceLinkRole::JumpTarget);
          return destination.value <= cmd.address().value ? cmd.loopCandidate(destination) : cmd.jump(destination);
        }
        break;
      case 0xf2:
        if (version == AkaoPs1Version::Version1_0 || version == AkaoPs1Version::Version1_1) {
          cmd.name("Program Change w/o Attack", SequenceSemantic::Program);
          const u8 art = cmd.u8("articulation");
          cmd.derived("bank", 0).derived("program", art).instrumentRef(0, art);
          rt.instrument(0, art, true);
          return cmd.next();
        }
        break;
      case 0xfc:
        if (version == AkaoPs1Version::Version1_1) {
          return preserve(rt, cmd, "Program Change (Key-Split Instrument)", 2, "key-split-program");
        }
        break;
      default:
        break;
    }

    return preserve(rt, cmd, "Akao Event", directOperandBytes(version, status), "event");
  }
};

[[nodiscard]] SequenceDialect makeAkaoDialect(AkaoPs1Version version) {
  return makeCursorDialect<AkaoTrackState, AkaoContext, AkaoCommandReader>(CursorDialectSpec<AkaoContext>{
      .id = dialectId(version),
      .commandKindPrefix = dialectId(version),
      .timebase = Timebase{.ppqn = kAkaoPpqn},
      .defaultBehavior = SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::Default,
          .commandLimit = 262144,
          .initialReverbSend = 0.25,
      },
      .context = AkaoContext{.version = version},
  });
}

}  // namespace

std::string versionName(AkaoPs1Version version) {
  switch (version) {
    case AkaoPs1Version::Version1_0:
      return "1.0";
    case AkaoPs1Version::Version1_1:
      return "1.1";
    case AkaoPs1Version::Version1_2:
      return "1.2";
    case AkaoPs1Version::Version2:
      return "2";
    case AkaoPs1Version::Version3_0:
      return "3.0";
    case AkaoPs1Version::Version3_1:
      return "3.1";
    case AkaoPs1Version::Version3_2:
      return "3.2";
    case AkaoPs1Version::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string dialectId(AkaoPs1Version version) {
  return "akao-ps1-" + versionName(version);
}

bool isVersion3OrLater(AkaoPs1Version version) noexcept {
  return version >= AkaoPs1Version::Version3_0;
}

AkaoPs1Version determineVersionFromSource(const SourceFile& source) {
  std::string haystack = source.name;
  if (source.title) {
    haystack += " ";
    haystack += *source.title;
  }
  if (!source.path.empty()) {
    haystack += " ";
    haystack += source.path.string();
  }
  haystack = lowerCopy(std::move(haystack));

  if (containsAny(haystack, {"chocobo dungeon 2", "final fantasy viii", "final fantasy 8", "chocobo racing",
                             "saga frontier 2", "racing lagoon"})) {
    return AkaoPs1Version::Version3_1;
  }
  if (containsAny(haystack, {"legend of mana", "front mission 3", "chrono cross", "vagrant story",
                             "final fantasy ix", "final fantasy 9", "final fantasy origins"})) {
    return AkaoPs1Version::Version3_2;
  }
  if (containsAny(haystack, {"final fantasy vii", "final fantasy 7"})) {
    return AkaoPs1Version::Version1_0;
  }
  if (containsAny(haystack, {"saga frontier"})) {
    return AkaoPs1Version::Version1_1;
  }
  if (containsAny(haystack, {"front mission 2", "chocobo's mysterious dungeon"})) {
    return AkaoPs1Version::Version1_2;
  }
  if (containsAny(haystack, {"parasite eve"})) {
    return AkaoPs1Version::Version2;
  }
  if (containsAny(haystack, {"another mind"})) {
    return AkaoPs1Version::Version3_0;
  }
  return AkaoPs1Version::Unknown;
}

AkaoPs1Version guessSequenceVersion(ByteReader reader, u32 offset) {
  if (reader.has(offset + 0x2c, 4) && reader.le32(offset + 0x2c) == 0) {
    return AkaoPs1Version::Version3_2;
  }
  if (reader.has(offset + 0x1c, 4) && reader.le32(offset + 0x1c) == 0) {
    return AkaoPs1Version::Version2;
  }
  return AkaoPs1Version::Version1_1;
}

AkaoPs1Version guessSampleVersion(ByteReader reader, u32 offset) {
  if (reader.has(offset + 0x40, 4) && reader.le32(offset + 0x40) == 0) {
    if (!reader.has(offset + 0x1c, 4)) {
      return AkaoPs1Version::Unknown;
    }
    const u32 artCount = reader.le32(offset + 0x1c);
    if (!reader.has(offset, artCount * 0x10ull)) {
      return AkaoPs1Version::Unknown;
    }
    for (u32 i = 0; i < artCount; ++i) {
      if (reader.u8At(offset + i * 0x10 + 0x0b) != 0) {
        return AkaoPs1Version::Version3_0;
      }
    }
    return AkaoPs1Version::Version3_2;
  }
  if ((reader.has(offset + 0x18, 4) && reader.le32(offset + 0x18) != 0) ||
      (reader.has(offset + 0x1c, 4) && reader.le32(offset + 0x1c) != 0)) {
    return AkaoPs1Version::Version2;
  }
  return AkaoPs1Version::Version1_1;
}

u32 trackAllocationBitsOffset(AkaoPs1Version version) noexcept {
  return isVersion3OrLater(version) ? 0x20 : 0x10;
}

u32 trackHeaderOffset(AkaoPs1Version version) noexcept {
  if (isVersion3OrLater(version)) {
    return 0x40;
  }
  return version == AkaoPs1Version::Version2 ? 0x20 : 0x14;
}

u32 sequenceLengthForVersion(ByteReader reader, u32 offset, AkaoPs1Version version) {
  if (!reader.has(offset + 6, 2)) {
    return 0;
  }
  const u32 stored = reader.le16(offset + 6);
  return isVersion3OrLater(version) ? stored : stored + 0x10;
}

bool isPossibleAkaoSequence(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 0x10) || reader.be32(offset) != kAkaoSignature || reader.le16(offset + 6) == 0) {
    return false;
  }
  const AkaoPs1Version version = guessSequenceVersion(reader, offset);
  const u32 bitsOffset = trackAllocationBitsOffset(version);
  if (!reader.has(offset + bitsOffset, 4)) {
    return false;
  }
  const u32 trackBits = reader.le32(offset + bitsOffset);
  if (!isVersion3OrLater(version) && (trackBits & ~0xffffffu) != 0) {
    return false;
  }
  if (isVersion3OrLater(version)) {
    if (!reader.has(offset + 0x40, 1)) {
      return false;
    }
    if (reader.le32(offset + 0x28) != 0 || reader.le32(offset + 0x2c) != 0 || reader.le32(offset + 0x38) != 0 ||
        reader.le32(offset + 0x3c) != 0) {
      return false;
    }
  }
  return true;
}

std::optional<AkaoSequenceAnalysis> analyzeAkaoSequence(ByteReader reader, const SourceFile& source, u32 offset) {
  if (!isPossibleAkaoSequence(reader, offset)) {
    return std::nullopt;
  }
  AkaoPs1Version version = determineVersionFromSource(source);
  if (version == AkaoPs1Version::Unknown) {
    version = guessSequenceVersion(reader, offset);
  }
  if (version == AkaoPs1Version::Unknown) {
    return std::nullopt;
  }

  const u32 length = sequenceLengthForVersion(reader, offset, version);
  if (length == 0 || !reader.has(offset, std::min<u64>(length, reader.size() - offset))) {
    return std::nullopt;
  }

  AkaoSequenceAnalysis analysis;
  analysis.header = AkaoSequenceHeader{
      .offset = offset,
      .length = static_cast<u32>(std::min<u64>(length, reader.size() - offset)),
      .version = version,
      .sequenceId = reader.le16(offset + 4),
      .trackBits = reader.le32(offset + trackAllocationBitsOffset(version)),
      .trackHeaderOffset = trackHeaderOffset(version),
  };
  if (isVersion3OrLater(version)) {
    analysis.header.sampleSetId = reader.le16(offset + 0x14);
    const u32 instr = reader.le32(offset + 0x30);
    const u32 drum = reader.le32(offset + 0x34);
    if (instr != 0) {
      analysis.header.instrumentSetOffset = offset + 0x30 + instr;
    }
    if (drum != 0) {
      analysis.header.drumSetOffset = offset + 0x34 + drum;
    }
  }

  const u32 trackCount = std::popcount(analysis.header.trackBits);
  const u32 pointerTable = offset + analysis.header.trackHeaderOffset;
  const u32 sequenceEnd = offset + analysis.header.length;
  if (!reader.has(pointerTable, trackCount * 2ull)) {
    return std::nullopt;
  }
  for (u32 i = 0; i < trackCount; ++i) {
    const u32 pointerOffset = analysis.header.trackHeaderOffset + i * 2;
    const u32 base = pointerOffset + (isVersion3OrLater(version) ? 0 : 2);
    const u32 relative = reader.le16(offset + pointerOffset);
    const u32 trackStart = offset + base + relative;
    if (trackStart < sequenceEnd && reader.has(trackStart, 1)) {
      analysis.trackStarts.push_back(trackStart);
    }
  }

  for (const u32 trackStart : analysis.trackStarts) {
    analyzeTrack(reader, analysis, trackStart);
  }
  return analysis;
}

SequenceProgramAsset parseAkaoSequenceProgram(const ScanInput& input, AssetId id, const AkaoSequenceAnalysis& analysis,
                                              std::optional<ScanInstrumentSetRef> instrumentSet,
                                              SourceMapBuilder* sourceMap,
                                              std::vector<Diagnostic>* diagnostics) {
  const SequenceDialect dialect = makeAkaoDialect(analysis.header.version);
  const u32 sequenceEnd = analysis.header.offset + analysis.header.length;
  const std::string name = fmt::format("Akao Seq {:02X}", analysis.header.sequenceId);
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{analysis.header.offset},
      .behavior = dialect.defaultBehavior,
  };

  ItemTree items;
  ItemTreeBuilder itemBuilder(items, input.ids);
  const ItemId root = itemBuilder.add(std::nullopt, ItemKind::Sequence, "akao-sequence", name,
                                      input.reader.range(analysis.header.offset, analysis.header.length));

  if (sourceMap != nullptr) {
    auto header = sourceMap->header("AKAO Sequence Header",
                                    input.reader.range(analysis.header.offset, analysis.header.trackHeaderOffset))
                      .kind("akao-sequence-header")
                      .field("sequence_id", input.reader.range(analysis.header.offset + 4, 2),
                             analysis.header.sequenceId)
                      .field("size", input.reader.range(analysis.header.offset + 6, 2), analysis.header.length)
                      .field("track_bits", input.reader.range(analysis.header.offset + trackAllocationBitsOffset(
                                                                   analysis.header.version),
                                                               4),
                             analysis.header.trackBits, SourceValueDisplay::Hex);
    if (analysis.header.sampleSetId) {
      header.field("sample_set_id", input.reader.range(analysis.header.offset + 0x14, 2),
                   *analysis.header.sampleSetId);
    }
  }

  const std::optional<AssetId> instrumentSetId =
      instrumentSet ? std::optional<AssetId>{instrumentSet->id} : std::nullopt;
  u32 trackIndex = 0;
  for (const u32 start : analysis.trackStarts) {
    auto track = decodeCursorReachableTrack<AkaoTrackState, AkaoContext, AkaoCommandReader>(
        input.reader, dialect,
        CursorTrackDecodeInput{
            .trackIndex = trackIndex,
            .startOffset = start,
            .bytecodeEnd = sequenceEnd,
            .sequenceOffset = analysis.header.offset,
            .sequenceEnd = sequenceEnd,
            .sourceMap = sourceMap,
            .diagnostics = diagnostics,
            .maxCommands = kMaxTrackCommands,
        });
    track.sourceTrackNumber = trackIndex;
    const auto trackItem = itemBuilder.add(root, ItemKind::Track, "akao-track", fmt::format("Track {}", trackIndex + 1),
                                           input.reader.range(start, 0));
    addSourceCommandItemsAndInstrumentReferences(itemBuilder, trackItem, program, dialect, track, instrumentSetId);
    program.tracks.push_back(std::move(track));
    ++trackIndex;
  }

  return SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = std::string(kAkaoFormatName),
              .name = name,
              .range = input.reader.range(analysis.header.offset, analysis.header.length),
              .items = std::move(items),
          },
      .program = std::move(program),
  };
}

void registerAkaoSequenceDialects(SequenceDialectRegistry& registry) {
  registry.add(makeAkaoDialect(AkaoPs1Version::Version1_0));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version1_1));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version1_2));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version2));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version3_0));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version3_1));
  registry.add(makeAkaoDialect(AkaoPs1Version::Version3_2));
}

}  // namespace vgmtrans::formats::akao
