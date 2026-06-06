/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSFLoader.h"

#include "base/Types.h"
#include "components/VGMMetadataHint.h"
#include "LoaderManager.h"
#include "LogManager.h"
#include "PSFFile.h"
#include "util/Path.h"
#include "util/Text.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/fmt/fmt.h>

namespace vgmtrans::loaders {
LoaderRegistration<PSFLoader> psf{"PSF"};
}

constexpr int PSF1_VERSION = 0x1;
constexpr int SSF_VERSION = 0x11;
constexpr int GSF_VERSION = 0x22;
constexpr int SNSF_VERSION = 0x23;
constexpr int NDS2SF_VERSION = 0x24;
constexpr int NCSF_VERSION = 0x25;
const std::unordered_map<int, size_t> data_offset = {{PSF1_VERSION, 0x800},
                                                     {SSF_VERSION, 0x04},
                                                     {GSF_VERSION, 0x0C},
                                                     {SNSF_VERSION, 0x08},
                                                     {NDS2SF_VERSION, 0x08},
                                                     {NCSF_VERSION, 0x0}};

namespace {

struct Image {
  u32 start = 0;
  u32 end = 0;
  std::vector<u8> data;
};

constexpr int MAX_RECURSION = 10;
constexpr u32 GBA_ROM_BASE = 0x08000000;
constexpr auto MP2K_FORMAT_NAME = "MP2k";
constexpr auto NDS_FORMAT_NAME = "NDS";

void overlay(Image &img, u32 addr, const u8 *data, size_t size) {
  if (!size)
    return;
  if (img.data.empty()) {
    img.start = addr;
    img.end = addr + static_cast<u32>(size);
    img.data.assign(data, data + size);
    return;
  }
  u32 new_start = std::min(img.start, addr);
  u32 new_end = std::max(img.end, addr + static_cast<u32>(size));
  if (new_start != img.start) {
    img.data.insert(img.data.begin(), img.start - new_start, 0);
    img.start = new_start;
  }
  if (new_end > img.end) {
    img.data.resize(new_end - img.start, 0);
    img.end = new_end;
  }
  std::copy(data, data + size, img.data.begin() + (addr - img.start));
}

std::optional<std::string> findLibTag(const PSFFile& psf) {
  if (auto it = psf.tags().find("_lib"); it != psf.tags().end()) {
    return it->second;
  }
  if (auto it = psf.tags().find("_Lib"); it != psf.tags().end()) {
    return it->second;
  }
  return std::nullopt;
}

std::filesystem::path resolveLibPath(const std::filesystem::path& basepath,
                                     const std::string& libname) {
  return std::filesystem::absolute(basepath / libname).lexically_normal();
}

bool isGsfMetadataCandidate(const std::filesystem::path& path) {
  const auto ext = toLower(pathToUtf8String(path.extension()));
  return ext == ".gsf" || ext == ".minigsf";
}

bool isNdsMetadataCandidate(const std::filesystem::path& path) {
  const auto ext = toLower(pathToUtf8String(path.extension()));
  return ext == ".2sf" || ext == ".mini2sf" || ext == ".ncsf" || ext == ".minincsf";
}

bool isNdsPsfVersion(int version) {
  return version == NDS2SF_VERSION || version == NCSF_VERSION;
}

const char* ndsSourceFormat(int version) {
  return version == NCSF_VERSION ? "NCSF" : "2SF";
}

std::optional<u32> selectedSongIndexFromGsf(const PSFFile& psf) {
  if (psf.version() != GSF_VERSION) {
    return std::nullopt;
  }

  const auto& exe = psf.exe();
  const auto payloadOffset = data_offset.at(GSF_VERSION);
  if (exe.size() <= payloadOffset) {
    return std::nullopt;
  }

  const u32 address = psf.getExe<u32>(0);
  if (address != GBA_ROM_BASE) {
    return std::nullopt;
  }

  const auto* payload = exe.data() + payloadOffset;
  const size_t payloadSize = exe.size() - payloadOffset;
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

std::vector<VGMMetadataHint> collectGsfMetadataHints(const RawFile* file, const PSFFile& psf) {
  std::vector<VGMMetadataHint> hints;
  if (psf.version() != GSF_VERSION) {
    return hints;
  }

  auto lib = findLibTag(psf);
  if (!lib) {
    return hints;
  }

  if (auto hint = hintFromGsf(psf, file->path())) {
    hints.emplace_back(std::move(*hint));
  }

  const auto basepath = file->path().parent_path();
  std::error_code ec;
  if (basepath.empty() || !std::filesystem::is_directory(basepath, ec)) {
    return hints;
  }

  const auto expectedLibPath = resolveLibPath(basepath, *lib);
  const auto openedPath = std::filesystem::absolute(file->path()).lexically_normal();

  for (const auto& entry : std::filesystem::directory_iterator(basepath, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec) || !isGsfMetadataCandidate(entry.path())) {
      continue;
    }

    const auto siblingPath = std::filesystem::absolute(entry.path()).lexically_normal();
    if (siblingPath == openedPath) {
      continue;
    }

    try {
      DiskFile siblingFile(entry.path());
      PSFFile siblingPsf(siblingFile);
      if (siblingPsf.version() != GSF_VERSION) {
        continue;
      }

      auto siblingLib = findLibTag(siblingPsf);
      if (!siblingLib ||
          resolveLibPath(entry.path().parent_path(), *siblingLib) != expectedLibPath) {
        continue;
      }

      if (auto hint = hintFromGsf(siblingPsf, entry.path())) {
        hints.emplace_back(std::move(*hint));
      }
    } catch (const std::exception& e) {
      L_DEBUG("Ignoring GSF metadata candidate '{}': {}", pathToUtf8String(entry.path()), e.what());
    }
  }

  return hints;
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

std::vector<VGMMetadataHint> collectNdsMetadataHints(const RawFile* file, const PSFFile& psf) {
  std::vector<VGMMetadataHint> hints;
  if (!isNdsPsfVersion(psf.version())) {
    return hints;
  }

  auto lib = findLibTag(psf);
  if (!lib) {
    return hints;
  }

  if (auto hint = hintFromNdsPsf(psf, file->path())) {
    hints.emplace_back(std::move(*hint));
  }

  const auto basepath = file->path().parent_path();
  std::error_code ec;
  if (basepath.empty() || !std::filesystem::is_directory(basepath, ec)) {
    return hints;
  }

  const auto expectedLibPath = resolveLibPath(basepath, *lib);
  const auto openedPath = std::filesystem::absolute(file->path()).lexically_normal();

  for (const auto& entry : std::filesystem::directory_iterator(basepath, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec) || !isNdsMetadataCandidate(entry.path())) {
      continue;
    }

    const auto siblingPath = std::filesystem::absolute(entry.path()).lexically_normal();
    if (siblingPath == openedPath) {
      continue;
    }

    try {
      DiskFile siblingFile(entry.path());
      PSFFile siblingPsf(siblingFile);
      if (!isNdsPsfVersion(siblingPsf.version())) {
        continue;
      }

      auto siblingLib = findLibTag(siblingPsf);
      if (!siblingLib ||
          resolveLibPath(entry.path().parent_path(), *siblingLib) != expectedLibPath) {
        continue;
      }

      if (auto hint = hintFromNdsPsf(siblingPsf, entry.path())) {
        hints.emplace_back(std::move(*hint));
      }
    } catch (const std::exception& e) {
      L_DEBUG("Ignoring NDS PSF metadata candidate '{}': {}",
              pathToUtf8String(entry.path()), e.what());
    }
  }

  return hints;
}

void load_with_libs(const PSFFile &psf, const std::filesystem::path &basepath, Image &img,
                    int depth = 0) {
  if (depth >= MAX_RECURSION)
    return;

  auto findLib = [&](const std::string &key) {
    auto it = psf.tags().find(key);
    return it != psf.tags().end() ? std::optional<std::string>(it->second)
                                  : std::nullopt;
  };

  auto tryOpenLib = [&](const std::string& libname) {
    auto newpath = basepath / libname;
    auto doLoad = [&](const std::filesystem::path& p) {
      DiskFile libfile(p);
      PSFFile libpsf(libfile);
      load_with_libs(libpsf, p.parent_path(), img, depth + 1);
    };
    try {
      doLoad(newpath);
    } catch (const std::exception& e) {
      L_ERROR("Cannot open PSF library file '{}': {}. Asking user for folder.", libname, e.what());
      auto chosen = pRoot->UI_openFolder(
          basepath,
          fmt::format("PSF library file '{}' could not be opened. Select the folder containing it.", libname));
      if (chosen.empty()) {
        throw std::runtime_error(fmt::format("PSF library file '{}' not accessible.", libname));
      }
      doLoad(chosen / libname);
    }
  };

  auto lib = findLib("_lib");
  if (!lib)
    lib = findLib("_Lib");

  if (lib)
    tryOpenLib(*lib);

  if (!psf.exe().empty()) {
    u32 addr = psf.version() == PSF1_VERSION ? psf.getExe<u32>(0x18)
                                                 : psf.getExe<u32>(0);
    size_t off = data_offset.at(psf.version());
    overlay(img, addr,
            reinterpret_cast<const u8 *>(psf.exe().data()) + off,
            psf.exe().size() - off);
  }

  for (int i = 2;; ++i) {
    auto it = psf.tags().find(fmt::format("_lib{}", i));
    if (it == psf.tags().end())
      break;
    tryOpenLib(it->second);
  }
}

} // namespace

