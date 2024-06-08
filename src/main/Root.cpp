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

/* Opens up a file from the filesystem and scans it */
bool VGMRoot::openRawFile(const std::string &filename) {
    auto newfile = new DiskFile(filename);
    return setupNewRawFile(newfile);
}

/* Creates a new file backed by RAM */
bool VGMRoot::createVirtFile(const uint8_t *databuf, uint32_t fileSize, const std::string& filename,
                             const std::string &parRawFileFullPath, const VGMTag& tag) {
    assert(fileSize != 0);

    auto newVirtFile = new VirtFile(databuf, fileSize, filename,
      parRawFileFullPath, tag);

    return setupNewRawFile(newVirtFile);
}

/* Applies loaders and scanners to a file, registering it if it contains anything */
bool VGMRoot::setupNewRawFile(RawFile *newRawFile) {
  UI_onBeginLoadRawFile();
  if (newRawFile->useLoaders()) {
    for (const auto &l : LoaderManager::get().loaders()) {
      l->apply(newRawFile);
      auto res = l->results();

      /* If the loader extracted anything, we shouldn't have to scan */
      if (!res.empty()) {
        newRawFile->setUseScanners(false);

        for (const auto &file : res) {
          setupNewRawFile(file);
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
      }
    } else {
      for (const auto &scanner : ScannerManager::get().scanners()) {
        scanner->scan(newRawFile);
      }
    }
  }

  if (!newRawFile->containedVGMFiles().empty()) {
    m_rawfiles.emplace_back(newRawFile);
    UI_addRawFile(newRawFile);
  }

  UI_onEndLoadRawFile();
  return true;
}

bool VGMRoot::closeRawFile(RawFile *targFile) {
  if (!targFile) {
    return false;
  }

  auto file = std::ranges::find(m_rawfiles, targFile);
  if (file != m_rawfiles.end()) {
    auto &vgmfiles = (*file)->containedVGMFiles();
    UI_beginRemoveVGMFiles();
    for (const auto & vgmfile : vgmfiles) {
      removeVGMFile(*vgmfile, false);
    }
    UI_endRemoveVGMFiles();

    m_rawfiles.erase(file);

    UI_closeRawFile(targFile);
  } else {
    L_WARN("Requested deletion for RawFile but it was not found");
    return false;
  }
  delete targFile;
  return true;
}

void VGMRoot::addVGMFile(
  std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
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
    UI_removeVGMFile(targFile);
    m_vgmfiles.erase(iter);
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
      closeRawFile(rawFile);
    }
  }
  delete targFile;
}

void VGMRoot::addVGMColl(VGMColl *theColl) {
    m_vgmcolls.push_back(theColl);
    UI_addVGMColl(theColl);
}

void VGMRoot::removeVGMColl(VGMColl *targColl) {
  auto iter = std::ranges::find(m_vgmcolls, targColl);
  if (iter != m_vgmcolls.end())
    m_vgmcolls.erase(iter);
  else
    L_WARN("Requested deletion for VGMColl but it was not found");

  targColl->removeFileAssocs();
  UI_removeVGMColl(targColl);
  delete targColl;
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
