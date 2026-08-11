/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HudsonSnes/HudsonSnes.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::hudson_snes {

using namespace core;
using namespace std::string_view_literals;

namespace {

constexpr MaskedBytePattern kNoteLengths{"\xc0\x60\x30\x18\x0c\x06\x03\x01"sv, "xxxxxxxx"sv};

// Early drivers fetch the four engine pointers through indexed absolute reads.
constexpr MaskedBytePattern kEngineEarly{
    "\xf6\x00\x00\xc4\x00\xfc\xf6\x00\x00\xc4\x00\x2f\x00\xf6\x00\x00"
    "\xc4\x00\xfc\xf6\x00\x00\xc4\x00\x2f\x00\xf6\x00\x00\xc5\x00\x00"sv,
    "x??x?xx??x?x?x??x?xx??x?x?x??x??"sv,
};

// 1.x and 2.x copy one global engine structure into direct-page pointers.
constexpr MaskedBytePattern kEngineLater{
    "\xe5\x00\x00\xec\x00\x00\xda\x00\xe5\x00\x00\xec\x00\x00\xda\x00"
    "\xe5\x00\x00\xec\x00\x00\xda\x00\xe5\x00\x00\xc5\x00\x00\xe5\x00\x00\xc4\x00"sv,
    "x??x??x?x??x??x?x??x??x?x??x??x??x?"sv,
};

constexpr MaskedBytePattern kLoadTracks{
    "\xe8\x00\xd4\x00\xd4\x00\xd5\x00\x00\x4b\x00\x90\x00\xfc\xf7\x00"
    "\xd5\x00\x00\xd5\x00\x00\xfc\xf7\x00\xd5\x00\x00\xd5\x00\x00"sv,
    "xxx?x?x??x?x?xx?x??x??xx?x??x??"sv,
};

constexpr MaskedBytePattern kLoadDirEarly{"\xe4\x00\x8d\x5d\x4f\x0c"sv, "x?xxxx"sv};

enum class HeaderField : u8 {
  Invalid,
  End,
  Tracks,
  Timebase,
  Instruments,
  Drums,
  PitchScripts,
  CustomWaveforms,
  Echo,
  NoteVelocity,
  VolumeCurves,
};

struct HeaderCommand {
  HeaderField field = HeaderField::Invalid;
  u8 unitSize = 0;
};

constexpr std::array<HeaderCommand, 10> kEarlyHeaderCommands{{
    {HeaderField::End},
    {HeaderField::Timebase},
    {HeaderField::Instruments, 1},
    {HeaderField::Drums, 1},
    {HeaderField::Instruments, 4},
    {HeaderField::PitchScripts, 2},
}};

constexpr std::array<HeaderCommand, 10> kV2HeaderCommands{{
    {HeaderField::End},
    {HeaderField::Tracks},
    {HeaderField::Timebase},
    {HeaderField::Instruments, 4},
    {HeaderField::Drums, 4},
    {HeaderField::PitchScripts, 2},
    {HeaderField::CustomWaveforms, 2},
    {HeaderField::Echo},
    {HeaderField::NoteVelocity},
    {HeaderField::VolumeCurves, 2},
}};

struct CountedBlock {
  u32 address = 0;
  u32 size = 0;
};

[[nodiscard]] HeaderCommand headerCommand(Version version, u8 opcode) {
  const auto& commands = version == Version::V2 ? kV2HeaderCommands : kEarlyHeaderCommands;
  return opcode < commands.size() ? commands[opcode] : HeaderCommand{};
}

[[nodiscard]] std::optional<CountedBlock> readCountedBlock(ByteReader reader, u32& cursor, u8 unitSize) {
  if (!reader.has(cursor, 1)) {
    return std::nullopt;
  }
  const u32 size = reader.u8At(cursor++) * unitSize;
  if (!reader.has(cursor, size)) {
    return std::nullopt;
  }
  const CountedBlock block{.address = cursor, .size = size};
  cursor += size;
  return block;
}

void appendRecipeRows(ByteReader reader, CountedBlock block, HeaderField field, SequenceRecipes& recipes) {
  const u32 rows = block.size / 4;
  for (u32 row = 0; row < rows; ++row) {
    const u32 item = block.address + row * 4;
    if (field == HeaderField::Instruments) {
      recipes.instruments.push_back(InstrumentRow{
          .program = static_cast<u8>(row),
          .srcn = reader.u8At(item),
          .adsr1 = reader.u8At(item + 1),
          .adsr2 = reader.u8At(item + 2),
          .gain = reader.u8At(item + 3),
          .source = reader.range(item, 4),
      });
    } else {
      recipes.drums.push_back(DrumSlot{
          .note = static_cast<u8>(row),
          .sourceProgram = reader.u8At(item),
          .sourceKey = reader.u8At(item + 1),
          .volume = reader.u8At(item + 2),
          .pan = reader.u8At(item + 3),
          .source = reader.range(item, 4),
      });
    }
  }
}

[[nodiscard]] bool readTracks(ByteReader reader, u32& cursor, ParsedHeader& header) {
  if (!reader.has(cursor, 1)) {
    return false;
  }
  const u8 available = reader.u8At(cursor++);
  for (u8 channel = 0; channel < kTrackCount; ++channel) {
    if ((available & (1u << channel)) == 0) {
      continue;
    }
    if (!reader.has(cursor, 2)) {
      return false;
    }
    const u16 address = reader.le16(cursor);
    cursor += 2;
    if (address == 0 || !reader.has(address, 1)) {
      return false;
    }
    header.tracks.emplace_back(channel, address);
  }
  return true;
}

[[nodiscard]] std::vector<u16> pointerTable(ByteReader reader, u32 address, u32 bytes) {
  std::vector<u16> result;
  result.reserve(bytes / 2);
  for (u32 offset = 0; offset + 1 < bytes; offset += 2) {
    result.push_back(reader.le16(address + offset));
  }
  return result;
}

void readInitialEcho(ByteReader reader, u32 address, ParsedHeader& header) {
  header.initialEchoLeft = static_cast<s8>(reader.u8At(address));
  header.initialEchoRight = static_cast<s8>(reader.u8At(address + 1));
  header.initialEchoDelay = reader.u8At(address + 2);
  header.initialEchoFeedback = static_cast<s8>(reader.u8At(address + 3));
  header.initialEchoFilter = reader.u8At(address + 4);
  header.initialEchoMask = reader.u8At(address + 5);
}

void decodeWaveforms(ByteReader reader, const std::vector<u16>& pointers, SequenceRecipes& recipes) {
  for (u32 index = 0; index < pointers.size() && index < 128; ++index) {
    const u32 start = pointers[index];
    if (start == 0 || !reader.has(start, 1)) {
      continue;
    }
    CustomWaveform waveform{.index = static_cast<u8>(index)};
    u32 cursor = start;
    while (reader.has(cursor, 1) && waveform.samples.size() < 128) {
      const u8 sample = reader.u8At(cursor++);
      if (sample == 0x80) {
        break;
      }
      waveform.samples.push_back(static_cast<s8>(sample));
    }
    if (!waveform.samples.empty()) {
      waveform.source = reader.range(start, cursor - start);
      recipes.customWaveforms.push_back(std::move(waveform));
    }
  }
}

void decodePitchScripts(ByteReader reader, const std::vector<u16>& pointers, SequenceRecipes& recipes) {
  for (u32 index = 0; index < pointers.size() && index < 128; ++index) {
    const u32 start = pointers[index];
    if (start == 0 || !reader.has(start, 1)) {
      continue;
    }
    PitchScript script{.index = static_cast<u8>(index)};
    u32 cursor = start;
    u32 first = start;
    u32 last = start;
    std::vector<std::pair<u32, u16>> repeats;
    for (u32 operations = 0; operations < 512 && script.steps.size() < 128 && reader.has(cursor, 1); ++operations) {
      first = std::min(first, cursor);
      const u8 opcode = reader.u8At(cursor);
      if (opcode < 0xfb) {
        if (!reader.has(cursor, 2)) {
          break;
        }
        script.steps.push_back(PitchScriptStep{
            .duration = opcode,
            .target = static_cast<s8>(reader.u8At(cursor + 1)),
        });
        cursor += 2;
        last = std::max(last, cursor);
        continue;
      }

      // FB/FF sustain the current value; FD restarts the script. A single
      // flattened cycle is the useful finite representation for both.
      if (opcode == 0xfb || opcode == 0xfd || opcode == 0xff) {
        last = std::max(last, cursor + 1);
        break;
      }
      const u32 bytes = opcode == 0xfc ? 3 : 4;
      if (!reader.has(cursor, bytes)) {
        break;
      }
      last = std::max(last, cursor + bytes);
      const u16 destination = reader.le16(cursor + bytes - 2);
      if (destination == 0 || !reader.has(destination, 1)) {
        break;
      }
      if (opcode == 0xfc) {
        cursor = destination;
        continue;
      }

      const u8 count = reader.u8At(cursor + 1);
      if (count == 0) {
        break;
      }
      const auto repeat = std::ranges::find(repeats, cursor, &std::pair<u32, u16>::first);
      if (repeat == repeats.end()) {
        repeats.emplace_back(cursor, count);
        cursor = destination;
      } else if (repeat->second > 1) {
        --repeat->second;
        cursor = destination;
      } else {
        cursor += bytes;
      }
    }
    if (!script.steps.empty()) {
      script.source = reader.range(first, last - first);
      recipes.pitchScripts.push_back(std::move(script));
    }
  }
}

void decodeVolumeCurves(ByteReader reader, const std::vector<u16>& pointers, SequenceRecipes& recipes) {
  for (u32 index = 0; index < pointers.size() && index < 128; ++index) {
    const u32 start = pointers[index];
    if (start == 0 || !reader.has(start, 128)) {
      continue;
    }
    VolumeCurve curve{.index = static_cast<u8>(index), .source = reader.range(start, 128)};
    curve.offsets.reserve(128);
    for (u32 note = 0; note < 128; ++note) {
      curve.offsets.push_back(static_cast<s8>(reader.u8At(start + note)));
    }
    recipes.volumeCurves.push_back(std::move(curve));
  }
}

struct SongCandidate {
  u16 address = 0;
  ParsedHeader header;
};

[[nodiscard]] std::vector<u16> songHeaders(ByteReader reader, u16 list) {
  std::vector<u16> result;
  u32 cutoff = kAramSize;
  for (u32 index = 0; index < 128; ++index) {
    const u32 entry = list + index * 2;
    if (!reader.has(entry, 2) || entry >= cutoff) {
      break;
    }
    const u16 address = reader.le16(entry);
    if (address != 0 && address != 0xffff) {
      result.push_back(address);
      cutoff = std::min(cutoff, static_cast<u32>(address));
    }
  }
  return result;
}

[[nodiscard]] std::optional<u16> currentLoopPoint(ByteReader reader) {
  const auto load = findBytePattern(reader, kLoadTracks);
  if (!load || !reader.has(*load + 30, 1)) {
    return std::nullopt;
  }
  const u16 lowAddress = reader.le16(*load + 20);
  const u16 highAddress = reader.le16(*load + 29);
  if (!reader.has(lowAddress, 1) || !reader.has(highAddress, 1)) {
    return std::nullopt;
  }
  const u16 loop = static_cast<u16>(reader.u8At(lowAddress) | (reader.u8At(highAddress) << 8));
  return loop == 0 || loop == 0xffff ? std::nullopt : std::optional{loop};
}

[[nodiscard]] std::vector<u16> referencedPointers(ByteReader reader, u16 table, const std::set<u8>& indexes) {
  if (table == 0 || indexes.empty()) {
    return {};
  }
  std::vector<u16> pointers(*indexes.rbegin() + 1);
  for (const u8 index : indexes) {
    const u32 entry = table + index * 2u;
    if (reader.has(entry, 2)) {
      pointers[index] = reader.le16(entry);
    }
  }
  return pointers;
}

template <class Recipe>
void appendMissing(std::vector<Recipe>& destination, std::vector<Recipe> source) {
  for (Recipe& recipe : source) {
    if (std::ranges::none_of(destination, [&](const Recipe& item) { return item.index == recipe.index; })) {
      destination.push_back(std::move(recipe));
    }
  }
}

}  // namespace

const char* versionName(Version version) {
  switch (version) {
    case Version::Early:
      return "early";
    case Version::V1:
      return "1.x";
    case Version::V2:
      return "2.x";
  }
  return "unknown";
}

std::optional<ParsedHeader> parseHeader(ByteReader reader, Version version, u32 address) {
  if (!reader.has(address, 1)) {
    return std::nullopt;
  }
  ParsedHeader header;
  u32 cursor = address;
  if (version != Version::V2 && !readTracks(reader, cursor, header)) {
    return std::nullopt;
  }

  std::vector<u16> pitchPointers;
  std::vector<u16> waveformPointers;
  std::vector<u16> volumePointers;
  bool ended = false;
  for (u32 command = 0; command < 32 && reader.has(cursor, 1); ++command) {
    const u8 opcode = reader.u8At(cursor++);
    const HeaderCommand descriptor = headerCommand(version, opcode);
    switch (descriptor.field) {
      case HeaderField::End:
        ended = true;
        break;
      case HeaderField::Tracks:
        if (!readTracks(reader, cursor, header)) {
          return std::nullopt;
        }
        break;
      case HeaderField::Timebase:
        if (!reader.has(cursor, 1)) {
          return std::nullopt;
        }
        header.timebaseShift = reader.u8At(cursor++) & 3;
        break;
      case HeaderField::Instruments:
      case HeaderField::Drums: {
        const auto block = readCountedBlock(reader, cursor, descriptor.unitSize);
        if (!block) {
          return std::nullopt;
        }
        appendRecipeRows(reader, *block, descriptor.field, header.recipes);
        break;
      }
      case HeaderField::PitchScripts:
      case HeaderField::CustomWaveforms:
      case HeaderField::VolumeCurves: {
        const auto block = readCountedBlock(reader, cursor, descriptor.unitSize);
        if (!block) {
          return std::nullopt;
        }
        std::vector<u16> pointers = pointerTable(reader, block->address, block->size);
        if (descriptor.field == HeaderField::PitchScripts) {
          pitchPointers = std::move(pointers);
        } else if (descriptor.field == HeaderField::CustomWaveforms) {
          waveformPointers = std::move(pointers);
        } else {
          volumePointers = std::move(pointers);
        }
        break;
      }
      case HeaderField::Echo: {
        if (!reader.has(cursor, 1)) {
          return std::nullopt;
        }
        const bool defaults = reader.u8At(cursor++) != 0;
        const u32 echo = defaults ? 0x0858 : cursor;
        if (!reader.has(echo, 6)) {
          return std::nullopt;
        }
        readInitialEcho(reader, echo, header);
        if (!defaults) {
          cursor += 6;
        }
        break;
      }
      case HeaderField::NoteVelocity:
        if (!reader.has(cursor, 1)) {
          return std::nullopt;
        }
        header.noteVelocity = reader.u8At(cursor++) != 0;
        break;
      case HeaderField::Invalid:
      default:
        return std::nullopt;
    }
    if (ended) {
      break;
    }
  }

  if (!ended || header.tracks.empty()) {
    return std::nullopt;
  }
  header.range = reader.range(address, cursor - address);
  decodeWaveforms(reader, waveformPointers, header.recipes);
  decodePitchScripts(reader, pitchPointers, header.recipes);
  decodeVolumeCurves(reader, volumePointers, header.recipes);
  return header;
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto noteLengths = findBytePattern(reader, kNoteLengths);
  if (!noteLengths) {
    return std::nullopt;
  }

  Version version;
  u16 songList = 0;
  u16 dir = 0;
  u16 tuning = 0;
  if (const auto engine = findBytePattern(reader, kEngineLater)) {
    const u16 enginePointer = reader.le16(*engine + 1);
    if (!reader.has(enginePointer, 2)) {
      return std::nullopt;
    }
    version = enginePointer < 0x0800 ? Version::V1 : Version::V2;
    if (!reader.has(enginePointer, 7)) {
      return std::nullopt;
    }
    const u16 songListTable = reader.le16(enginePointer);
    if (!reader.has(songListTable, 2)) {
      return std::nullopt;
    }
    songList = reader.le16(songListTable);
    tuning = reader.le16(enginePointer + 4);
    dir = static_cast<u16>(reader.u8At(enginePointer + 6) << 8);
  } else if (const auto earlyEngine = findBytePattern(reader, kEngineEarly)) {
    version = Version::Early;
    const u8 listPointer = reader.u8At(*earlyEngine + 4);
    if (!reader.has(listPointer, 2)) {
      return std::nullopt;
    }
    const u16 listTable = reader.le16(listPointer);
    if (!reader.has(listTable, 2)) {
      return std::nullopt;
    }
    songList = reader.le16(listTable);

    const auto loadDir = findBytePattern(reader, kLoadDirEarly);
    const u16 tuningPointer = reader.le16(*earlyEngine + 30);
    if (!loadDir || !reader.has(0x100 + reader.u8At(*loadDir + 1), 1) || !reader.has(tuningPointer, 1)) {
      return std::nullopt;
    }
    dir = static_cast<u16>(reader.u8At(0x100 + reader.u8At(*loadDir + 1)) << 8);
    tuning = static_cast<u16>((reader.u8At(tuningPointer) + 1) << 8);
  } else {
    return std::nullopt;
  }

  if (songList == 0 || !reader.has(songList, 2) || !reader.has(dir, 4) || !reader.has(tuning, 4)) {
    return std::nullopt;
  }

  std::vector<SongCandidate> candidates;
  for (const u16 address : songHeaders(reader, songList)) {
    if (auto header = parseHeader(reader, version, address)) {
      candidates.push_back(SongCandidate{.address = address, .header = std::move(*header)});
    }
  }
  if (candidates.empty()) {
    return std::nullopt;
  }

  const std::optional<u16> loop = currentLoopPoint(reader);
  auto selected = candidates.begin();
  if (loop) {
    u32 bestDistance = std::numeric_limits<u32>::max();
    for (auto candidate = candidates.begin(); candidate != candidates.end(); ++candidate) {
      if (candidate->address <= *loop && *loop - candidate->address < bestDistance) {
        selected = candidate;
        bestDistance = *loop - candidate->address;
      }
    }
  }

  const u32 tablePointers = version == Version::Early ? 0x30 : (version == Version::V1 ? 0x3a : 0x40);
  const auto liveTable = [&](u32 offset) -> u16 {
    return reader.has(tablePointers + offset, 2) ? reader.le16(tablePointers + offset) : 0;
  };
  return Layout{
      .version = version,
      .sequenceHeaderAddress = selected->address,
      .noteLengthTableAddress = static_cast<u16>(*noteLengths),
      .spcDirAddress = dir,
      .tuningTableAddress = tuning,
      .activeInstrumentTableAddress = liveTable(0),
      .activeDrumTableAddress = liveTable(2),
      .activePitchTableAddress = version == Version::Early ? u16{0} : liveTable(4),
      .activeWaveformTableAddress = version == Version::V2 ? liveTable(6) : u16{0},
      .activeVolumeTableAddress = version == Version::V2 ? liveTable(8) : u16{0},
  };
}

void supplementLiveRecipes(ByteReader reader, const Layout& layout, SequenceReferences references,
                           SequenceRecipes& recipes) {
  if (recipes.drums.empty() && layout.activeDrumTableAddress != 0) {
    u32 end = kAramSize;
    for (const u16 candidate :
         {layout.activePitchTableAddress, layout.activeWaveformTableAddress, layout.activeVolumeTableAddress}) {
      if (candidate > layout.activeDrumTableAddress) {
        end = std::min(end, static_cast<u32>(candidate));
      }
    }
    const u32 rows = std::min<u32>((end - layout.activeDrumTableAddress) / 4, 68);
    for (u32 note = 0; note < rows; ++note) {
      const u32 item = layout.activeDrumTableAddress + note * 4;
      if (!reader.has(item, 4)) {
        break;
      }
      const bool invalidVolume = layout.version == Version::V2 && (reader.u8At(item + 2) & 0x7f) > 0x4f;
      if (invalidVolume || (reader.u8At(item + 3) & 0x1f) > 30) {
        break;
      }
      recipes.drums.push_back(DrumSlot{
          .note = static_cast<u8>(note),
          .sourceProgram = reader.u8At(item),
          .sourceKey = reader.u8At(item + 1),
          .volume = reader.u8At(item + 2),
          .pan = reader.u8At(item + 3),
          .source = reader.range(item, 4),
      });
      references.programs.insert(reader.u8At(item));
    }
  }

  for (const u8 programNumber : references.programs) {
    if (std::ranges::any_of(recipes.instruments,
                            [&](const InstrumentRow& row) { return row.program == programNumber; })) {
      continue;
    }
    const u32 item = layout.activeInstrumentTableAddress + programNumber * 4u;
    if (layout.activeInstrumentTableAddress == 0 || !reader.has(item, 4)) {
      continue;
    }
    recipes.instruments.push_back(InstrumentRow{
        .program = programNumber,
        .srcn = reader.u8At(item),
        .adsr1 = reader.u8At(item + 1),
        .adsr2 = reader.u8At(item + 2),
        .gain = reader.u8At(item + 3),
        .source = reader.range(item, 4),
    });
  }

  SequenceRecipes live;
  decodePitchScripts(reader, referencedPointers(reader, layout.activePitchTableAddress, references.pitchScripts), live);
  decodeWaveforms(reader, referencedPointers(reader, layout.activeWaveformTableAddress, references.customWaveforms),
                  live);
  decodeVolumeCurves(reader, referencedPointers(reader, layout.activeVolumeTableAddress, references.volumeCurves),
                     live);
  appendMissing(recipes.pitchScripts, std::move(live.pitchScripts));
  appendMissing(recipes.customWaveforms, std::move(live.customWaveforms));
  appendMissing(recipes.volumeCurves, std::move(live.volumeCurves));

  if (layout.version != Version::Early) {
    const u32 presets = layout.version == Version::V1 ? 0x0780 : 0x0840;
    for (InstrumentRow& row : recipes.instruments) {
      if (row.adsr1 != 0 || row.adsr2 != 0 || (row.gain & 0x80) != 0) {
        continue;
      }
      const u32 preset = presets + row.gain * 4u;
      if (reader.has(preset, 4)) {
        row.srcn = reader.u8At(preset);
        row.adsr1 = reader.u8At(preset + 1);
        row.adsr2 = reader.u8At(preset + 2);
        row.gain = reader.u8At(preset + 3);
      }
    }
  }

  for (InstrumentRow& row : recipes.instruments) {
    const u32 tuning = layout.tuningTableAddress + row.srcn * 4u;
    if (!reader.has(tuning, 4)) {
      continue;
    }
    row.pitchScale = static_cast<u16>((reader.u8At(tuning) << 8) | reader.u8At(tuning + 1));
    row.coarseTuning = static_cast<s8>(reader.u8At(tuning + 2));
    row.fineTuning = static_cast<s8>(reader.u8At(tuning + 3));
  }
}

}  // namespace vgmtrans::formats::hudson_snes
