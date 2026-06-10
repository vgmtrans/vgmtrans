/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/CapcomSnes/Value/CapcomSnesValueSequence.h"

#include <fmt/format.h>

#include <set>
#include <string>
#include <utility>
#include <variant>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

[[nodiscard]] std::string capcomSnesCommandDetailKind(const SequencerCommand& command) {
  return "capcom-snes-" + defaultCommandDetailKind(command);
}

[[nodiscard]] std::string capcomSnesCommandDescription(const SequencerCommand& command) {
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

}  // namespace

TrackProgram decodeCapcomSnesTrack(
    ByteReader reader,
    CapcomSnesEngineVersion version,
    u32 sourceTrackNumber,
    u32 startAddress) {
  TrackProgram track{
      .id = TrackId{sourceTrackNumber},
      .sourceTrackNumber = sourceTrackNumber,
      .startAddress = Address{startAddress},
  };

  std::set<u32> visitedOffsets;
  u32 offset = startAddress;
  u32 lastCommandOffset = startAddress;

  while (reader.has(offset, 1) && track.commands.size() < 4096) {
    if (!visitedOffsets.insert(offset).second) {
      // Preserve decoded loop intent as data; the shared lowerer decides playback policy.
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

    auto need = [&](u32 count) {
      return reader.has(offset, count);
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
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(NoteStateCommand{
            .action = NoteStateAction::Attributes,
            .rawValue = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x05:
        if (!need(2)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(TempoCommand{
            .rawValue = reader.be16(offset),
            .range = reader.range(beginOffset, 3),
        });
        offset += 2;
        break;
      case 0x06:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(DurationCommand{
            .rawValue = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x07:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(VolumeCommand{
            .rawValue = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x08:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(ProgramCommand{
            .rawProgram = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x09:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(NoteStateCommand{
            .action = NoteStateAction::Octave,
            .rawValue = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x0a:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(GlobalTransposeCommand{
            .rawSemitones = reader.s8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x0b:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(TransposeCommand{
            .rawSemitones = reader.s8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x0c:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(TuningCommand{
            .rawValue = reader.s8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x0d:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(PortamentoCommand{
            .rawTime = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x0e:
      case 0x0f:
      case 0x10:
      case 0x11:
        if (!need(3)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(RepeatCommand{
            .slot = static_cast<u8>(status - 0x0e),
            .count = reader.u8At(offset),
            .destination = Address{reader.be16(offset + 1)},
            .range = reader.range(beginOffset, 4),
        });
        offset += 3;
        break;
      case 0x12:
      case 0x13:
      case 0x14:
      case 0x15:
        if (!need(3)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(RepeatBreakCommand{
            .slot = static_cast<u8>(status - 0x12),
            .rawAttributes = reader.u8At(offset),
            .destination = Address{reader.be16(offset + 1)},
            .range = reader.range(beginOffset, 4),
        });
        offset += 3;
        break;
      case 0x16: {
        if (!need(2)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        const Address destination{reader.be16(offset)};
        track.commands.push_back(JumpCommand{
            .destination = destination,
            .range = reader.range(beginOffset, 3),
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
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(PanCommand{
            .rawValue = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x19:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(MasterVolumeCommand{
            .rawValue = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x1a:
        if (!need(2)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(LfoCommand{
            .target = reader.u8At(offset) == 0 ? LfoTarget::Pitch
                      : reader.u8At(offset) == 1 ? LfoTarget::Volume
                                                 : LfoTarget::Unknown,
            .rawType = reader.u8At(offset),
            .rawAmount = reader.u8At(offset + 1),
            .range = reader.range(beginOffset, 3),
        });
        offset += 2;
        break;
      case 0x1b:
        if (!need(2)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        offset += 2;
        track.commands.push_back(driverCommand("Echo Param", reader, beginOffset, 3));
        break;
      case 0x1c:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(ReverbCommand{
            .rawValue = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x1d:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(EnvelopeCommand{
            .rawRelease = reader.u8At(offset++),
            .range = reader.range(beginOffset, 2),
        });
        break;
      case 0x1e:
      case 0x1f:
        if (version == CapcomSnesEngineVersion::v1BgmInList) {
          // In the oldest driver these bytes are consumed commands; newer drivers treat them as NOPs.
          if (!need(1)) {
            track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
            return track;
          }
          const auto bytes = reader.slice(beginOffset, 2);
          track.commands.push_back(UnknownCommand{
              .opcode = status,
              .bytes = {bytes.begin(), bytes.end()},
              .range = reader.range(beginOffset, 2),
          });
          ++offset;
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

  SequenceProgram program{
      .timebase = Timebase{.ppqn = kCapcomSnesPpqn},
      .behavior = SequenceBehavior{
          .linearAmplitudeScale = true,
          .writeInitialReverb = true,
          .initialReverb = 0,
          .writeInitialMonoMode = true,
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
      },
      .sequencerProfile = std::string(capcomSnesProfileName(layout.version)),
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
          program.referencedInstruments.push_back(InstrumentRef{
              .asset = instrumentSetId,
              .bank = bank,
              .program = programNumber,
              .range = programCommand->range,
          });
        }
      }
    }
    program.tracks.push_back(std::move(track));
  }

  for (const auto& track : program.tracks) {
    // Legacy playback applies an initial global transpose if it appears before audible data.
    for (const auto& command : track.commands) {
      if (const auto* globalTranspose = std::get_if<GlobalTransposeCommand>(&command)) {
        program.behavior.initialGlobalTranspose = globalTranspose->rawSemitones;
        break;
      }
      if (std::holds_alternative<NoteCommand>(command) || std::holds_alternative<RestCommand>(command) ||
          std::holds_alternative<EndCommand>(command)) {
        break;
      }
    }
    if (program.behavior.initialGlobalTranspose != 0) {
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
      .program = std::move(program),
  };
}

}  // namespace vgmtrans::formats::capcom_snes
