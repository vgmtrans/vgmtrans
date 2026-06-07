/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Root.h"

#include "base/Types.h"
#include "FileLoader.h"
#include "Format.h"
#include "Helper.h"
#include "LoaderManager.h"
#include "LogManager.h"
#include "Matcher.h"
#include "Scanner.h"
#include "ScannerManager.h"
#include "VGMColl.h"
#include "VGMFile.h"
#include "VGMInstrSet.h"
#include "VGMMiscFile.h"
#include "VGMRgn.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "VGMSeq.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

#include <spdlog/fmt/std.h>

VGMRoot *pRoot;

VGMRoot::VGMRoot() = default;
VGMRoot::~VGMRoot() = default;

VGMFile* variantToVGMFile(VGMFileVariant variant) {
  VGMFile *vgmFilePtr = nullptr;
  std::visit([&vgmFilePtr](auto *vgm) { vgmFilePtr = static_cast<VGMFile *>(vgm); }, variant);
  return vgmFilePtr;
}

VGMFileVariant vgmFileToVariant(VGMFile* file) {
  if (auto seq = dynamic_cast<VGMSeq*>(file)) {
    return seq;
  } else if (auto instrSet = dynamic_cast<VGMInstrSet*>(file)) {
    return instrSet;
  } else if (auto sampColl = dynamic_cast<VGMSampColl*>(file)) {
    return sampColl;
  } else if (auto miscFile = dynamic_cast<VGMMiscFile*>(file)) {
    return miscFile;
  } else {
    throw std::runtime_error("Unknown VGMFile subclass");
  }
}

bool VGMRoot::init() {
    UI_setRootPtr(&pRoot);
    return true;
}

/* Opens up a file from the filesystem and scans it.
 * Returns bool indicating if VGMFiles were found. */
bool VGMRoot::openRawFile(const std::filesystem::path &filePath) {
  std::unique_ptr<DiskFile> newFile;

  try {
    newFile = std::make_unique<DiskFile>(filePath);
  } catch (...) {
    L_ERROR("Failed to open file '{}': could not read from disk (file not found or permission denied)", filePath);
    UI_toast(fmt::format("Error opening file at path: {}", filePath), ToastType::Error);
    return false;
  }
  size_t vgmFileCountBefore = vgmFiles().size();
  loadRawFile(std::move(newFile));
  return vgmFiles().size() > vgmFileCountBefore;
}

/* Creates a new file backed by RAM */
bool VGMRoot::createVirtFile(const u8 *databuf, u32 fileSize, const std::string& filename,
                             const std::filesystem::path &parRawFileFullPath, const VGMTag& tag) {
  assert(fileSize != 0);

  return loadRawFile(std::make_unique<VirtFile>(databuf, fileSize, filename, parRawFileFullPath, tag));
}

// Applies loaders and scanners to a rawfile, loading any discovered files
// returns true if files were discovered
bool VGMRoot::loadRawFile(std::unique_ptr<RawFile> newRawFile) {
  if (!newRawFile) {
    return false;
  }

  RawFile* rawFile = newRawFile.get();
  pushLoadRawFile();
  if (rawFile->useLoaders()) {
    for (const auto &l : LoaderManager::get().loaders()) {
      l->apply(rawFile);
      auto res = l->results();

      /* If the loader extracted anything, we shouldn't have to scan */
      if (!res.empty()) {
        rawFile->setUseScanners(false);

        for (auto& file : res) {
          loadRawFile(std::move(file));
        }
      }
    }
  }

  if (rawFile->useScanners()) {
    /*
     * Make use of the extension to run only a subset of scanners.
     * Unsure how good of an idea this is
     */
    auto specific_scanners =
      ScannerManager::get().scannersWithExtension(rawFile->extension());
    if (!specific_scanners.empty()) {
      for (const auto &scanner : specific_scanners) {
        scanner->scan(rawFile);
        if (auto matcher = scanner->format()->matcher.get()) {
          matcher->onFinishedScan(rawFile);
        }
      }
    } else {
      for (const auto &scanner : ScannerManager::get().scanners()) {
        scanner->scan(rawFile);
        if (auto matcher = scanner->format()->matcher.get()) {
          matcher->onFinishedScan(rawFile);
        }
      }
    }
  }

  bool foundFiles = !rawFile->containedVGMFiles().empty();
  if (foundFiles) {
    m_rawfiles.emplace_back(rawFile);
    UI_loadRawFile(rawFile);
    m_ownedRawFiles.emplace_back(std::move(newRawFile));
  }

  popLoadRawFile();
  return foundFiles;
}

