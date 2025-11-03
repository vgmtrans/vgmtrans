/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <fstream>

#include "Root.h"
#include "VGMColl.h"
#include "VGMFile.h"
#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMMiscFile.h"
#include "Format.h"
#include "Scanner.h"
#include "Matcher.h"

#include "FileLoader.h"
#include "LoaderManager.h"
#include "ScannerManager.h"
#include "LogManager.h"

#include <filesystem>

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
bool VGMRoot::openRawFile(const std::string &filePath) {
  DiskFile* newFile = nullptr;
  try {
    newFile = new DiskFile(filePath);
  } catch (...) {
    UI_toast("Error opening file at path: " + filePath, ToastType::Error);
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
                             const std::string &parRawFileFullPath, const VGMTag& tag) {
  assert(fileSize != 0);

  auto newVirtFile = new VirtFile(databuf, fileSize, filename,
    parRawFileFullPath, tag);

  if (!loadRawFile(newVirtFile)) {
    delete newVirtFile;
    return false;
  }
  return true;
}

// Applies loaders and scanners to a rawfile, loading any discovered files
// returns true if files were discovered
bool VGMRoot::loadRawFile(RawFile *newRawFile) {
  UI_beginLoadRawFile();
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

  UI_endLoadRawFile();
  return foundFiles;
}

bool VGMRoot::removeRawFile(size_t idx) {
  if (idx >= m_rawfiles.size()) {
    L_WARN("Requested deletion of RawFile with invalid index");
    return false;
  }

  auto rawfile = m_rawfiles[idx];
  auto &vgmfiles = rawfile->containedVGMFiles();
  for (const auto & vgmfile : vgmfiles) {
    removeVGMFile(*vgmfile, false);
  }

  UI_beginRemoveRawFiles(idx, idx);
  UI_removeRawFile(rawfile);
  m_rawfiles.erase(m_rawfiles.begin() + static_cast<std::ptrdiff_t>(idx));
  UI_endRemoveRawFiles();

  delete rawfile;
  return true;
}


bool VGMRoot::removeRawFile(RawFile *targFile) {
  if (!targFile)
    return false;

  auto iter = std::ranges::find(m_rawfiles, targFile);
  return removeRawFile(iter - m_rawfiles.begin());
}

void VGMRoot::addVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  m_vgmfiles.push_back(file);
  L_INFO("Loaded {} successfully.", variantToVGMFile(file)->name());
  UI_addVGMFile(file);
}

// Removes a VGMFile from the interface.  The UI_RemoveVGMFile will handle the
// interface-specific stuff
void VGMRoot::removeVGMFile(size_t idx, bool bRemoveEmptyRawFile) {
  if (idx >= m_vgmfiles.size()) {
    L_WARN("Requested deletion of VGMFile with invalid index");
    return;
  }

  auto file = m_vgmfiles[idx];
  auto targFile = variantToVGMFile(file);
  // First we should call the format's onClose handler in case it needs to use
  // the RawFile before we close it (FilenameMatcher, for ex)
  if (Format *fmt = targFile->format()) {
    fmt->onCloseFile(file);
  }

  UI_beginRemoveVGMFiles(idx, idx);
  UI_removeVGMFile(targFile);
  m_vgmfiles.erase(m_vgmfiles.begin() + static_cast<std::ptrdiff_t>(idx));
  UI_endRemoveVGMFiles();

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

void VGMRoot::removeVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file, bool bRemoveEmptyRawFile) {
  auto iter = std::ranges::find(m_vgmfiles, file);
  removeVGMFile(iter - m_vgmfiles.begin(), bRemoveEmptyRawFile);
}

void VGMRoot::addVGMColl(VGMColl *theColl) {
  m_vgmcolls.push_back(theColl);
  UI_addVGMColl(theColl);
}

void VGMRoot::removeVGMColl(size_t idx) {
  if (idx >= m_vgmcolls.size()) {
    L_WARN("Requested deletion of VGMColl with invalid index");
    return;
  }
  auto coll = m_vgmcolls[idx];

  UI_beginRemoveVGMColls(idx, idx);
  auto iter = std::ranges::find(m_vgmcolls, coll);
  if (iter != m_vgmcolls.end())
    m_vgmcolls.erase(iter);
  else
    L_WARN("Requested deletion for VGMColl but it was not found");

  coll->removeFileAssocs();
  UI_removeVGMColl(coll);
  UI_endRemoveVGMColls();
  delete coll;
}

void VGMRoot::removeVGMColl(VGMColl *coll) {
  auto iter = std::ranges::find(m_vgmcolls, coll);
  removeVGMColl(iter - m_vgmcolls.begin());
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
bool VGMRoot::UI_writeBufferToFile(const std::string &filepath, uint8_t *buf, size_t size) {
    std::ofstream outfile(filepath, std::ios::out | std::ios::trunc | std::ios::binary);

    if (!outfile.is_open()) {
      L_ERROR(std::string("Error: could not open file " + filepath + " for writing").c_str());
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

std::string VGMRoot::UI_getResourceDirPath() {
  return std::string(std::filesystem::current_path().generic_string());
}
