/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/CueExtractor.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vgmtrans::formats::cue {

using namespace core;

namespace {

struct Track {
  int number = 0;
  std::string mode;
  std::optional<u64> pregap;
  std::optional<u64> start;
};

struct File {
  std::string name;
  std::string type;
  std::vector<Track> tracks;
};

struct TrackMode {
  std::string_view name;
  size_t sectorSize;
  size_t payloadOffset;
  size_t payloadSize;  // Zero means XA: the subheader selects Form 1 or Form 2.
};

const TrackMode& trackMode(std::string_view name) {
  static constexpr TrackMode modes[]{
      {"AUDIO", 2352, 0, 2352},
      {"MODE1/2048", 2048, 0, 2048},
      {"MODE1/2352", 2352, 16, 2048},
      {"MODE2/2048", 2048, 0, 2048},
      {"MODE2/2324", 2324, 0, 2324},
      {"MODE2/2336", 2336, 8, 0},
      {"MODE2/2352", 2352, 24, 0},
  };
  const auto found = std::ranges::find(modes, name, &TrackMode::name);
  if (found == std::end(modes)) {
    throw std::runtime_error("unsupported track mode: " + std::string(name));
  }
  return *found;
}

std::string upper(std::string text) {
  std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::toupper(c); });
  return text;
}

std::vector<File> parseCue(ByteReader reader) {
  const auto bytes = reader.slice(0, reader.size());
  std::string text(bytes.begin(), bytes.end());
  if (text.starts_with("\xef\xbb\xbf")) {
    text.erase(0, 3);
  }
  std::istringstream cue(text);
  std::vector<File> files;
  for (std::string line; std::getline(cue, line);) {
    std::istringstream fields(line);
    std::string command;
    fields >> command;
    command = upper(command);
    if (command == "FILE") {
      File file;
      // Backslashes in Windows paths are literal, not quote escapes.
      if (!(fields >> std::quoted(file.name, '"', '\0') >> file.type) || file.name.empty()) {
        throw std::runtime_error("invalid FILE directive");
      }
      std::ranges::replace(file.name, '\\', '/');
      file.type = upper(file.type);
      files.push_back(std::move(file));
    } else if (command == "TRACK") {
      Track track;
      if (files.empty() || !(fields >> track.number >> track.mode) || track.number < 1 || track.number > 99) {
        throw std::runtime_error("invalid TRACK directive");
      }
      track.mode = upper(track.mode);
      files.back().tracks.push_back(std::move(track));
    } else if (command == "INDEX") {
      int index, minutes, seconds, frames;
      char colon1, colon2;
      if (files.empty() || files.back().tracks.empty() ||
          !(fields >> index >> minutes >> colon1 >> seconds >> colon2 >> frames) ||
          index < 0 || index > 99 || minutes < 0 || minutes > 99 || seconds < 0 || seconds >= 60 ||
          frames < 0 || frames >= 75 || colon1 != ':' || colon2 != ':') {
        throw std::runtime_error("invalid INDEX directive");
      }
      auto& track = files.back().tracks.back();
      const u64 sector = (minutes * 60 + seconds) * 75 + frames;
      if (index <= 1) {
        auto& position = index == 0 ? track.pregap : track.start;
        if (position) {
          throw std::runtime_error("duplicate track index");
        }
        position = sector;
      }
    }
    // Metadata, PREGAP and POSTGAP do not occupy bytes in the track file.
  }
  return files;
}

