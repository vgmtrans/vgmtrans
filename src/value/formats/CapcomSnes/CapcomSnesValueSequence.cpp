/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesValueSequence.h"

#include <fmt/format.h>

#include <set>
#include <string>
#include <utility>
#include <variant>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

[[nodiscard]] std::string capcomSnesCommandDetailKind(const Command& command) {
  return "capcom-snes-" + defaultCommandDetailKind(command);
}

[[nodiscard]] std::string capcomSnesCommandDescription(const Command& command) {
  return defaultCommandDescription(command);
}

[[nodiscard]] DriverSpecificCommand driverCommand(
    std::string name,
    ByteReader reader,
    u32 offset,
    u32 size) {
  const auto bytes = reader.slice(offset, size);
  return DriverSpecificCommand{
      .name = std::move(name),
      .bytes = {bytes.begin(), bytes.end()},
      .range = reader.range(offset, size),
  };
}

struct TrackDecodeCursor {
  ByteReader reader;
  u32& offset;

  [[nodiscard]] bool has(u32 count) const {
    return reader.has(offset, count);
  }

  [[nodiscard]] UnknownCommand truncated(u8 opcode, u32 beginOffset) const {
    return UnknownCommand{.opcode = opcode, .range = reader.range(beginOffset, 1)};
  }

  [[nodiscard]] SourceRange rangeFrom(u32 beginOffset) const {
    return reader.range(beginOffset, offset - beginOffset);
  }

  [[nodiscard]] u8 readU8() {
    return reader.u8At(offset++);
  }

  [[nodiscard]] s8 readS8() {
    return reader.s8At(offset++);
  }

  [[nodiscard]] u16 readBe16() {
    const u16 value = reader.be16(offset);
    offset += 2;
    return value;
  }

  void skip(u32 count) {
    offset += count;
  }
};

}  // namespace

