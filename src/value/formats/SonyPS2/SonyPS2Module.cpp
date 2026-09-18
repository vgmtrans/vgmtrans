/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS2/SonyPS2.h"

#include "value/extractors/PsfExtractor.h"

#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::sony_ps2 {

using namespace core;

namespace {

struct Psf2Selection {
  std::string sequence = "default.sq";
  std::string header = "default.hd";
  std::string body = "default.bd";
  u32 midi = 0;
  u32 volume = 128;
  u32 reverbType = 5;
  u32 reverbDepth = 0x3fff;
};

[[nodiscard]] std::string normalizedPath(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  std::ranges::replace(result, '\\', '/');
  if (const auto device = result.find(':'); device != std::string::npos) {
    result.erase(0, device + 1);
  }
  while (result.starts_with("./") || result.starts_with('/')) {
    result.erase(0, result.front() == '/' ? 1 : 2);
  }
  if (result.ends_with(";1")) {
    result.resize(result.size() - 2);
  }
  return result;
}

[[nodiscard]] std::string_view baseName(std::string_view path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] bool selectedMember(const SourceFile& source, std::string_view selected) {
  const auto member = source.attribute("container-member");
  if (!member) {
    return true;
  }
  const std::string actualPath = normalizedPath(*member);
  const std::string selectedPath = normalizedPath(selected);
  return selectedPath.find('/') == std::string::npos ? baseName(actualPath) == selectedPath
                                                     : actualPath == selectedPath;
}

[[nodiscard]] std::vector<std::string> commandWords(std::string_view line) {
  std::vector<std::string> words;
  for (size_t cursor = 0; cursor < line.size();) {
    while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) {
      ++cursor;
    }
    if (cursor == line.size()) {
      break;
    }
    const char quote = line[cursor] == '\'' || line[cursor] == '"' ? line[cursor++] : '\0';
    const size_t begin = cursor;
    while (cursor < line.size() &&
           (quote != '\0' ? line[cursor] != quote : !std::isspace(static_cast<unsigned char>(line[cursor])))) {
      ++cursor;
    }
    words.emplace_back(line.substr(begin, cursor - begin));
    if (quote != '\0' && cursor < line.size()) {
      ++cursor;
    }
  }
  return words;
}

void applySqOption(Psf2Selection& selection, std::string_view word) {
  if (word.size() < 4 || word.front() != '-' || word[2] != '=') {
    return;
  }
  const char option = static_cast<char>(std::tolower(static_cast<unsigned char>(word[1])));
  const std::string_view value = word.substr(3);
  switch (option) {
    case 's':
      selection.sequence = value;
      return;
    case 'h':
      selection.header = value;
      return;
    case 'b':
      selection.body = value;
      return;
    default:
      break;
  }

  u32 number = 0;
  const auto [position, error] = std::from_chars(value.data(), value.data() + value.size(), number);
  if (error != std::errc{} || position != value.data() + value.size()) {
    return;
  }
  switch (option) {
    case 'd':
      selection.reverbDepth = number;
      break;
    case 'n':
      selection.midi = number;
      break;
    case 'r':
      selection.reverbType = number;
      break;
    case 'v':
      selection.volume = number;
      break;
    default:
      break;
  }
}