std::vector<u8> readTrack(std::ifstream& file, u64 start, u64 end, const TrackMode& mode) {
  std::vector<u8> bytes(static_cast<size_t>(end - start));
  file.seekg(static_cast<std::streamoff>(start));
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    throw std::runtime_error("could not read track data");
  }
  if (mode.payloadOffset == 0) {
    return bytes;
  }
  size_t written = 0;
  for (size_t offset = 0; offset < bytes.size(); offset += mode.sectorSize) {
    const auto* sector = bytes.data() + offset;
    if (mode.sectorSize == 2352 && sector[15] != (mode.name.starts_with("MODE1/") ? 1 : 2)) {
      throw std::runtime_error("sector mode does not match " + std::string(mode.name));
    }
    size_t size = mode.payloadSize;
    if (size == 0) {
      // XA submode is six bytes before the payload; bit 5 selects Form 2.
      size = (sector[mode.payloadOffset - 6] & 0x20) != 0 ? 2324 : 2048;
    }
    std::memmove(bytes.data() + written, sector + mode.payloadOffset, size);
    written += size;
  }
  // Compact in place so large images need only one backing allocation.
  bytes.resize(written);
  return bytes;
}

ExtractionResult extractCue(const ExtractionInput& input) {
  if (input.source.derived() ||
      (input.source.knownFormat != source_formats::kCue &&
       upper(std::filesystem::path(input.source.name).extension().string()) != ".CUE")) {
    return {};
  }
  ExtractionResult result;
  for (const auto& entry : parseCue(input.reader)) {
    if (!std::ranges::any_of(entry.tracks, [](const Track& track) { return track.mode != "AUDIO"; })) {
      continue;
    }
    try {
      if (entry.type != "BINARY") {
        throw std::runtime_error("data tracks require a BINARY file");
      }
      for (size_t i = 0; i < entry.tracks.size(); ++i) {
        const auto& track = entry.tracks[i];
        trackMode(track.mode);
        if (!track.start || (track.pregap && *track.pregap > *track.start) ||
            (i != 0 && track.pregap.value_or(*track.start) <= *entry.tracks[i - 1].start)) {
          throw std::runtime_error("missing or unordered track indexes");
        }
      }
      std::ifstream file(input.source.path.parent_path() / entry.name, std::ios::binary | std::ios::ate);
      const auto length = file.tellg();
      if (length < 0) {
        throw std::runtime_error("could not open track file");
      }
      const auto fileSize = static_cast<u64>(length);
      const auto& first = entry.tracks.front();
      u64 offset = first.pregap.value_or(*first.start) * trackMode(first.mode).sectorSize;
      for (size_t i = 0; i < entry.tracks.size(); ++i) {
        const auto& track = entry.tracks[i];
        const auto& mode = trackMode(track.mode);
        const u64 firstSector = track.pregap.value_or(*track.start);
        const u64 start = offset + (*track.start - firstSector) * mode.sectorSize;
        // CUE indexes count sectors, even when adjacent tracks store different sector sizes.
        const u64 end = i + 1 == entry.tracks.size()
                            ? fileSize
                            : offset + (entry.tracks[i + 1].pregap.value_or(*entry.tracks[i + 1].start) - firstSector) *
                                           mode.sectorSize;
        if (end > fileSize || start >= end || (end - offset) % mode.sectorSize != 0) {
          throw std::runtime_error("truncated track file or index past end of file");
        }
        if (track.mode != "AUDIO") {
          result.sources.push_back(ExtractedSource{
              .file = SourceFile{
                  .name = entry.name + " (Track " + std::to_string(track.number) + ")",
                  .path = input.source.path,
              },
              .bytes = readTrack(file, start, end, mode),
          });
        }
        offset = end;
      }
    } catch (const std::exception& ex) {
      result.diagnostics.push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "CUE " + entry.name + ": " + ex.what(),
      });
    }
  }
  if (result.sources.empty() && result.diagnostics.empty()) {
    result.diagnostics.push_back(Diagnostic{.severity = Severity::Warning, .message = "CUE contains no data tracks"});
  }
  return result;
}

}  // namespace

SourceExtractor cueExtractor() {
  return SourceExtractor{.name = "Cue", .acceptedFormats = {source_formats::kCue}, .extract = extractCue};
}

}  // namespace vgmtrans::formats::cue