CommandTrack decodeCapcomSnesTrack(
    ByteReader reader,
    CapcomSnesEngineVersion version,
    u32 sourceTrackNumber,
    u32 startAddress) {
  CommandTrack track{
      .id = TrackId{sourceTrackNumber},
      .sourceTrackNumber = sourceTrackNumber,
      .startAddress = Address{startAddress},
  };

  std::set<u32> visitedOffsets;
  u32 offset = startAddress;
  u32 lastCommandOffset = startAddress;

  while (reader.has(offset, 1) && track.commands.size() < 4096) {
    if (!visitedOffsets.insert(offset).second) {
      // Preserve decoded loop intent as data; the shared builder decides playback policy.
      track.commands.push_back(LoopBoundaryCommand{
          .destination = Address{offset},
          .trigger = Address{lastCommandOffset},
          .range = reader.range(offset, 0),
      });
      break;
    }

    const u32 beginOffset = offset;
    lastCommandOffset = beginOffset;
    const u8 status = reader.u8At(offset++);

    if (status >= 0x20) {
      // High opcodes pack note/rest identity and duration into one byte.
      const u8 keyIndex = status & 0x1f;
      const u8 lengthIndex = status >> 5;
      if (keyIndex == 0) {
        track.commands.push_back(RestCommand{
            .rawDuration = lengthIndex,
            .range = reader.range(beginOffset, 1),
        });
      } else {
        track.commands.push_back(NoteCommand{
            .key = keyIndex,
            .rawDuration = lengthIndex,
            .range = reader.range(beginOffset, 1),
        });
      }
      continue;
    }

    TrackDecodeCursor cursor{reader, offset};
    auto finishTruncated = [&] {
      track.commands.push_back(cursor.truncated(status, beginOffset));
      return track;
    };

    switch (status) {
      case 0x00:
        track.commands.push_back(NoteStateCommand{
            .action = NoteStateAction::ToggleTriplet,
            .range = reader.range(beginOffset, 1),
        });
        break;
      case 0x01:
        track.commands.push_back(NoteStateCommand{
            .action = NoteStateAction::ToggleSlur,
            .range = reader.range(beginOffset, 1),
        });
        break;
      case 0x02:
        track.commands.push_back(NoteStateCommand{
            .action = NoteStateAction::EnableDotted,
            .range = reader.range(beginOffset, 1),
        });
        break;
      case 0x03:
        track.commands.push_back(NoteStateCommand{
            .action = NoteStateAction::ToggleOctaveUp,
            .range = reader.range(beginOffset, 1),
        });
        break;
      case 0x04:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(NoteStateCommand{
            .action = NoteStateAction::Attributes,
            .rawValue = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x05:
        if (!cursor.has(2)) {
          return finishTruncated();
        }
        track.commands.push_back(TempoCommand{
            .rawValue = cursor.readBe16(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x06:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(DurationCommand{
            .rawValue = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x07:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(VolumeCommand{
            .rawValue = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x08:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(ProgramCommand{
            .rawProgram = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x09:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(NoteStateCommand{
            .action = NoteStateAction::Octave,
            .rawValue = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x0a:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(GlobalTransposeCommand{
            .rawSemitones = cursor.readS8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x0b:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(TransposeCommand{
            .rawSemitones = cursor.readS8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x0c:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(TuningCommand{
            .rawValue = cursor.readS8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x0d:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(PortamentoCommand{
            .rawTime = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x0e:
      case 0x0f:
      case 0x10:
      case 0x11: {
        if (!cursor.has(3)) {
          return finishTruncated();
        }
        const u8 repeatCount = cursor.readU8();
        const Address repeatDestination{cursor.readBe16()};
        track.commands.push_back(RepeatCommand{
            .slot = static_cast<u8>(status - 0x0e),
            .count = repeatCount,
            .destination = repeatDestination,
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      }
      case 0x12:
      case 0x13:
      case 0x14:
      case 0x15: {
        if (!cursor.has(3)) {
          return finishTruncated();
        }
        const u8 rawAttributes = cursor.readU8();
        const Address breakDestination{cursor.readBe16()};
        track.commands.push_back(RepeatBreakCommand{
            .slot = static_cast<u8>(status - 0x12),
            .rawAttributes = rawAttributes,
            .destination = breakDestination,
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      }
      case 0x16: {
        if (!cursor.has(2)) {
          return finishTruncated();
        }
        const Address destination{cursor.readBe16()};
        track.commands.push_back(JumpCommand{
            .destination = destination,
            .range = cursor.rangeFrom(beginOffset),
        });
        offset = static_cast<u32>(destination.value);
        break;
      }
      case 0x17:
        track.commands.push_back(EndCommand{
            .range = reader.range(beginOffset, 1),
        });
        return track;
      case 0x18:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(PanCommand{
            .rawValue = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x19:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(MasterVolumeCommand{
            .rawValue = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x1a: {
        if (!cursor.has(2)) {
          return finishTruncated();
        }
        const u8 lfoType = cursor.readU8();
        const u8 lfoAmount = cursor.readU8();
        track.commands.push_back(LfoCommand{
            .target = lfoType == 0 ? LfoTarget::Pitch
                      : lfoType == 1 ? LfoTarget::Volume
                                     : LfoTarget::Unknown,
            .rawType = lfoType,
            .rawAmount = lfoAmount,
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      }
      case 0x1b:
        if (!cursor.has(2)) {
          return finishTruncated();
        }
        cursor.skip(2);
        track.commands.push_back(driverCommand("Echo Param", reader, beginOffset, 3));
        break;
      case 0x1c:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(ReverbCommand{
            .rawValue = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x1d:
        if (!cursor.has(1)) {
          return finishTruncated();
        }
        track.commands.push_back(EnvelopeCommand{
            .rawRelease = cursor.readU8(),
            .range = cursor.rangeFrom(beginOffset),
        });
        break;
      case 0x1e:
      case 0x1f:
        if (version == CapcomSnesEngineVersion::v1BgmInList) {
          // In the oldest driver these bytes are consumed commands; newer drivers treat them as NOPs.
          if (!cursor.has(1)) {
            return finishTruncated();
          }
          cursor.skip(1);
          const auto bytes = reader.slice(beginOffset, 2);
          track.commands.push_back(UnknownCommand{
              .opcode = status,
              .bytes = {bytes.begin(), bytes.end()},
              .range = cursor.rangeFrom(beginOffset),
          });
        } else {
          track.commands.push_back(driverCommand("Nop", reader, beginOffset, 1));
        }
        break;
      default:
        track.commands.push_back(UnknownCommand{
            .opcode = status,
            .range = reader.range(beginOffset, 1),
        });
        return track;
    }
  }

  return track;
}

SequenceAsset parseCapcomSnesSequence(
    const ScanInput& input,
    const CapcomSnesLayout& layout,
    AssetId sequenceId,
    std::optional<AssetId> instrumentSetId,
    std::string_view displayName) {
  const u32 headerSize = (layout.priorityInHeader ? 1 : 0) + kCapcomSnesMaxTracks * 2;
  ItemTree items;
  ItemTreeBuilder itemBuilder(items, input.ids);
  const auto root = itemBuilder.add(std::nullopt,
                                    ItemKind::Sequence,
                                    "capcom-snes-sequence-header",
                                    "Sequence Header",
                                    input.reader.range(layout.sequenceHeaderAddress, headerSize));

  CommandSequence commandSequence{
      .timebase = Timebase{.ppqn = kCapcomSnesPpqn},
      .behavior = SequenceBehavior{
          .linearAmplitudeScale = true,
          .writeInitialReverb = true,
          .initialReverb = 0,
          .writeInitialMonoMode = true,
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
      },
      .midiSequenceProfile = std::string(capcomSnesProfileName(layout.version)),
  };

  const u32 pointerBase = layout.sequenceHeaderAddress + (layout.priorityInHeader ? 1 : 0);
  std::set<std::pair<u32, u32>> referencedInstruments;
  // Capcom stores track pointers in reverse channel order.
  for (int trackIndex = static_cast<int>(kCapcomSnesMaxTracks) - 1; trackIndex >= 0; --trackIndex) {
    const auto pointerOffset = pointerBase + static_cast<u32>(trackIndex) * 2;
    const u16 trackAddress = input.reader.be16(pointerOffset);
    if (trackAddress == 0) {
      continue;
    }

    const auto trackItem = itemBuilder.add(root,
                                           ItemKind::Track,
                                           "capcom-snes-track-pointer",
                                           "Track Pointer",
                                           input.reader.range(pointerOffset, 2),
                                           fmt::format("Track starts at ${:04X}", trackAddress));
    auto track = decodeCapcomSnesTrack(input.reader,
                                       layout.version,
                                       static_cast<u32>(kCapcomSnesMaxTracks - 1 - trackIndex),
                                       trackAddress);
    for (const auto& command : track.commands) {
      static_cast<void>(itemBuilder.add(trackItem,
                                        ItemKind::Command,
                                        capcomSnesCommandDetailKind(command),
                                        defaultCommandName(command),
                                        commandRange(command),
                                        capcomSnesCommandDescription(command)));
      if (const auto* programCommand = std::get_if<ProgramCommand>(&command)) {
        const u32 bank = programCommand->rawProgram >> 7;
        const u32 programNumber = programCommand->rawProgram & 0x7f;
        if (referencedInstruments.insert({bank, programNumber}).second) {
          commandSequence.referencedInstruments.push_back(InstrumentRef{
              .asset = instrumentSetId,
              .bank = bank,
              .program = programNumber,
              .range = programCommand->range,
          });
        }
      }
    }
    commandSequence.tracks.push_back(std::move(track));
  }

  for (const auto& track : commandSequence.tracks) {
    // Legacy playback applies an initial global transpose if it appears before audible data.
    for (const auto& command : track.commands) {
      if (const auto* globalTranspose = std::get_if<GlobalTransposeCommand>(&command)) {
        commandSequence.behavior.initialGlobalTranspose = globalTranspose->rawSemitones;
        break;
      }
      if (std::holds_alternative<NoteCommand>(command) || std::holds_alternative<RestCommand>(command) ||
          std::holds_alternative<EndCommand>(command)) {
        break;
      }
    }
    if (commandSequence.behavior.initialGlobalTranspose != 0) {
      break;
    }
  }

  return SequenceAsset{
      .metadata = AssetMetadata{
          .id = sequenceId,
          .format = "CapcomSnes",
          .name = std::string(displayName),
          .range = input.reader.range(layout.sequenceHeaderAddress, headerSize),
          .items = std::move(items),
      },
      .commandSequence = std::move(commandSequence),
  };
}

}  // namespace vgmtrans::formats::capcom_snes