bool VGMRoot::removeRawFile(RawFile *rawfile) {
  if (!rawfile)
    return false;

  auto iter = std::ranges::find(m_rawfiles, rawfile);
  if (iter == m_rawfiles.end()) {
    L_WARN("Requested deletion of a RawFile not stored in Root");
    return false;
  }

  auto vgmfiles = rawfile->containedVGMFiles();
  for (const auto &vgmfile : vgmfiles) {
    removeVGMFile(vgmfile, false);
  }

  pushRemoveRawFiles();
  UI_removeRawFile(rawfile);
  m_rawfiles.erase(iter);
  popRemoveRawFiles();

  auto ownedIter = std::ranges::find_if(m_ownedRawFiles, [rawfile](const auto& ownedRawFile) {
    return ownedRawFile.get() == rawfile;
  });
  if (ownedIter != m_ownedRawFiles.end()) {
    m_ownedRawFiles.erase(ownedIter);
  } else {
    L_WARN("RawFile removed from Root did not have an owning entry");
  }
  return true;
}

bool VGMRoot::loadVGMFile(std::unique_ptr<VGMFile> file, bool useMatcher) {
  if (!file || !file->load()) {
    return false;
  }

  sinkVGMFile(std::move(file), useMatcher);
  return true;
}

void VGMRoot::sinkVGMFile(std::unique_ptr<VGMFile>&& file, bool useMatcher) {
  if (!file) {
    return;
  }

  auto discoveredFiles = file->releaseDiscoveredFiles();
  for (auto& discoveredFile : discoveredFiles) {
    sinkVGMFile(std::move(discoveredFile), useMatcher);
  }

  auto* vgmFile = file.get();
  auto variant = vgmFileToVariant(vgmFile);
  vgmFile->rawFile()->addContainedVGMFile(variant);
  m_ownedVGMFiles.emplace_back(std::move(file));
  m_vgmfiles.push_back(variant);
  L_INFO("Loaded {} ({} bytes at {:x}) successfully.", vgmFile->name(), vgmFile->length(), vgmFile->offset());
  UI_addVGMFile(variant);

  if (useMatcher) {
    if (auto fmt = vgmFile->format(); fmt) {
      fmt->onNewFile(variant);
    }
  }
}

// Removes a VGMFile from the interface.  The UI_RemoveVGMFile will handle the
// interface-specific stuff
void VGMRoot::removeVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file, bool bRemoveEmptyRawFile) {
  auto targFile = variantToVGMFile(file);
  // First we should call the format's onClose handler in case it needs to use
  // the RawFile before we close it (FilenameMatcher, for ex)
  if (Format *fmt = targFile->format()) {
    fmt->onCloseFile(file);
  }

  auto iter = std::ranges::find(m_vgmfiles, file);

  if (iter != m_vgmfiles.end()) {
    pushRemoveVGMFiles();
    UI_removeVGMFile(targFile);
    m_vgmfiles.erase(iter);
    popRemoveVGMFiles();
  } else {
    L_WARN("Requested deletion for VGMFile but it was not found");
  }

  while (targFile->hasAssocColls()) {
    removeVGMColl(targFile->assocColls().back());
  }

  if (bRemoveEmptyRawFile) {
    const auto rawFile = targFile->rawFile();
    rawFile->removeContainedVGMFile(file);
    if (rawFile->containedVGMFiles().empty()) {
      removeRawFile(rawFile);
    }
  }

  auto ownedIter = std::ranges::find_if(m_ownedVGMFiles, [targFile](const auto& ownedVGMFile) {
    return ownedVGMFile.get() == targFile;
  });
  if (ownedIter != m_ownedVGMFiles.end()) {
    m_ownedVGMFiles.erase(ownedIter);
  } else {
    L_WARN("VGMFile removed from Root did not have an owning entry");
  }
}

bool VGMRoot::loadVGMColl(std::unique_ptr<VGMColl> coll) {
  if (!coll || !coll->load()) {
    return false;
  }

  sinkVGMColl(std::move(coll));
  return true;
}

void VGMRoot::sinkVGMColl(std::unique_ptr<VGMColl>&& coll) {
  if (!coll) {
    return;
  }

  auto* rawColl = coll.get();
  m_vgmcolls.push_back(rawColl);
  UI_addVGMColl(rawColl);
  m_ownedVGMColls.emplace_back(std::move(coll));
}

void VGMRoot::removeVGMColl(VGMColl *coll) {
  auto iter = std::ranges::find(m_vgmcolls, coll);
  pushRemoveVGMColls();
  if (iter != m_vgmcolls.end()) {
    m_vgmcolls.erase(iter);
  } else {
    L_WARN("Requested deletion of VGMColl not stored in Root");
  }

  coll->removeFileAssocs();
  UI_removeVGMColl(coll);
  popRemoveVGMColls();

  auto ownedIter = std::ranges::find_if(m_ownedVGMColls, [coll](const auto& ownedColl) {
    return ownedColl.get() == coll;
  });
  if (ownedIter != m_ownedVGMColls.end()) {
    m_ownedVGMColls.erase(ownedIter);
  } else {
    L_WARN("VGMColl removed from Root did not have an owning entry");
  }
}

