/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/CapcomSnes/CapcomSnesModule.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr u64 kAramSize = 0x10000;
constexpr u32 kMaxTracks = 8;
constexpr u32 kPpqn = 48;

enum class EngineVersion : u8 {
  none = 0,
  v1BgmInList,
  v2BgmUsuallyAtFixedLocation,
  v3BgmFixedLocation,
};

struct BytePatternView {
  std::span<const u8> bytes;
  std::string_view mask;
};

struct Layout {
  EngineVersion version = EngineVersion::none;
  bool hasSongList = false;
  bool bgmAtFixedAddress = false;
  u32 songListAddress = 0;
  u32 bgmHeaderAddress = 0;
  u32 sequenceHeaderAddress = 0;
  bool priorityInHeader = false;
  std::optional<u32> instrumentTableAddress;
  std::optional<u32> spcDirAddress;
};

struct SampleInfo {
  u8 srcn = 0;
  u32 dirEntryAddress = 0;
  u32 startAddress = 0;
  u32 loopAddress = 0;
  u32 encodedLength = 0;
  bool loops = false;
};

struct InstrumentInfo {
  u32 index = 0;
  u32 address = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
  s16 pitchScale = 0;
};

constexpr std::array<u8, 16> kReadSongListPattern{
    0x1c, 0x5d, 0xf5, 0x03, 0x0e, 0xc4, 0xc0, 0xf5,
    0x02, 0x0e, 0xc4, 0xc1, 0x04, 0xc0, 0xf0, 0xdd};
constexpr std::string_view kReadSongListMask = "xxx??x?x??x?x?x?";

constexpr std::array<u8, 16> kReadBgmAddressPattern{
    0x6f, 0x3f, 0xef, 0x06, 0x8f, 0x0d, 0xa1, 0x8f,
    0xaf, 0xa0, 0x3f, 0x82, 0x05, 0x8d, 0x00, 0xdd};
constexpr std::string_view kReadBgmAddressMask = "xx??x??x??x??xxx";

constexpr std::array<u8, 16> kDspRegInitPattern{
    0x8d, 0x03, 0xf6, 0x63, 0x04, 0xc5, 0xf2, 0x00,
    0xf6, 0x66, 0x04, 0xc5, 0xf3, 0x00, 0xfe, 0xf2};
constexpr std::string_view kDspRegInitMask = "x?x??xxxx??xxxx?";

constexpr std::array<u8, 15> kDspRegInitOldPattern{
    0xf5, 0xf9, 0x0b, 0xfd, 0xf5, 0x05, 0x0c, 0x3f,
    0xf2, 0x0b, 0x3d, 0xc8, 0x0c, 0xd0, 0xf1};
constexpr std::string_view kDspRegInitOldMask = "x??xx??x??xx?x?";

constexpr std::array<u8, 12> kLoadInstrTablePattern{
    0x8d, 0x06, 0xcf, 0xda, 0xa0, 0x60, 0x98, 0xac, 0xa0, 0x98, 0x47, 0xa1};
constexpr std::string_view kLoadInstrTableMask = "xxxx?xx??x??";

