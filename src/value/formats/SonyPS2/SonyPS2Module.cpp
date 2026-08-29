/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS2/SonyPS2.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <string>

namespace vgmtrans::formats::sony_ps2 {

using namespace core;

namespace {

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

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const bool bodyCandidate = lowerExtension(input.source) == ".bd";
  const auto sequences = findSequenceLayouts(input.reader);
  auto banks = findBanks(input.reader);
  if (sequences.empty() && banks.empty() && !bodyCandidate) {
    return {};
  }
  ScanResultBuilder result(input, std::string(kFormatName), std::string(kCollectionResolver));
  if (bodyCandidate && !addSampleBody(result) && sequences.empty() && banks.empty()) {
    return {};
  }
  for (auto& [offset, bank] : banks) {
    addSoundBank(result, offset, std::move(bank));
  }
  for (u32 fileIndex = 0; fileIndex < sequences.size(); ++fileIndex) {
    const auto& layout = sequences[fileIndex];
    struct SongEntry {
      u32 index;
      u32 offset;
      u32 end;
    };
    std::vector<bool> publishMidi(layout.midiBlocks.size(), !layout.songs.has_value());
    std::vector<SongEntry> publishSongs;
    bool foundPlayableSong = false;
    if (layout.songs) {
      for (u32 song = 0; song < layout.songs->entries.size(); ++song) {
        const auto entry = layout.songs->entries[song];
        if (!entry) {
          continue;
        }
        u32 entryEnd = layout.songs->offset + layout.songs->size;
        for (const auto candidate : layout.songs->entries) {
          if (candidate && *candidate > *entry) {
            entryEnd = std::min(entryEnd, *candidate);
          }
        }
        if (auto program = parseSongSequence(input.reader, AssetId{}, layout, *entry, entryEnd, nullptr, nullptr)) {
          foundPlayableSong = true;
          if (const auto midi = trivialSongMidi(*program, layout)) {
            publishMidi[*midi] = true;
          } else {
            publishSongs.push_back(SongEntry{.index = song, .offset = *entry, .end = entryEnd});
          }
        } else {
          auto misc = result.misc(fmt::format("{} Song {} table", result.sourceDisplayName(), song),
                                  input.reader.range(*entry, entryEnd - *entry));
          misc.payload({});
        }
      }
    }
    if (!foundPlayableSong) {
      std::fill(publishMidi.begin(), publishMidi.end(), true);
    }
    for (u32 midi = 0; midi < layout.midiBlocks.size(); ++midi) {
      if (!publishMidi[midi]) {
        continue;
      }
      const auto& block = layout.midiBlocks[midi];
      auto sequence = result.sequence(fmt::format("{} MIDI {}", result.sourceDisplayName(), midi),
                                      input.reader.range(block.offset, block.dataEnd - block.offset));
      sequence.data(SequenceData{})
          .program(parseMidiSequence(input.reader, sequence.id(), block, &result.sourceMap(), &result.diagnostics()));
    }
    for (const auto& song : publishSongs) {
      auto sequence = result.sequence(fmt::format("{} Song {}", result.sourceDisplayName(), song.index),
                                      input.reader.range(song.offset, song.end - song.offset));
      auto program = parseSongSequence(input.reader, sequence.id(), layout, song.offset, song.end, &result.sourceMap(),
                                       &result.diagnostics());
      sequence.data(SequenceData{}).program(std::move(*program));
    }
    if (layout.seSequences) {
      for (const auto& block : layout.seSequenceBlocks) {
        if (!parseSeSequence(input.reader, AssetId{}, block, nullptr, nullptr)) {
          continue;
        }
        auto sequence =
            result.sequence(fmt::format("{} SeSeq {}:{}", result.sourceDisplayName(), block.set, block.sequence),
                            input.reader.range(block.offset, block.dataEnd - block.offset));
        auto program = parseSeSequence(input.reader, sequence.id(), block, &result.sourceMap(), &result.diagnostics());
        sequence.data(SequenceData{}).program(std::move(*program));
      }
      result.warning("SonyPS2 SeSeq notes and jumps are playable; per-active-voice fades, pitch, pan, and LFO "
                     "automation remain source-only",
                     input.reader.range(layout.seSequences->offset, layout.seSequences->size));
    }
    if (layout.seSongs) {
      auto misc = result.misc(fmt::format("{} SeSong tables", result.sourceDisplayName()),
                              input.reader.range(layout.seSongs->offset, layout.seSongs->size));
      misc.payload({});
      result.warning("SonyPS2 SeSong playback tables remain source-only",
                     input.reader.range(layout.seSongs->offset, layout.seSongs->size));
    }
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
