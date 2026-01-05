/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <fstream>
#include <filesystem>
#include "spdlog/fmt/std.h"

#include "Root.h"
#include "VGMColl.h"
#include "VGMFile.h"
#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMMiscFile.h"
#include "Format.h"
#include "Scanner.h"
#include "helper.h"
#include "Matcher.h"
#include "FileLoader.h"
#include "LoaderManager.h"
#include "ScannerManager.h"
#include "LogManager.h"

VGMRoot *pRoot;

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
  DiskFile* newFile = nullptr;

  try {
    newFile = new DiskFile(filePath);
  } catch (...) {
    UI_toast(fmt::format("Error opening file at path: {}", filePath), ToastType::Error);
    return false;
  }
  size_t vgmFileCountBefore = vgmFiles().size();
  if (!loadRawFile(newFile)) {
    delete newFile;
  }
  return vgmFiles().size() > vgmFileCountBefore;
}

/* Creates a new file backed by RAM */
bool VGMRoot::createVirtFile(const uint8_t *databuf, uint32_t fileSize, const std::string& filename,
                             const std::filesystem::path &parRawFileFullPath, const VGMTag& tag) {
  assert(fileSize != 0);

  auto newVirtFile = new VirtFile(databuf, fileSize, filename, parRawFileFullPath, tag);

  if (!loadRawFile(newVirtFile)) {
    delete newVirtFile;
    return false;
  }
  return true;
}

// Applies loaders and scanners to a rawfile, loading any discovered files
// returns true if files were discovered
bool VGMRoot::loadRawFile(RawFile *newRawFile) {
  pushLoadRawFile();
  if (newRawFile->useLoaders()) {
    for (const auto &l : LoaderManager::get().loaders()) {
      l->apply(newRawFile);
      auto res = l->results();

      /* If the loader extracted anything, we shouldn't have to scan */
      if (!res.empty()) {
        newRawFile->setUseScanners(false);

        for (const auto &file : res) {
          if (!loadRawFile(file)) {
            delete file;
          }
        }
      }
    }
  }

  if (newRawFile->useScanners()) {
    /*
     * Make use of the extension to run only a subset of scanners.
     * Unsure how good of an idea this is
     */
    auto specific_scanners =
      ScannerManager::get().scannersWithExtension(newRawFile->extension());
    if (!specific_scanners.empty()) {
      for (const auto &scanner : specific_scanners) {
        scanner->scan(newRawFile);
        if (auto matcher = scanner->format()->matcher) {
          matcher->onFinishedScan(newRawFile);
        }
      }
    } else {
      for (const auto &scanner : ScannerManager::get().scanners()) {
        scanner->scan(newRawFile);
        if (auto matcher = scanner->format()->matcher) {
          matcher->onFinishedScan(newRawFile);
        }
      }
    }
  }

  bool foundFiles = !newRawFile->containedVGMFiles().empty();
  if (foundFiles) {
    m_rawfiles.emplace_back(newRawFile);
    UI_loadRawFile(newRawFile);
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

  auto &vgmfiles = rawfile->containedVGMFiles();
  for (const auto & vgmfile : vgmfiles) {
    removeVGMFile(*vgmfile, false);
  }

  pushRemoveRawFiles();
  UI_removeRawFile(rawfile);
  m_rawfiles.erase(iter);
  popRemoveRawFiles();

  delete rawfile;
  return true;
}

void VGMRoot::addVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  m_vgmfiles.push_back(file);
  L_INFO("Loaded {} successfully.", variantToVGMFile(file)->name());
  UI_addVGMFile(file);
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

  while (!targFile->assocColls.empty()) {
    removeVGMColl(targFile->assocColls.back());
  }

  if (bRemoveEmptyRawFile) {
    const auto rawFile = targFile->rawFile();
    rawFile->removeContainedVGMFile(file);
    if (rawFile->containedVGMFiles().empty()) {
      removeRawFile(rawFile);
    }
  }
  delete targFile;
}

void VGMRoot::addVGMColl(VGMColl *theColl) {
  m_vgmcolls.push_back(theColl);
  UI_addVGMColl(theColl);
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
  delete coll;
}

void VGMRoot::removeAllFilesAndCollections() {
  pushRemoveAll();

  for (auto vgmcoll : m_vgmcolls)
    UI_removeVGMColl(vgmcoll);
  deleteVect(m_vgmcolls);

  for (auto variant : m_vgmfiles) {
    auto vgmfile = variantToVGMFile(variant);
    if (Format *fmt = vgmfile->format()) {
      fmt->onCloseFile(variant);
    }
    UI_removeVGMFile(vgmfile);
    delete variantToVGMFile(variant);
  }
  m_vgmfiles.clear();

  for (auto rawfile: m_rawfiles)
    UI_removeRawFile(rawfile);
  deleteVect(m_rawfiles);

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
bool VGMRoot::UI_writeBufferToFile(const std::filesystem::path &filepath, uint8_t *buf, size_t size) {
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
  return std::filesystem::current_path();
}
