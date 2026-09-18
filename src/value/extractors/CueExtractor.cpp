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

std::vector<u8> readTrack(std::ifstream& file, u64 start, u64 end, size_t sectorSize) {
  std::vector<u8> bytes(static_cast<size_t>(end - start));
  file.seekg(static_cast<std::streamoff>(start));
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
    throw std::runtime_error("could not read track data");
  }
  const size_t header = sectorSize == 2352 ? 16 : 0;
  size_t written = 0;
  for (size_t offset = 0; offset < bytes.size(); offset += sectorSize) {
    const auto* sector = bytes.data() + offset;
    if (header != 0 && sector[15] != 2) {
      throw std::runtime_error("expected a MODE2 sector");
    }
    // XA submode bit 5 selects Form 2. Both forms have an eight-byte
    // subheader; only user bytes are retained, excluding EDC/ECC.
    const size_t size = (sector[header + 2] & 0x20) != 0 ? 2324 : 2048;
    std::memmove(bytes.data() + written, sector + header + 8, size);
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
    if (!std::ranges::any_of(entry.tracks, [](const Track& track) { return track.mode.starts_with("MODE2/"); })) {
      continue;
    }
    try {
      if (entry.type != "BINARY") {
        throw std::runtime_error("MODE2 tracks require a BINARY file");
      }
      // Require one physical sector size per FILE, including audio tracks.
      const size_t sectorSize = entry.tracks.front().mode == "MODE2/2336" ? 2336 : 2352;
      for (size_t i = 0; i < entry.tracks.size(); ++i) {
        const auto& track = entry.tracks[i];
        if ((sectorSize == 2336 && track.mode != "MODE2/2336") ||
            (sectorSize == 2352 && track.mode != "MODE2/2352" && track.mode != "MODE1/2352" && track.mode != "AUDIO")) {
          throw std::runtime_error("unsupported or mixed sector sizes");
        }
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
      if (fileSize % sectorSize != 0 || *entry.tracks.back().start >= fileSize / sectorSize) {
        throw std::runtime_error("truncated track file or index past end of file");
      }
      for (size_t i = 0; i < entry.tracks.size(); ++i) {
        const auto& track = entry.tracks[i];
        if (!track.mode.starts_with("MODE2/")) {
          continue;
        }
        const u64 end = i + 1 == entry.tracks.size()
                            ? fileSize
                            : entry.tracks[i + 1].pregap.value_or(*entry.tracks[i + 1].start) * sectorSize;
        result.sources.push_back(ExtractedSource{
            .file = SourceFile{
                .name = entry.name + " (Track " + std::to_string(track.number) + ")",
                .path = input.source.path,
            },
            .bytes = readTrack(file, *track.start * sectorSize, end, sectorSize),
        });
      }
    } catch (const std::exception& ex) {
      result.diagnostics.push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "CUE " + entry.name + ": " + ex.what(),
      });
    }
  }
  if (result.sources.empty() && result.diagnostics.empty()) {
    result.diagnostics.push_back(Diagnostic{.severity = Severity::Warning, .message = "CUE contains no MODE2 tracks"});
  }
  return result;
}

}  // namespace

SourceExtractor cueExtractor() {
  return SourceExtractor{.name = "Cue", .acceptedFormats = {source_formats::kCue}, .extract = extractCue};
}

}  // namespace vgmtrans::formats::cue
