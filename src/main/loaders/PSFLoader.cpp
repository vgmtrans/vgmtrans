/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PSFLoader.h"

#include <unordered_map>
#include <vector>
#include <filesystem>
#include <optional>
#include <spdlog/fmt/fmt.h>

#include "LogManager.h"
#include "PSFFile.h"
#include "LoaderManager.h"

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
  uint32_t start = 0;
  uint32_t end = 0;
  std::vector<u8> data;
};

constexpr int MAX_RECURSION = 10;

void overlay(Image &img, uint32_t addr, const u8 *data, size_t size) {
  if (!size)
    return;
  if (img.data.empty()) {
    img.start = addr;
    img.end = addr + static_cast<uint32_t>(size);
    img.data.assign(data, data + size);
    return;
  }
  uint32_t new_start = std::min(img.start, addr);
  uint32_t new_end = std::max(img.end, addr + static_cast<uint32_t>(size));
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

void load_with_libs(const PSFFile &psf, const std::filesystem::path &basepath, Image &img,
                    int depth = 0) {
  if (depth >= MAX_RECURSION)
    return;

  auto findLib = [&](const std::string &key) {
    auto it = psf.tags().find(key);
    return it != psf.tags().end() ? std::optional<std::string>(it->second)
                                  : std::nullopt;
  };

  auto lib = findLib("_lib");
  if (!lib)
    lib = findLib("_Lib");
  if (lib) {
    auto newpath = basepath;
    newpath /= *lib;
    DiskFile libfile(newpath.string());
    PSFFile libpsf(libfile);
    load_with_libs(libpsf, newpath.parent_path(), img, depth + 1);
  }

  if (!psf.exe().empty()) {
    uint32_t addr = psf.version() == PSF1_VERSION ? psf.getExe<uint32_t>(0x18)
                                                 : psf.getExe<uint32_t>(0);
    size_t off = data_offset.at(psf.version());
    overlay(img, addr,
            reinterpret_cast<const u8 *>(psf.exe().data()) + off,
            psf.exe().size() - off);
  }

  for (int i = 2;; ++i) {
    auto it = psf.tags().find(fmt::format("_lib{}", i));
    if (it == psf.tags().end())
      break;
    auto newpath = basepath;
    newpath /= it->second;
    DiskFile libfile(newpath.string());
    PSFFile libpsf(libfile);
    load_with_libs(libpsf, newpath.parent_path(), img, depth + 1);
  }
}

} // namespace

void PSFLoader::apply(const RawFile *file) {
  if (file->size() <= 16)
    return;
  if (std::equal(file->begin(), file->begin() + 3, "PSF")) {
    uint8_t version = file->get<u8>(3);
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
      auto virt = new VirtFile(img.data.data(), img.data.size(), file->name(),
                               file->path().string(), tag);
      enqueue(virt);
    }
  } catch (std::exception &e) {
    L_ERROR(e.what());
  }
}