[[nodiscard]] bool matchPattern(std::span<const u8> bytes, u64 offset, BytePatternView pattern) {
  if (offset > bytes.size() || pattern.bytes.size() > bytes.size() - offset) {
    return false;
  }

  for (size_t i = 0; i < pattern.bytes.size(); ++i) {
    if (pattern.mask[i] == 'x' && bytes[offset + i] != pattern.bytes[i]) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> searchPattern(std::span<const u8> bytes, BytePatternView pattern) {
  if (pattern.bytes.size() != pattern.mask.size() || pattern.bytes.size() > bytes.size()) {
    return std::nullopt;
  }

  for (u64 offset = 0; offset <= bytes.size() - pattern.bytes.size(); ++offset) {
    if (matchPattern(bytes, offset, pattern)) {
      return static_cast<u32>(offset);
    }
  }
  return std::nullopt;
}

[[nodiscard]] u16 readLe16(std::span<const u8> bytes, u64 offset) {
  return static_cast<u16>(bytes[offset] | (bytes[offset + 1] << 8));
}

[[nodiscard]] u16 readBe16(std::span<const u8> bytes, u64 offset) {
  return static_cast<u16>((bytes[offset] << 8) | bytes[offset + 1]);
}

[[nodiscard]] s8 readS8(std::span<const u8> bytes, u64 offset) {
  return static_cast<s8>(bytes[offset]);
}

[[nodiscard]] std::string sourceDisplayName(const SourceFile& source) {
  if (!source.name.empty()) {
    return std::filesystem::path(source.name).stem().string();
  }
  if (!source.path.empty()) {
    return source.path.stem().string();
  }
  return "CapcomSnes";
}

[[nodiscard]] bool isValidBgmHeader(std::span<const u8> bytes, u32 address) {
  if (address + 17 > bytes.size()) {
    return false;
  }

  for (u32 track = 0; track < kMaxTracks; ++track) {
    const u16 trackAddress = readBe16(bytes, address + 1 + track * 2);
    if ((trackAddress & 0xff00) == 0) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] int songListLength(std::span<const u8> bytes, u16 songListAddress) {
  int length = 0;
  for (int songIndex = 0; songIndex <= 0x7f; ++songIndex) {
    const u32 pointerAddress = songListAddress + songIndex * 2;
    if (pointerAddress + 2 > bytes.size()) {
      break;
    }

    const u16 songHeaderAddress = readBe16(bytes, pointerAddress);
    if (songHeaderAddress == 0) {
      ++length;
      continue;
    }
    if (!isValidBgmHeader(bytes, songHeaderAddress)) {
      break;
    }

    ++length;
  }
  return length;
}

[[nodiscard]] u16 currentPlayAddress(std::span<const u8> bytes, EngineVersion version, u8 channel) {
  if (version == EngineVersion::v1BgmInList) {
    return static_cast<u16>(bytes[0x00 + channel * 2 + 1] | (bytes[0x10 + channel * 2 + 1] << 8));
  }
  return static_cast<u16>(bytes[0x00 + channel] | (bytes[0x08 + channel] << 8));
}

[[nodiscard]] std::optional<u8> guessCurrentSong(
    std::span<const u8> bytes,
    EngineVersion version,
    u16 songListAddress) {
  std::optional<u8> guessedSongIndex;
  int bestScore = std::numeric_limits<int>::max();

  const int length = songListLength(bytes, songListAddress);
  for (int songIndex = 0; songIndex < length; ++songIndex) {
    const u16 songHeaderAddress = readBe16(bytes, songListAddress + songIndex * 2);
    if (songHeaderAddress == 0) {
      continue;
    }

    int score = 0;
    int validTrackCount = 0;
    for (u32 track = 0; track < kMaxTracks; ++track) {
      const u16 trackStart = readBe16(bytes, songHeaderAddress + 1 + track * 2);
      const u16 currentAddress = currentPlayAddress(bytes, version, 7 - track);
      if (currentAddress == 0) {
        continue;
      }
      if (trackStart > currentAddress) {
        validTrackCount = 0;
        break;
      }

      score += currentAddress - trackStart;
      ++validTrackCount;
    }

    if (validTrackCount > 0) {
      score = (score * 16) / validTrackCount;
      if (score < bestScore) {
        bestScore = score;
        guessedSongIndex = static_cast<u8>(songIndex);
      }
    }
  }

  return guessedSongIndex;
}

[[nodiscard]] std::map<u8, u8> initialDspRegisterMap(std::span<const u8> bytes) {
  std::map<u8, u8> registers;

  u32 registerCount = 0;
  u32 registerListAddress = 0;
  u32 valueListAddress = 0;

  if (const auto modernOffset = searchPattern(bytes, BytePatternView{kDspRegInitPattern, kDspRegInitMask})) {
    registerCount = bytes[*modernOffset + 1];
    registerListAddress = readLe16(bytes, *modernOffset + 3) + 1;
    valueListAddress = readLe16(bytes, *modernOffset + 9) + 1;
  } else if (const auto oldOffset = searchPattern(bytes, BytePatternView{kDspRegInitOldPattern, kDspRegInitOldMask})) {
    registerCount = bytes[*oldOffset + 12];
    registerListAddress = readLe16(bytes, *oldOffset + 1);
    valueListAddress = readLe16(bytes, *oldOffset + 5);
  } else {
    return registers;
  }

  if (registerListAddress + registerCount > bytes.size() || valueListAddress + registerCount > bytes.size()) {
    return registers;
  }

  for (u32 i = 0; i < registerCount; ++i) {
    registers[bytes[registerListAddress + i]] = bytes[valueListAddress + i];
  }

  return registers;
}

[[nodiscard]] std::optional<Layout> findLayout(std::span<const u8> bytes) {
  if (bytes.size() != kAramSize) {
    return std::nullopt;
  }

  Layout layout;

  if (const auto offset = searchPattern(bytes, BytePatternView{kReadSongListPattern, kReadSongListMask})) {
    layout.hasSongList = true;
    layout.songListAddress = std::min(readLe16(bytes, *offset + 3), readLe16(bytes, *offset + 8));
  }

  if (const auto offset = searchPattern(bytes, BytePatternView{kReadBgmAddressPattern, kReadBgmAddressMask})) {
    layout.bgmAtFixedAddress = true;
    layout.bgmHeaderAddress = static_cast<u32>((bytes[*offset + 5] << 8) | bytes[*offset + 8]);
  }

  if (layout.hasSongList) {
    if (layout.bgmAtFixedAddress) {
      layout.version = EngineVersion::v2BgmUsuallyAtFixedLocation;
      const bool bgmHeaderCoversSongList =
          layout.bgmHeaderAddress <= layout.songListAddress && layout.bgmHeaderAddress + 17 > layout.songListAddress;
      if (bgmHeaderCoversSongList || !isValidBgmHeader(bytes, layout.bgmHeaderAddress)) {
        layout.bgmAtFixedAddress = false;
      }
    } else {
      layout.version = EngineVersion::v1BgmInList;
    }
  } else if (layout.bgmAtFixedAddress) {
    layout.version = EngineVersion::v3BgmFixedLocation;
  } else {
    return std::nullopt;
  }

  if (layout.bgmAtFixedAddress) {
    layout.sequenceHeaderAddress = layout.bgmHeaderAddress + 1;
    layout.priorityInHeader = false;
  } else if (layout.hasSongList) {
    const auto currentSong = guessCurrentSong(bytes, layout.version, static_cast<u16>(layout.songListAddress));
    if (!currentSong) {
      return std::nullopt;
    }
    layout.sequenceHeaderAddress = readBe16(bytes, layout.songListAddress + (*currentSong * 2));
    layout.priorityInHeader = true;
  }

  if (!isValidBgmHeader(bytes, layout.priorityInHeader ? layout.sequenceHeaderAddress : layout.sequenceHeaderAddress - 1)) {
    return std::nullopt;
  }

  if (const auto offset = searchPattern(bytes, BytePatternView{kLoadInstrTablePattern, kLoadInstrTableMask})) {
    layout.instrumentTableAddress = static_cast<u32>(bytes[*offset + 7] | (bytes[*offset + 10] << 8));
  }

  const auto dspRegisters = initialDspRegisterMap(bytes);
  if (const auto found = dspRegisters.find(0x5d); found != dspRegisters.end()) {
    layout.spcDirAddress = static_cast<u32>(found->second) << 8;
  }

  return layout;
}

[[nodiscard]] ItemId addItem(
    ItemTree& tree,
    ScanIdAllocator& ids,
    std::optional<ItemId> parent,
    ItemKind kind,
    std::string detailKind,
    std::string name,
    SourceRange range,
    std::string description = {}) {
  const auto id = ids.nextItemId();
  tree.nodes.push_back(ItemNode{
      .id = id,
      .parent = parent,
      .kind = kind,
      .detailKind = std::move(detailKind),
      .name = std::move(name),
      .description = std::move(description),
      .range = range,
  });
  if (parent) {
    auto found = std::ranges::find_if(tree.nodes, [parent](const ItemNode& node) {
      return node.id == *parent;
    });
    if (found != tree.nodes.end()) {
      found->children.push_back(id);
    }
  } else {
    tree.root = id;
  }
  return id;
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

[[nodiscard]] SourceRange commandRange(const SequencerCommand& command) {
  return std::visit([](const auto& typedCommand) { return typedCommand.range; }, command);
}

[[nodiscard]] std::string commandDetailKind(const SequencerCommand& command) {
  return std::visit(
      [](const auto& typedCommand) -> std::string {
        using Command = std::decay_t<decltype(typedCommand)>;
        if constexpr (std::is_same_v<Command, NoteCommand>) {
          return "capcom-snes-note";
        } else if constexpr (std::is_same_v<Command, RestCommand>) {
          return "capcom-snes-rest";
        } else if constexpr (std::is_same_v<Command, DurationCommand>) {
          return "capcom-snes-duration";
        } else if constexpr (std::is_same_v<Command, ProgramCommand>) {
          return "capcom-snes-program";
        } else if constexpr (std::is_same_v<Command, VolumeCommand>) {
          return "capcom-snes-volume";
        } else if constexpr (std::is_same_v<Command, PanCommand>) {
          return "capcom-snes-pan";
        } else if constexpr (std::is_same_v<Command, TempoCommand>) {
          return "capcom-snes-tempo";
        } else if constexpr (std::is_same_v<Command, TransposeCommand>) {
          return "capcom-snes-transpose";
        } else if constexpr (std::is_same_v<Command, GlobalTransposeCommand>) {
          return "capcom-snes-global-transpose";
        } else if constexpr (std::is_same_v<Command, TuningCommand>) {
          return "capcom-snes-tuning";
        } else if constexpr (std::is_same_v<Command, PortamentoCommand>) {
          return "capcom-snes-portamento";
        } else if constexpr (std::is_same_v<Command, LfoCommand>) {
          return "capcom-snes-lfo";
        } else if constexpr (std::is_same_v<Command, ReverbCommand>) {
          return "capcom-snes-reverb";
        } else if constexpr (std::is_same_v<Command, EnvelopeCommand>) {
          return "capcom-snes-envelope";
        } else if constexpr (std::is_same_v<Command, MasterVolumeCommand>) {
          return "capcom-snes-master-volume";
        } else if constexpr (std::is_same_v<Command, JumpCommand>) {
          return "capcom-snes-jump";
        } else if constexpr (std::is_same_v<Command, RepeatCommand>) {
          return "capcom-snes-repeat";
        } else if constexpr (std::is_same_v<Command, RepeatBreakCommand>) {
          return "capcom-snes-repeat-break";
        } else if constexpr (std::is_same_v<Command, LoopBoundaryCommand>) {
          return "capcom-snes-loop-boundary";
        } else if constexpr (std::is_same_v<Command, EndCommand>) {
          return "capcom-snes-end";
        } else if constexpr (std::is_same_v<Command, UnknownCommand>) {
          return "capcom-snes-unknown";
        } else {
          return "capcom-snes-driver-specific";
        }
      },
      command);
}

[[nodiscard]] std::string commandName(const SequencerCommand& command) {
  return std::visit(
      [](const auto& typedCommand) -> std::string {
        using Command = std::decay_t<decltype(typedCommand)>;
        if constexpr (std::is_same_v<Command, NoteCommand>) {
          return "Note";
        } else if constexpr (std::is_same_v<Command, RestCommand>) {
          return "Rest";
        } else if constexpr (std::is_same_v<Command, DurationCommand>) {
          return "Duration";
        } else if constexpr (std::is_same_v<Command, ProgramCommand>) {
          return "Program";
        } else if constexpr (std::is_same_v<Command, VolumeCommand>) {
          return "Volume";
        } else if constexpr (std::is_same_v<Command, PanCommand>) {
          return "Pan";
        } else if constexpr (std::is_same_v<Command, TempoCommand>) {
          return "Tempo";
        } else if constexpr (std::is_same_v<Command, TransposeCommand>) {
          return "Transpose";
        } else if constexpr (std::is_same_v<Command, GlobalTransposeCommand>) {
          return "Global Transpose";
        } else if constexpr (std::is_same_v<Command, TuningCommand>) {
          return "Tuning";
        } else if constexpr (std::is_same_v<Command, PortamentoCommand>) {
          return "Portamento";
        } else if constexpr (std::is_same_v<Command, LfoCommand>) {
          return "LFO";
        } else if constexpr (std::is_same_v<Command, ReverbCommand>) {
          return "Reverb";
        } else if constexpr (std::is_same_v<Command, EnvelopeCommand>) {
          return "Envelope";
        } else if constexpr (std::is_same_v<Command, MasterVolumeCommand>) {
          return "Master Volume";
        } else if constexpr (std::is_same_v<Command, JumpCommand>) {
          return "Jump";
        } else if constexpr (std::is_same_v<Command, RepeatCommand>) {
          return "Repeat";
        } else if constexpr (std::is_same_v<Command, RepeatBreakCommand>) {
          return "Repeat Break";
        } else if constexpr (std::is_same_v<Command, LoopBoundaryCommand>) {
          return "Loop Boundary";
        } else if constexpr (std::is_same_v<Command, EndCommand>) {
          return "End";
        } else if constexpr (std::is_same_v<Command, UnknownCommand>) {
          return "Unknown";
        } else {
          return typedCommand.name;
        }
      },
      command);
}

[[nodiscard]] std::string commandDescription(const SequencerCommand& command) {
  return std::visit(
      [](const auto& typedCommand) -> std::string {
        using Command = std::decay_t<decltype(typedCommand)>;
        if constexpr (std::is_same_v<Command, NoteCommand>) {
          return "Key " + std::to_string(typedCommand.key) + ", length index " +
                 std::to_string(typedCommand.rawDuration);
        } else if constexpr (std::is_same_v<Command, RestCommand>) {
          return "Length index " + std::to_string(typedCommand.rawDuration);
        } else if constexpr (std::is_same_v<Command, DurationCommand>) {
          return "Raw " + std::to_string(typedCommand.rawValue);
        } else if constexpr (std::is_same_v<Command, ProgramCommand>) {
          return "Program " + std::to_string(typedCommand.rawProgram);
        } else if constexpr (std::is_same_v<Command, VolumeCommand> || std::is_same_v<Command, PanCommand> ||
                             std::is_same_v<Command, TempoCommand> || std::is_same_v<Command, MasterVolumeCommand> ||
                             std::is_same_v<Command, ReverbCommand>) {
          return "Raw " + std::to_string(typedCommand.rawValue);
        } else if constexpr (std::is_same_v<Command, TransposeCommand>) {
          return "Semitones " + std::to_string(typedCommand.rawSemitones);
        } else if constexpr (std::is_same_v<Command, GlobalTransposeCommand>) {
          return "Semitones " + std::to_string(typedCommand.rawSemitones);
        } else if constexpr (std::is_same_v<Command, TuningCommand>) {
          return "Raw " + std::to_string(typedCommand.rawValue);
        } else if constexpr (std::is_same_v<Command, PortamentoCommand>) {
          return "Time " + std::to_string(typedCommand.rawTime);
        } else if constexpr (std::is_same_v<Command, LfoCommand>) {
          return "Type " + std::to_string(typedCommand.rawType) + ", amount " + std::to_string(typedCommand.rawAmount);
        } else if constexpr (std::is_same_v<Command, EnvelopeCommand>) {
          return "Release " + std::to_string(typedCommand.rawRelease);
        } else if constexpr (std::is_same_v<Command, JumpCommand>) {
          return "Destination $" + std::to_string(typedCommand.destination.value);
        } else if constexpr (std::is_same_v<Command, RepeatCommand>) {
          return "Slot " + std::to_string(typedCommand.slot) + ", count " + std::to_string(typedCommand.count) +
                 ", destination $" + std::to_string(typedCommand.destination.value);
        } else if constexpr (std::is_same_v<Command, RepeatBreakCommand>) {
          return "Slot " + std::to_string(typedCommand.slot) + ", attributes " +
                 std::to_string(typedCommand.rawAttributes) + ", destination $" +
                 std::to_string(typedCommand.destination.value);
        } else if constexpr (std::is_same_v<Command, LoopBoundaryCommand>) {
          return "Destination $" + std::to_string(typedCommand.destination.value) +
                 ", trigger $" + std::to_string(typedCommand.trigger.value);
        } else if constexpr (std::is_same_v<Command, UnknownCommand>) {
          return "Opcode " + std::to_string(typedCommand.opcode);
        } else if constexpr (std::is_same_v<Command, DriverSpecificCommand>) {
          return "Bytes " + std::to_string(typedCommand.bytes.size());
        } else {
          return {};
        }
      },
      command);
}

[[nodiscard]] TrackProgram decodeTrack(
    ByteReader reader,
    EngineVersion version,
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
        track.commands.push_back(driverCommand("Toggle Triplet", reader, beginOffset, 1));
        break;
      case 0x01:
        track.commands.push_back(driverCommand("Toggle Slur", reader, beginOffset, 1));
        break;
      case 0x02:
        track.commands.push_back(driverCommand("Dotted Note On", reader, beginOffset, 1));
        break;
      case 0x03:
        track.commands.push_back(driverCommand("Toggle 2-Octave Up", reader, beginOffset, 1));
        break;
      case 0x04:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        ++offset;
        track.commands.push_back(driverCommand("Note Attributes", reader, beginOffset, 2));
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
        ++offset;
        track.commands.push_back(driverCommand("Octave", reader, beginOffset, 2));
        break;
      case 0x0a:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(GlobalTransposeCommand{
            .rawSemitones = readS8(reader.slice(0, reader.size()), offset),
            .range = reader.range(beginOffset, 2),
        });
        ++offset;
        break;
      case 0x0b:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(TransposeCommand{
            .rawSemitones = readS8(reader.slice(0, reader.size()), offset),
            .range = reader.range(beginOffset, 2),
        });
        ++offset;
        break;
      case 0x0c:
        if (!need(1)) {
          track.commands.push_back(UnknownCommand{.opcode = status, .range = reader.range(beginOffset, 1)});
          return track;
        }
        track.commands.push_back(TuningCommand{
            .rawValue = readS8(reader.slice(0, reader.size()), offset),
            .range = reader.range(beginOffset, 2),
        });
        ++offset;
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
        if (version == EngineVersion::v1BgmInList) {
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

[[nodiscard]] SequenceAsset parseSequence(
    const ScanInput& input,
    const Layout& layout,
    AssetId sequenceId,
    std::string_view displayName) {
  const u32 headerSize = (layout.priorityInHeader ? 1 : 0) + kMaxTracks * 2;
  ItemTree items;
  const auto root = addItem(items,
                            input.ids,
                            std::nullopt,
                            ItemKind::Sequence,
                            "capcom-snes-sequence-header",
                            "Sequence Header",
                            input.reader.range(layout.sequenceHeaderAddress, headerSize));

  SequenceProgram program{
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceBehavior{
          .linearAmplitudeScale = true,
          .writeInitialReverb = true,
          .initialReverb = 0,
          .writeInitialMonoMode = true,
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
      },
  };

  const u32 pointerBase = layout.sequenceHeaderAddress + (layout.priorityInHeader ? 1 : 0);
  std::set<std::pair<u32, u32>> referencedInstruments;
  for (int trackIndex = static_cast<int>(kMaxTracks) - 1; trackIndex >= 0; --trackIndex) {
    const auto pointerOffset = pointerBase + static_cast<u32>(trackIndex) * 2;
    const u16 trackAddress = input.reader.be16(pointerOffset);
    if (trackAddress == 0) {
      continue;
    }

    const auto trackItem = addItem(items,
                                   input.ids,
                                   root,
                                   ItemKind::Track,
                                   "capcom-snes-track-pointer",
                                   "Track Pointer",
                                   input.reader.range(pointerOffset, 2),
                                   "Track starts at $" + std::to_string(trackAddress));
    auto track = decodeTrack(input.reader,
                             layout.version,
                             static_cast<u32>(kMaxTracks - 1 - trackIndex),
                             trackAddress);
    for (const auto& command : track.commands) {
      static_cast<void>(addItem(items,
                                input.ids,
                                trackItem,
                                ItemKind::Command,
                                commandDetailKind(command),
                                commandName(command),
                                commandRange(command),
                                commandDescription(command)));
      if (const auto* programCommand = std::get_if<ProgramCommand>(&command)) {
        const u32 bank = programCommand->rawProgram >> 7;
        const u32 programNumber = programCommand->rawProgram & 0x7f;
        if (referencedInstruments.insert({bank, programNumber}).second) {
          program.referencedInstruments.push_back(InstrumentRef{
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

[[nodiscard]] bool sampleDirIsValid(std::span<const u8> bytes, u32 dirEntryAddress, bool validateSample);

[[nodiscard]] u32 sampleLength(std::span<const u8> bytes, u32 startAddress, bool& loop) {
  u32 offset = startAddress;
  while (true) {
    if (offset + 9 > bytes.size()) {
      return 0;
    }

    const u8 flag = bytes[offset];
    offset += 9;
    if ((flag & 1) != 0) {
      loop = (flag & 2) != 0;
      break;
    }
  }
  return offset - startAddress;
}

[[nodiscard]] bool sampleDirIsValid(std::span<const u8> bytes, u32 dirEntryAddress, bool validateSample) {
  if (dirEntryAddress + 4 > bytes.size()) {
    return false;
  }

  const u16 sampleStart = readLe16(bytes, dirEntryAddress);
  const u16 sampleLoop = readLe16(bytes, dirEntryAddress + 2);
  if (sampleLoop < sampleStart || sampleStart + 9 >= bytes.size()) {
    return false;
  }

  if (validateSample) {
    bool loops = false;
    const u32 length = sampleLength(bytes, sampleStart, loops);
    if (length == 0) {
      return false;
    }
    if (loops && sampleLoop >= sampleStart + length) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool blankInstrumentSlot(std::span<const u8> bytes, u32 address) {
  for (u32 offset = address; offset < address + 6; ++offset) {
    if (bytes[offset] != 0 && bytes[offset] != 0xff) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool instrumentHeaderIsValid(
    std::span<const u8> bytes,
    u32 address,
    u32 spcDirAddress,
    bool validateSample) {
  if (address + 6 > bytes.size()) {
    return false;
  }

  const u8 srcn = bytes[address];
  const u8 adsr1 = bytes[address + 1];
  const u8 gain = bytes[address + 3];
  if (srcn >= 0x80 || (adsr1 == 0 && gain == 0)) {
    return false;
  }

  const u32 dirEntryAddress = spcDirAddress + srcn * 4;
  if (!sampleDirIsValid(bytes, dirEntryAddress, validateSample)) {
    return false;
  }

  const u16 sampleStart = readLe16(bytes, dirEntryAddress);
  const u16 sampleLoop = readLe16(bytes, dirEntryAddress + 2);
  return sampleStart <= sampleLoop && ((sampleLoop - sampleStart) % 9) == 0;
}

[[nodiscard]] std::vector<InstrumentInfo> parseInstrumentInfos(
    std::span<const u8> bytes,
    u32 instrumentTableAddress,
    u32 spcDirAddress) {
  std::vector<InstrumentInfo> instruments;

  for (u32 instrumentIndex = 0; instrumentIndex <= 0xff; ++instrumentIndex) {
    const u32 address = instrumentTableAddress + instrumentIndex * 6;
    if (address + 6 > bytes.size()) {
      break;
    }

    if (blankInstrumentSlot(bytes, address)) {
      continue;
    }
    if (!instrumentHeaderIsValid(bytes, address, spcDirAddress, false)) {
      break;
    }
    if (!instrumentHeaderIsValid(bytes, address, spcDirAddress, true)) {
      continue;
    }

    instruments.push_back(InstrumentInfo{
        .index = instrumentIndex,
        .address = address,
        .srcn = bytes[address],
        .adsr1 = bytes[address + 1],
        .adsr2 = bytes[address + 2],
        .gain = bytes[address + 3],
        .pitchScale = static_cast<s16>(readBe16(bytes, address + 4)),
    });
  }

  return instruments;
}

[[nodiscard]] std::vector<SampleInfo> parseSampleInfos(
    std::span<const u8> bytes,
    u32 spcDirAddress,
    const std::vector<InstrumentInfo>& instruments) {
  std::vector<u8> srcns;
  srcns.reserve(instruments.size());
  for (const auto& instrument : instruments) {
    if (std::ranges::find(srcns, instrument.srcn) == srcns.end()) {
      srcns.push_back(instrument.srcn);
    }
  }
  std::ranges::sort(srcns);

  std::vector<SampleInfo> samples;
  samples.reserve(srcns.size());
  for (const u8 srcn : srcns) {
    const u32 dirEntryAddress = spcDirAddress + srcn * 4;
    if (!sampleDirIsValid(bytes, dirEntryAddress, true)) {
      continue;
    }

    const u16 start = readLe16(bytes, dirEntryAddress);
    const u16 loop = readLe16(bytes, dirEntryAddress + 2);
    bool loops = false;
    const u32 length = sampleLength(bytes, start, loops);
    samples.push_back(SampleInfo{
        .srcn = srcn,
        .dirEntryAddress = dirEntryAddress,
        .startAddress = start,
        .loopAddress = loop,
        .encodedLength = length,
        .loops = loops,
    });
  }

  return samples;
}

[[nodiscard]] Tuning capcomInstrumentTuning(s16 pitchScale) {
  constexpr int baseUnityKey = 96;
  constexpr double referencePitch = 0x10b0 / 4096.0;

  const double ratio = pitchScale != 0 ? (static_cast<double>(pitchScale) / 256.0) * referencePitch : 1.0;
  const double semitones = 12.0 * std::log2(ratio);
  int coarse = static_cast<int>(std::lround(semitones));
  int fine = static_cast<int>(std::lround((semitones - coarse) * 100.0));
  if (fine >= 50) {
    ++coarse;
    fine -= 100;
  } else if (fine < -50) {
    --coarse;
    fine += 100;
  }

  const int unityKeyShift = baseUnityKey - coarse - baseUnityKey;
  return Tuning{.cents = unityKeyShift * 100 + fine};
}

[[nodiscard]] SampleCollectionAsset parseSamples(
    const ScanInput& input,
    AssetId sampleCollectionId,
    const std::vector<SampleInfo>& sampleInfos,
    std::string_view displayName) {
  ItemTree items;
  u32 rootOffset = 0;
  u32 rootSize = 0;
  if (!sampleInfos.empty()) {
    rootOffset = sampleInfos.front().dirEntryAddress;
    const u32 lastEnd = sampleInfos.back().dirEntryAddress + 4;
    rootSize = lastEnd - rootOffset;
  }

  const auto root = addItem(items,
                            input.ids,
                            std::nullopt,
                            ItemKind::SampleCollection,
                            "snes-sample-dir",
                            "Sample DIR",
                            input.reader.range(rootOffset, rootSize));

  SampleCollection collection;
  collection.samples.reserve(sampleInfos.size());
  for (const auto& sampleInfo : sampleInfos) {
    const u32 loopStart = sampleInfo.loopAddress >= sampleInfo.startAddress
                              ? ((sampleInfo.loopAddress - sampleInfo.startAddress) / 9) * 16
                              : 0;
    const u32 decodedLength = (sampleInfo.encodedLength / 9) * 16;
    collection.samples.push_back(Sample{
        .name = "Sample " + std::to_string(sampleInfo.srcn),
        .codec = AudioCodec::SnesBrr,
        .encodedData = input.reader.range(sampleInfo.startAddress, sampleInfo.encodedLength),
        .sampleRate = 32000,
        .channels = 1,
        .bitsPerSample = 16,
        .loop = Loop{
            .enabled = sampleInfo.loops,
            .start = loopStart,
            .length = sampleInfo.loops && decodedLength >= loopStart ? decodedLength - loopStart : 0,
        },
    });

    static_cast<void>(addItem(items,
                              input.ids,
                              root,
                              ItemKind::Sample,
                              "snes-brr-sample",
                              "Sample " + std::to_string(sampleInfo.srcn),
                              input.reader.range(sampleInfo.startAddress, sampleInfo.encodedLength),
                              "DIR entry $" + std::to_string(sampleInfo.dirEntryAddress)));
  }

  return SampleCollectionAsset{
      .metadata = AssetMetadata{
          .id = sampleCollectionId,
          .format = "CapcomSnes",
          .name = std::string(displayName) + " Samples",
          .range = input.reader.range(rootOffset, rootSize),
          .items = std::move(items),
      },
      .samples = std::move(collection),
  };
}

[[nodiscard]] InstrumentBankAsset parseInstrumentBank(
    const ScanInput& input,
    AssetId instrumentBankId,
    AssetId sampleCollectionId,
    const std::vector<InstrumentInfo>& instrumentInfos,
    const std::vector<SampleInfo>& sampleInfos,
    std::string_view displayName) {
  std::map<u8, u32> sampleIndexBySrcn;
  for (u32 index = 0; index < sampleInfos.size(); ++index) {
    sampleIndexBySrcn[sampleInfos[index].srcn] = index;
  }

  ItemTree items;
  u32 rootOffset = instrumentInfos.empty() ? 0 : instrumentInfos.front().address;
  u32 rootSize = instrumentInfos.empty() ? 0 : (instrumentInfos.back().address + 6) - rootOffset;
  const auto root = addItem(items,
                            input.ids,
                            std::nullopt,
                            ItemKind::InstrumentBank,
                            "capcom-snes-instrument-table",
                            "Instrument Table",
                            input.reader.range(rootOffset, rootSize));

  InstrumentBank bank;
  bank.instruments.reserve(instrumentInfos.size());
  for (const auto& info : instrumentInfos) {
    const auto sampleIndex = sampleIndexBySrcn.find(info.srcn);
    if (sampleIndex == sampleIndexBySrcn.end()) {
      continue;
    }

    Instrument instrument{
        .bank = info.index >> 7,
        .program = info.index & 0x7f,
        .name = "Instrument " + std::to_string(info.index),
        .range = input.reader.range(info.address, 6),
    };
    instrument.regions.push_back(Region{
        .sample = SampleRef{
            .collection = sampleCollectionId,
            .index = sampleIndex->second,
        },
        .range = input.reader.range(info.address, 6),
        .tuning = capcomInstrumentTuning(info.pitchScale),
        .envelope = Envelope{
            .attack = info.adsr1,
            .decay = info.adsr2,
            .sustain = info.gain,
        },
    });

    bank.instruments.push_back(std::move(instrument));
    const auto instrumentItem = addItem(items,
                                        input.ids,
                                        root,
                                        ItemKind::Instrument,
                                        "capcom-snes-instrument",
                                        "Instrument " + std::to_string(info.index),
                                        input.reader.range(info.address, 6),
                                        "SRCN " + std::to_string(info.srcn));
    static_cast<void>(addItem(items,
                              input.ids,
                              instrumentItem,
                              ItemKind::Region,
                              "capcom-snes-region",
                              "Region",
                              input.reader.range(info.address, 6),
                              "Sample " + std::to_string(sampleIndex->second)));
  }

  return InstrumentBankAsset{
      .metadata = AssetMetadata{
          .id = instrumentBankId,
          .format = "CapcomSnes",
          .name = std::string(displayName) + " Instruments",
          .range = input.reader.range(rootOffset, rootSize),
          .items = std::move(items),
      },
      .bank = std::move(bank),
  };
}

}  // namespace

std::string_view CapcomSnesModule::name() const {
  return "CapcomSnes";
}

bool CapcomSnesModule::canScan(const SourceFile&, std::span<const u8> bytes) const {
  return findLayout(bytes).has_value();
}

ScanResult CapcomSnesModule::scan(const ScanInput& input) const {
  const auto layout = findLayout(input.reader.slice(0, input.reader.size()));
  if (!layout) {
    return {};
  }

  const std::string displayName = sourceDisplayName(input.source);
  const auto sequenceId = input.ids.nextAssetId();
  const auto instrumentBankId = input.ids.nextAssetId();
  const auto sampleCollectionId = input.ids.nextAssetId();

  ScanResult result;
  result.assets.emplace_back(parseSequence(input, *layout, sequenceId, displayName));

  std::vector<InstrumentInfo> instrumentInfos;
  std::vector<SampleInfo> sampleInfos;
  if (layout->instrumentTableAddress && layout->spcDirAddress) {
    instrumentInfos = parseInstrumentInfos(input.reader.slice(0, input.reader.size()),
                                           *layout->instrumentTableAddress,
                                           *layout->spcDirAddress);
    sampleInfos = parseSampleInfos(input.reader.slice(0, input.reader.size()), *layout->spcDirAddress, instrumentInfos);
  }

  if (!instrumentInfos.empty() && !sampleInfos.empty()) {
    result.assets.emplace_back(parseInstrumentBank(input,
                                                   instrumentBankId,
                                                   sampleCollectionId,
                                                   instrumentInfos,
                                                   sampleInfos,
                                                   displayName));
    result.assets.emplace_back(parseSamples(input, sampleCollectionId, sampleInfos, displayName));
  }

  Collection collection{
      .id = input.ids.nextCollectionId(),
      .name = displayName,
      .sequence = sequenceId,
  };
  if (!instrumentInfos.empty() && !sampleInfos.empty()) {
    collection.instrumentBanks.push_back(instrumentBankId);
    collection.sampleCollections.push_back(sampleCollectionId);
  }
  result.collections.push_back(std::move(collection));

  if (!layout->instrumentTableAddress || !layout->spcDirAddress) {
    result.diagnostics.push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = "CapcomSnes sequence found, but instrument table or SPC DIR address was not detected",
        .range = input.reader.range(0, input.reader.size()),
    });
  }

  return result;
}

void registerCapcomSnesModule(FormatRegistry& registry) {
  registry.add(std::make_unique<CapcomSnesModule>());
}

}  // namespace vgmtrans::formats::capcom_snes
