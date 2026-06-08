/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSFMetadataHints.h"

#include "base/Types.h"
#include "io/RawFile.h"
#include "LogManager.h"
#include "PSFFile.h"
#include "util/Path.h"
#include "util/Text.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr u32 GBA_ROM_BASE = 0x08000000;
constexpr auto MP2K_FORMAT_NAME = "MP2k";
constexpr auto NDS_FORMAT_NAME = "NDS";

constexpr std::array<std::string_view, 2> GSF_METADATA_EXTENSIONS = {".gsf", ".minigsf"};
constexpr std::array<std::string_view, 1> GSF_LIBRARY_EXTENSIONS = {".gsflib"};
constexpr std::array<std::string_view, 4> NDS_METADATA_EXTENSIONS = {
    ".2sf", ".mini2sf", ".ncsf", ".minincsf"};
constexpr std::array<std::string_view, 2> NDS_LIBRARY_EXTENSIONS = {".2sflib", ".ncsflib"};

struct Rules {
  std::string_view debugName;
  std::span<const std::string_view> metadataExtensions;
  std::span<const std::string_view> libraryExtensions;
  bool (*supportsVersion)(u8 version);
  std::optional<VGMMetadataHint> (*makeHint)(const PSFFile& psf,
                                             const std::filesystem::path& sourcePath);
};

std::filesystem::path normalizePath(const std::filesystem::path& path) {
  return std::filesystem::absolute(path).lexically_normal();
}

std::optional<std::filesystem::path> containingDirectory(const RawFile& file) {
  if (file.path().empty()) {
    return std::nullopt;
  }
  return normalizePath(file.path()).parent_path();
}

std::filesystem::path resolveLibPath(const std::filesystem::path& basepath,
                                     const std::string& libname) {
  return normalizePath(basepath / libname);
}

bool hasExtension(const std::filesystem::path& path, std::span<const std::string_view> extensions) {
  const auto ext = toLower(pathToUtf8String(path.extension()));
  return std::ranges::any_of(extensions, [&](std::string_view candidate) {
    return ext == candidate;
  });
}

bool isGsfVersion(u8 version) {
  return version == vgmtrans::psf::GSF_VERSION;
}

bool isNdsPsfVersion(u8 version) {
  return version == vgmtrans::psf::NDS2SF_VERSION || version == vgmtrans::psf::NCSF_VERSION;
}

const char* ndsSourceFormat(u8 version) {
  return version == vgmtrans::psf::NCSF_VERSION ? "NCSF" : "2SF";
}

std::optional<u32> selectedSongIndexFromGsf(const PSFFile& psf) {
  if (!isGsfVersion(psf.version())) {
    return std::nullopt;
  }

  const auto& exe = psf.exe();
  if (exe.size() <= vgmtrans::psf::GSF_DATA_OFFSET) {
    return std::nullopt;
  }

  const u32 address = psf.getExe<u32>(0);
  if (address != GBA_ROM_BASE) {
    return std::nullopt;
  }

  const auto* payload = exe.data() + vgmtrans::psf::GSF_DATA_OFFSET;
  const size_t payloadSize = exe.size() - vgmtrans::psf::GSF_DATA_OFFSET;
  if (payloadSize == sizeof(u32)) {
    return static_cast<u32>(payload[0]) |
           (static_cast<u32>(payload[1]) << 8) |
           (static_cast<u32>(payload[2]) << 16) |
           (static_cast<u32>(payload[3]) << 24);
  }
  if (payloadSize == sizeof(u16)) {
    return static_cast<u32>(payload[0]) | (static_cast<u32>(payload[1]) << 8);
  }
  if (payloadSize == sizeof(u8)) {
    return static_cast<u32>(payload[0]);
  }

  return std::nullopt;
}

std::optional<VGMMetadataHint> hintFromGsf(const PSFFile& psf,
                                           const std::filesystem::path& sourcePath) {
  auto songIndex = selectedSongIndexFromGsf(psf);
  if (!songIndex) {
    return std::nullopt;
  }

  return VGMMetadataHint{
      .target = {
          .targetFormat = MP2K_FORMAT_NAME,
          .songIndex = songIndex,
      },
      .sourceFormat = "GSF",
      .sourcePath = sourcePath,
      .tag = PSFFile::tagFromPSFFile(psf),
  };
}

std::optional<VGMMetadataHint> hintFromNdsPsf(const PSFFile& psf,
                                             const std::filesystem::path& sourcePath) {
  if (!isNdsPsfVersion(psf.version())) {
    return std::nullopt;
  }

  const auto origFilename = psf.tags().find("origFilename");
  if (origFilename == psf.tags().end() || origFilename->second.empty()) {
    return std::nullopt;
  }

  return VGMMetadataHint{
      .target = {
          .targetFormat = NDS_FORMAT_NAME,
          .lookupKey = origFilename->second,
      },
      .sourceFormat = ndsSourceFormat(psf.version()),
      .sourcePath = sourcePath,
      .tag = PSFFile::tagFromPSFFile(psf),
  };
}

