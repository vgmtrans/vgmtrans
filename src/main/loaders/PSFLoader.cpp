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
#include "PSFMetadataHints.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/fmt/fmt.h>

namespace vgmtrans::loaders {
LoaderRegistration<PSFLoader> psf{"PSF"};
}

namespace {

struct Image {
  u32 start = 0;
  u32 end = 0;
  std::vector<u8> data;
};

constexpr int MAX_RECURSION = 10;

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

void load_with_libs(const PSFFile &psf, const std::filesystem::path &basepath, Image &img,
                    int depth = 0) {
  if (depth >= MAX_RECURSION)
    return;

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

  auto lib = psf.primaryLibName();
  if (lib)
    tryOpenLib(*lib);

  if (!psf.exe().empty()) {
    u32 addr = psf.version() == vgmtrans::psf::PSF1_VERSION ? psf.getExe<u32>(0x18)
                                                            : psf.getExe<u32>(0);
    auto off = vgmtrans::psf::dataOffsetForVersion(psf.version());
    if (!off) {
      throw std::runtime_error(fmt::format("Unsupported PSF version {:#x}",
                                           static_cast<unsigned>(psf.version())));
    }
    overlay(img, addr,
            reinterpret_cast<const u8 *>(psf.exe().data()) + *off,
            psf.exe().size() - *off);
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
    if (vgmtrans::psf::dataOffsetForVersion(version)) {
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
      auto hints = vgmtrans::psf::collectMetadataHintsForOpenedFile(*file, psf);
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