[[nodiscard]] std::optional<Psf2Selection> psf2Selection(const SourceFile& source) {
  const auto ini = source.attribute(vgmtrans::formats::psf::kPsf2IniAttribute);
  if (!ini) {
    return std::nullopt;
  }
  for (size_t begin = 0; begin < ini->size();) {
    const size_t end = ini->find_first_of("\r\n", begin);
    const auto words = commandWords(ini->substr(begin, end == std::string_view::npos ? end : end - begin));
    if (!words.empty() && baseName(normalizedPath(words.front())) == "sq.irx") {
      Psf2Selection selection;
      for (const auto& word : words | std::views::drop(1)) {
        applySqOption(selection, word);
      }
      return selection;
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return std::nullopt;
}

[[nodiscard]] std::string lowerExtension(const SourceFile& source) {
  const auto extension = [](const std::filesystem::path& path) {
    std::string result = path.extension().string();
    std::ranges::transform(result, result.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return result;
  };
  if (const auto member = source.attribute("container-member")) {
    if (auto result = extension(*member); !result.empty()) {
      return result;
    }
  }
  if (auto result = extension(source.name); !result.empty()) {
    return result;
  }
  return extension(source.path);
}

[[nodiscard]] std::optional<u32> trivialSongMidi(const SequenceProgram& program, const SequenceLayout& layout) {
  if (!program.sectionPlaylist || program.sectionPlaylist->commands.size() != 2 ||
      program.behavior.initialMasterLevel.has_value() ||
      program.behavior.initialTempoMicrosecondsPerQuarter != 500000 || program.behavior.initialChannelPan.has_value() ||
      (program.behavior.initialStereoBalance && (program.behavior.initialStereoBalance->leftGain != 1.0 ||
                                                 program.behavior.initialStereoBalance->rightGain != 1.0))) {
    return std::nullopt;
  }
  const auto& play = program.sectionPlaylist->commands[0];
  const auto& end = program.sectionPlaylist->commands[1];
  if (play.kind != PlaylistCommandKind::PlaySection || end.kind != PlaylistCommandKind::End ||
      play.fallthrough.value != end.address.value) {
    return std::nullopt;
  }
  const auto block = std::ranges::find(layout.midiBlocks, play.target.value,
                                       [](const MidiBlockLayout& candidate) { return candidate.dataOffset; });
  return block == layout.midiBlocks.end()
             ? std::nullopt
             : std::optional<u32>{static_cast<u32>(std::distance(layout.midiBlocks.begin(), block))};
}

[[nodiscard]] std::vector<std::pair<u32, SoundBankData>> findBanks(ByteReader reader) {
  std::vector<std::pair<u32, SoundBankData>> banks;
  constexpr std::string_view signature = "IECSsreV";
  for (u64 offset = 0; offset + 0x40 <= reader.size(); ++offset) {
    if (offset > std::numeric_limits<u32>::max() ||
        !std::ranges::equal(signature, reader.slice(offset, signature.size()))) {
      continue;
    }
    if (auto layout = readSoundBankLayout(reader, static_cast<u32>(offset))) {
      const u32 size = reader.le32(offset + 0x1c);
      banks.emplace_back(static_cast<u32>(offset), std::move(*layout));
      offset += size - 1;
    }
  }
  return banks;
}

void publishMidiBlock(ScanResultBuilder& result, const MidiBlockLayout& block, u32 initialVolume = 128) {
  const ByteReader reader = result.reader();
  auto sequence = result.sequence(fmt::format("{} MIDI {}", result.sourceDisplayName(), block.index),
                                  reader.range(block.offset, block.dataEnd - block.offset));
  auto program = parseMidiSequence(reader, sequence.id(), block, &result.sourceMap(), &result.diagnostics());
  if (initialVolume != 128) {
    program.behavior.initialMasterLevel = std::min(initialVolume, 128u) / 128.0;
  }
  sequence.data(SequenceData{}).program(std::move(program));
}

[[nodiscard]] u32 songEntryEnd(const SparseChunkLayout& songs, u32 offset) {
  u32 end = songs.offset + songs.size;
  for (const auto candidate : songs.entries) {
    if (candidate && *candidate > offset) {
      end = std::min(end, *candidate);
    }
  }
  return end;
}

void publishSongs(ScanResultBuilder& result, const SequenceLayout& layout, const SparseChunkLayout& songs) {
  struct PlayableSong {
    u32 index;
    u32 offset;
    u32 end;
  };

  std::vector<bool> publishMidi(layout.midiBlocks.size(), false);
  std::vector<PlayableSong> playableSongs;
  bool foundPlayableSong = false;
  const ByteReader reader = result.reader();
  for (u32 song = 0; song < songs.entries.size(); ++song) {
    const auto entry = songs.entries[song];
    if (!entry) {
      continue;
    }
    const u32 end = songEntryEnd(songs, *entry);
    if (auto program = parseSongSequence(reader, AssetId{}, layout, *entry, end, nullptr, nullptr)) {
      foundPlayableSong = true;
      if (const auto midi = trivialSongMidi(*program, layout)) {
        publishMidi[*midi] = true;
      } else {
        playableSongs.push_back(PlayableSong{.index = song, .offset = *entry, .end = end});
      }
      continue;
    }

    const auto name = fmt::format("{} Song {} table", result.sourceDisplayName(), song);
    const auto range = reader.range(*entry, end - *entry);
    auto misc = result.misc(name, range);
    misc.payload({});
    result.sourceMap().table(name, range).kind("sony-ps2-song-table").owner(ObjectRefs::misc(misc.id()));
  }
  if (!foundPlayableSong) {
    std::fill(publishMidi.begin(), publishMidi.end(), true);
  }
  for (u32 midi = 0; midi < publishMidi.size(); ++midi) {
    if (publishMidi[midi]) {
      publishMidiBlock(result, layout.midiBlocks[midi]);
    }
  }
  for (const auto& song : playableSongs) {
    auto sequence = result.sequence(fmt::format("{} Song {}", result.sourceDisplayName(), song.index),
                                    reader.range(song.offset, song.end - song.offset));
    auto program = parseSongSequence(reader, sequence.id(), layout, song.offset, song.end, &result.sourceMap(),
                                     &result.diagnostics());
    sequence.data(SequenceData{}).program(std::move(*program));
  }
}

void publishSeSequences(ScanResultBuilder& result, const SequenceLayout& layout) {
  const ByteReader reader = result.reader();
  if (layout.seSequences) {
    for (const auto& block : layout.seSequenceBlocks) {
      if (!parseSeSequence(reader, AssetId{}, block, nullptr, nullptr)) {
        continue;
      }
      auto sequence =
          result.sequence(fmt::format("{} SeSeq {}:{}", result.sourceDisplayName(), block.set, block.sequence),
                          reader.range(block.offset, block.dataEnd - block.offset));
      auto program = parseSeSequence(reader, sequence.id(), block, &result.sourceMap(), &result.diagnostics());
      sequence.data(SequenceData{}).program(std::move(*program));
    }
    result.warning("SonyPS2 SeSeq notes, jumps, and per-voice pitch slides are playable; volume, pan, and LFO "
                   "automation remain source-only",
                   reader.range(layout.seSequences->offset, layout.seSequences->size));
  }
  if (layout.seSongs) {
    const auto name = fmt::format("{} SeSong tables", result.sourceDisplayName());
    const auto range = reader.range(layout.seSongs->offset, layout.seSongs->size);
    auto misc = result.misc(name, range);
    misc.payload({});
    result.sourceMap().table(name, range).kind("sony-ps2-sesong-tables").owner(ObjectRefs::misc(misc.id()));
    result.warning("SonyPS2 SeSong playback tables remain source-only", range);
  }
}

void publishSequenceLayout(ScanResultBuilder& result, const SequenceLayout& layout, const Psf2Selection* selection) {
  if (selection != nullptr) {
    const auto midi = std::ranges::find(layout.midiBlocks, selection->midi, &MidiBlockLayout::index);
    if (midi != layout.midiBlocks.end()) {
      publishMidiBlock(result, *midi, selection->volume);
    } else {
      result.warning(fmt::format("PSF2 sq.irx selects missing SonyPS2 MIDI block {}", selection->midi),
                     result.reader().range(layout.offset, layout.length));
    }
    return;
  }

  if (!layout.songs) {
    for (const auto& midi : layout.midiBlocks) {
      publishMidiBlock(result, midi);
    }
  } else {
    publishSongs(result, layout, *layout.songs);
  }
  publishSeSequences(result, layout);
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const std::string extension = lowerExtension(input.source);
  const auto selection = psf2Selection(input.source);
  if (selection && ((extension == ".sq" && !selectedMember(input.source, selection->sequence)) ||
                    (extension == ".hd" && !selectedMember(input.source, selection->header)) ||
                    (extension == ".bd" && !selectedMember(input.source, selection->body)))) {
    return {};
  }
  const bool bodyCandidate = extension == ".bd";
  std::vector<SequenceLayout> sequences;
  std::vector<std::pair<u32, SoundBankData>> banks;
  if (extension == ".sq") {
    if (auto layout = readSequenceLayout(input.reader, 0)) {
      sequences.push_back(std::move(*layout));
    }
  } else if (extension == ".hd") {
    if (auto layout = readSoundBankLayout(input.reader, 0)) {
      banks.emplace_back(0, std::move(*layout));
    }
  } else if (!bodyCandidate) {
    sequences = findSequenceLayouts(input.reader);
    banks = findBanks(input.reader);
  }
  if (sequences.empty() && banks.empty() && !bodyCandidate) {
    return {};
  }
  ScanResultBuilder result(input, std::string(kFormatName), std::string(kCollectionResolver));
  if (bodyCandidate && !addSampleBody(result) && sequences.empty() && banks.empty()) {
    return {};
  }
  for (auto& [offset, bank] : banks) {
    if (selection && extension == ".hd") {
      bank.reverbType = selection->reverbType;
      bank.reverbDepth = selection->reverbDepth;
    }
    addSoundBank(result, offset, std::move(bank));
  }
  const Psf2Selection* selectedSq = selection && extension == ".sq" ? &*selection : nullptr;
  for (const auto& layout : sequences) {
    publishSequenceLayout(result, layout, selectedSq);
  }
  return result.finish();
}

}  // namespace

FormatModule module() {
  // SCE's separate software-synth (SS) format adds oscillator and filter
  // graphs that do not fit value-core's sampled-region model. Do not claim SS
  // files here until those synthesis primitives can be represented faithfully.
  return FormatModule{
      .name = std::string(kFormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .scan = scan,
      .resolveCollections = resolveCollections,
      .bindCollection = bindCollection,
  };
}

}  // namespace vgmtrans::formats::sony_ps2