void VGMRoot::removeAllFilesAndCollections() {
  pushRemoveAll();

  for (auto vgmcoll : m_vgmcolls)
    UI_removeVGMColl(vgmcoll);
  m_vgmcolls.clear();
  m_ownedVGMColls.clear();

  for (auto variant : m_vgmfiles) {
    auto vgmfile = variantToVGMFile(variant);
    if (Format *fmt = vgmfile->format()) {
      fmt->onCloseFile(variant);
    }
    UI_removeVGMFile(vgmfile);
  }
  m_vgmfiles.clear();
  m_ownedVGMFiles.clear();

  for (auto rawfile: m_rawfiles)
    UI_removeRawFile(rawfile);
  m_rawfiles.clear();
  m_ownedRawFiles.clear();

  popRemoveAll();
}

void VGMRoot::pushLoadRawFile() {
  if (rawFileLoadRecurseStack++ == 0)
    this->UI_beginLoadRawFile();
}

void VGMRoot::popLoadRawFile() {
  if (--rawFileLoadRecurseStack == 0)
    this->UI_endLoadRawFile();
}

void VGMRoot::pushRemoveRawFiles() {
  if (rawFileRemoveStack++ == 0)
    this->UI_beginRemoveRawFiles();
}

void VGMRoot::popRemoveRawFiles() {
  if (--rawFileRemoveStack == 0)
    this->UI_endRemoveRawFiles();
}

void VGMRoot::pushRemoveVGMFiles() {
  if (vgmFileRemoveStack++ == 0)
    this->UI_beginRemoveVGMFiles();
}

void VGMRoot::popRemoveVGMFiles() {
  if (--vgmFileRemoveStack == 0)
    this->UI_endRemoveVGMFiles();
}

void VGMRoot::pushRemoveVGMColls() {
  if (vgmCollRemoveStack++ == 0)
    this->UI_beginRemoveVGMColls();
}

void VGMRoot::popRemoveVGMColls() {
  if (--vgmCollRemoveStack == 0)
    this->UI_endRemoveVGMColls();
}

void VGMRoot::pushRemoveAll() {
  pushRemoveRawFiles();
  pushRemoveVGMFiles();
  pushRemoveVGMColls();
}

void VGMRoot::popRemoveAll() {
  popRemoveVGMColls();
  popRemoveVGMFiles();
  popRemoveRawFiles();
}

// This virtual function is called whenever a VGMFile is added to the interface.
// By default, it simply sorts out what type of file was added and then calls a more
// specific virtual function for the file type.  It is virtual in case a user-interface
// wants do something universally whenever any type of VGMFiles is added.
void VGMRoot::UI_addVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if(auto seq = std::get_if<VGMSeq *>(&file)) {
    UI_addVGMSeq(*seq);
  } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
    UI_addVGMInstrSet(*instr);
  } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
    UI_addVGMSampColl(*sampcoll);
  } else if(auto misc = std::get_if<VGMMiscFile *>(&file)) {
    UI_addVGMMisc(*misc);
  }
}

// Given a pointer to a buffer of data, size, and a filename, this function writes the data
// into a file on the filesystem.
bool VGMRoot::UI_writeBufferToFile(const std::filesystem::path &filepath, u8 *buf, size_t size) {
  std::ofstream outfile(filepath, std::ios::out | std::ios::trunc | std::ios::binary);

  if (!outfile.is_open()) {
    L_ERROR("Error: could not open file {} for writing", filepath);
    return false;
  }

  outfile.write(reinterpret_cast<char *>(buf), size);
  outfile.close();
  return true;
}

// Adds a log item to the interface. The UI_AddLog function will handle the interface-specific stuff
void VGMRoot::log(LogItem *theLog) {
  UI_log(theLog);
}

std::filesystem::path VGMRoot::UI_getResourceDirPath() {
#if defined(__APPLE__)
  std::filesystem::path resDir = (std::filesystem::current_path() / ".." / "Resources").lexically_normal();
  if (std::filesystem::exists(resDir / "mame_roms.json")) {
    return resDir;
  }
#endif

#if defined(VGMTRANS_DATA_DIR)
  std::filesystem::path dataDir = std::filesystem::path(VGMTRANS_DATA_DIR);
  if (std::filesystem::exists(dataDir / "mame_roms.json")) {
    return dataDir;
  }
#endif

#if defined(DEV_ENV_BUILD_TREE)
  std::filesystem::path devDir = std::filesystem::path(DEV_ENV_BUILD_TREE);
  if (std::filesystem::exists(devDir / "mame_roms.json")) {
    return devDir;
  }
#endif

  return std::filesystem::current_path();
}