void appendHints(std::vector<VGMMetadataHint>& hints, std::vector<VGMMetadataHint>&& moreHints) {
  hints.insert(hints.end(), std::make_move_iterator(moreHints.begin()),
               std::make_move_iterator(moreHints.end()));
}

std::vector<std::filesystem::path> sortedCandidatePaths(
    const std::filesystem::path& basepath, const Rules& rules) {
  std::vector<std::filesystem::path> paths;
  std::error_code ec;

  std::filesystem::directory_iterator it(basepath, ec);
  std::filesystem::directory_iterator end;
  while (!ec && it != end) {
    const auto& entry = *it;
    std::error_code entryEc;
    if (entry.is_regular_file(entryEc) && !entryEc &&
        hasExtension(entry.path(), rules.metadataExtensions)) {
      paths.emplace_back(entry.path());
    }
    it.increment(ec);
  }

  std::ranges::sort(paths);
  return paths;
}

std::vector<VGMMetadataHint> collectSiblingHintsReferencingLibPath(
    const RawFile& openedFile,
    const Rules& rules,
    const std::filesystem::path& targetLibPath) {
  std::vector<VGMMetadataHint> hints;
  const auto basepath = containingDirectory(openedFile);
  if (!basepath) {
    return hints;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(*basepath, ec)) {
    return hints;
  }

  const auto openedPath = normalizePath(openedFile.path());
  for (const auto& candidatePath : sortedCandidatePaths(*basepath, rules)) {
    const auto candidateNormalizedPath = normalizePath(candidatePath);
    if (candidateNormalizedPath == openedPath) {
      continue;
    }

    try {
      DiskFile siblingFile(candidatePath);
      PSFFile siblingPsf(siblingFile);
      if (!rules.supportsVersion(siblingPsf.version())) {
        continue;
      }

      auto siblingLib = siblingPsf.primaryLibName();
      if (!siblingLib ||
          resolveLibPath(candidatePath.parent_path(), *siblingLib) != targetLibPath) {
        continue;
      }

      if (auto hint = rules.makeHint(siblingPsf, candidatePath)) {
        hints.emplace_back(std::move(*hint));
      }
    } catch (const std::exception& e) {
      L_DEBUG("Ignoring {} metadata candidate '{}': {}",
              rules.debugName, pathToUtf8String(candidatePath), e.what());
    }
  }

  return hints;
}

std::vector<VGMMetadataHint> collectOwnHint(
    const PSFFile& psf, const std::filesystem::path& sourcePath, const Rules& rules) {
  std::vector<VGMMetadataHint> hints;
  if (auto hint = rules.makeHint(psf, sourcePath)) {
    hints.emplace_back(std::move(*hint));
  }
  return hints;
}

std::vector<VGMMetadataHint> collectSiblingHintsForSameLib(
    const RawFile& openedFile, const PSFFile& openedPsf, const Rules& rules) {
  const auto basepath = containingDirectory(openedFile);
  const auto lib = openedPsf.primaryLibName();
  if (!basepath || !lib) {
    return {};
  }

  return collectSiblingHintsReferencingLibPath(openedFile, rules, resolveLibPath(*basepath, *lib));
}

std::vector<VGMMetadataHint> collectSiblingHintsReferencingThisLib(const RawFile& openedFile, const Rules& rules) {
  if (openedFile.path().empty() || !hasExtension(openedFile.path(), rules.libraryExtensions)) {
    return {};
  }

  return collectSiblingHintsReferencingLibPath(openedFile, rules, normalizePath(openedFile.path()));
}

std::vector<VGMMetadataHint> collectForRules(const RawFile& file, const PSFFile& psf, const Rules& rules) {
  std::vector<VGMMetadataHint> hints;
  if (!rules.supportsVersion(psf.version())) {
    return hints;
  }

  appendHints(hints, collectOwnHint(psf, file.path(), rules));

  if (psf.primaryLibName()) {
    appendHints(hints, collectSiblingHintsForSameLib(file, psf, rules));
  } else {
    appendHints(hints, collectSiblingHintsReferencingThisLib(file, rules));
  }

  return hints;
}

const std::array<Rules, 2>& metadataRules() {
  static constexpr std::array<Rules, 2> rules = {{
      {
          .debugName = "GSF",
          .metadataExtensions = GSF_METADATA_EXTENSIONS,
          .libraryExtensions = GSF_LIBRARY_EXTENSIONS,
          .supportsVersion = isGsfVersion,
          .makeHint = hintFromGsf,
      },
      {
          .debugName = "NDS PSF",
          .metadataExtensions = NDS_METADATA_EXTENSIONS,
          .libraryExtensions = NDS_LIBRARY_EXTENSIONS,
          .supportsVersion = isNdsPsfVersion,
          .makeHint = hintFromNdsPsf,
      },
  }};
  return rules;
}

}  // namespace

std::vector<VGMMetadataHint> vgmtrans::psf::collectMetadataHintsForOpenedFile(
    const RawFile& file, const PSFFile& psf) {
  std::vector<VGMMetadataHint> hints;
  for (const auto& rules : metadataRules()) {
    appendHints(hints, collectForRules(file, psf, rules));
  }
  return hints;
}