void PSFLoader::apply(const RawFile *file) {
  if (file->size() <= 16)
    return;
  if (std::equal(file->begin(), file->begin() + 3, "PSF")) {
    u8 version = file->get<u8>(3);
    if (data_offset.contains(version)) {
      psf_read_exe(file);
    }
  }
}

void PSFLoader::psf_read_exe(const RawFile *file) {
  try {
    PSFFile psf(*file);
    Image img;
    load_with_libs(psf, file->path().parent_path(), img);
    if (!img.data.empty()) {
      auto tag = PSFFile::tagFromPSFFile(psf);
      std::shared_ptr<const VGMMetadataHintProvider> metadataProvider;
      auto hints = collectGsfMetadataHints(file, psf);
      auto ndsHints = collectNdsMetadataHints(file, psf);
      hints.insert(hints.end(), std::make_move_iterator(ndsHints.begin()),
                   std::make_move_iterator(ndsHints.end()));
      if (!hints.empty()) {
        metadataProvider = std::make_shared<IndexedMetadataHintProvider>(std::move(hints));
      }

      enqueue(std::make_unique<VirtFile>(img.data.data(), img.data.size(), file->name(),
                                         file->path(), tag, std::move(metadataProvider)));
    }
  } catch (std::exception &e) {
    L_ERROR(e.what());
  }
}
