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

bool VGMRoot::Init() {
    UI_SetRootPtr(&pRoot);
    return true;
}

/* Opens up a file from the filesystem and scans it */
bool VGMRoot::OpenRawFile(const std::string &filename) {
    auto newfile = new DiskFile(filename);
    return SetupNewRawFile(newfile);
}

/* Creates a new file backed by RAM */
bool VGMRoot::CreateVirtFile(uint8_t *databuf, uint32_t fileSize, const std::string &filename,
                             const std::string &parRawFileFullPath, VGMTag tag) {
    assert(fileSize != 0);

    auto newVirtFile = new VirtFile(databuf, fileSize, filename,
      parRawFileFullPath, tag);

    return SetupNewRawFile(newVirtFile);
}

/* Applies loaders and scanners to a file, registering it if it contains anything */
bool VGMRoot::SetupNewRawFile(RawFile *newRawFile) {
  UI_OnBeginLoadRawFile();
  if (newRawFile->useLoaders()) {
    for (const auto &l : LoaderManager::get().loaders()) {
      l->apply(newRawFile);
      auto res = l->results();

      /* If the loader extracted anything, we shouldn't have to scan */
      if (!res.empty()) {
        newRawFile->setUseScanners(false);

        for (const auto &file : res) {
          SetupNewRawFile(file);
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
      ScannerManager::get().scanners_with_extension(newRawFile->extension());
    if (!specific_scanners.empty()) {
      for (const auto &scanner : specific_scanners) {
        scanner->Scan(newRawFile);
      }
    } else {
      for (const auto &scanner : ScannerManager::get().scanners()) {
        scanner->Scan(newRawFile);
      }
    }
  }

  if (!newRawFile->containedVGMFiles().empty()) {
    vRawFile.emplace_back(newRawFile);
    UI_AddRawFile(newRawFile);
  }

  UI_OnEndLoadRawFile();
  return true;
}

bool VGMRoot::CloseRawFile(RawFile *targFile) {
  if (!targFile) {
    return false;
  }

  auto file = std::find(vRawFile.begin(), vRawFile.end(), targFile);
  if (file != vRawFile.end()) {
    auto &vgmfiles = (*file)->containedVGMFiles();
    UI_BeginRemoveVGMFiles();
    for (const auto & vgmfile : vgmfiles) {
      RemoveVGMFile(*vgmfile, false);
    }
    UI_EndRemoveVGMFiles();

    vRawFile.erase(file);

    UI_CloseRawFile(targFile);
  } else {
    L_WARN("Requested deletion for RawFile but it was not found");
    return false;
  }
  delete targFile;
  return true;
}

void VGMRoot::AddVGMFile(
  std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  vVGMFile.push_back(file);
  L_INFO("Loaded {} successfully.", *variantToVGMFile(file)->GetName());
  UI_AddVGMFile(file);
}

// Removes a VGMFile from the interface.  The UI_RemoveVGMFile will handle the
// interface-specific stuff
void VGMRoot::RemoveVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file, bool bRemoveRawFile) {
  auto targFile = variantToVGMFile(file);
  // First we should call the format's onClose handler in case it needs to use
  // the RawFile before we close it (FilenameMatcher, for ex)
  Format *fmt = targFile->GetFormat();
  if (fmt) {
    fmt->OnCloseFile(file);
  }

  auto iter = std::find(vVGMFile.begin(), vVGMFile.end(), file);

  if (iter != vVGMFile.end()) {
    UI_RemoveVGMFile(targFile);
    vVGMFile.erase(iter);
  } else {
    L_WARN("Requested deletion for VGMFile but it was not found");
  }

  while (!targFile->assocColls.empty()) {
    RemoveVGMColl(targFile->assocColls.back());
  }

  if (bRemoveRawFile) {
    const auto rawFile = targFile->GetRawFile();
    rawFile->RemoveContainedVGMFile(file);
    if (rawFile->containedVGMFiles().empty()) {
      CloseRawFile(rawFile);
    }
  }
  delete targFile;
}

void VGMRoot::AddVGMColl(VGMColl *theColl) {
    vVGMColl.push_back(theColl);
    UI_AddVGMColl(theColl);
}

void VGMRoot::RemoveVGMColl(VGMColl *targColl) {
  auto iter = find(vVGMColl.begin(), vVGMColl.end(), targColl);
  if (iter != vVGMColl.end())
    vVGMColl.erase(iter);
  else
    L_WARN("Requested deletion for VGMColl but it was not found");

  targColl->RemoveFileAssocs();
  UI_RemoveVGMColl(targColl);
  delete targColl;
}

// This virtual function is called whenever a VGMFile is added to the interface.
// By default, it simply sorts out what type of file was added and then calls a more
// specific virtual function for the file type.  It is virtual in case a user-interface
// wants do something universally whenever any type of VGMFiles is added.
void VGMRoot::UI_AddVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
    if(auto seq = std::get_if<VGMSeq *>(&file)) {
        UI_AddVGMSeq(*seq);
    } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
        UI_AddVGMInstrSet(*instr);
    } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
        UI_AddVGMSampColl(*sampcoll);
    } else if(auto misc = std::get_if<VGMMiscFile *>(&file)) {
        UI_AddVGMMisc(*misc);
    }
}

// Given a pointer to a buffer of data, size, and a filename, this function writes the data
// into a file on the filesystem.
bool VGMRoot::UI_WriteBufferToFile(const std::string &filepath, uint8_t *buf, uint32_t size) {
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
void VGMRoot::Log(LogItem *theLog) {
  UI_Log(theLog);
}

const std::string VGMRoot::UI_GetResourceDirPath() {
  return std::string(std::filesystem::current_path().generic_string());
}
