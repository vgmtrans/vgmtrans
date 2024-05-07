/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <variant>
#include <memory>

#include "common.h"
#include "VGMTag.h"

class VGMScanner;
class VGMColl;
class VGMItem;
class VGMFile;
class RawFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;
class LogItem;

VGMFile* variantToVGMFile(VGMFileVariant variant);
VGMFileVariant vgmFileToVariant(VGMFile* vgmfile);

// global switch indicating if we are in CLI mode
inline bool g_isCliMode = false;

class VGMRoot {
public:
  VGMRoot() = default;
  virtual ~VGMRoot() = default;

  virtual bool Init();
  virtual bool OpenRawFile(const std::string &filename);
  bool CreateVirtFile(const uint8_t* databuf, uint32_t fileSize, const std::string& filename,
                      const std::string& parRawFileFullPath = "", const VGMTag& tag = VGMTag());
  bool SetupNewRawFile(RawFile* newRawFile);
  bool CloseRawFile(RawFile *targFile);
  void AddVGMFile(VGMFileVariant file);
  void RemoveVGMFile(VGMFileVariant file, bool bRemoveEmptyRawFile = true);
  void AddVGMColl(VGMColl *theColl);
  void RemoveVGMColl(VGMColl *theFile);
  void Log(LogItem *theLog);

  virtual std::string UI_GetResourceDirPath();
  virtual void UI_SetRootPtr(VGMRoot **theRoot) = 0;
  virtual void UI_AddRawFile(RawFile *) {}
  virtual void UI_CloseRawFile(RawFile *) {}

  virtual void UI_OnBeginLoadRawFile() {}
  virtual void UI_OnEndLoadRawFile() {}
  virtual void UI_AddVGMFile(VGMFileVariant file);
  virtual void UI_AddVGMSeq(VGMSeq *) {}
  virtual void UI_AddVGMInstrSet(VGMInstrSet *) {}
  virtual void UI_AddVGMSampColl(VGMSampColl *) {}
  virtual void UI_AddVGMMisc(VGMMiscFile *) {}
  virtual void UI_AddVGMColl(VGMColl *) {}
  virtual void UI_RemoveVGMFile(VGMFile *) {}
  virtual void UI_BeginRemoveVGMFiles() {}
  virtual void UI_EndRemoveVGMFiles() {}
  virtual void UI_Log(LogItem *) { }

  virtual void UI_RemoveVGMColl(VGMColl *) {}
  virtual void UI_AddItem(VGMItem *, VGMItem *, const std::string &, void *) {}
  virtual std::string UI_GetSaveFilePath(const std::string &suggestedFilename,
                                         const std::string &extension = "") = 0;
  virtual std::string UI_GetSaveDirPath(const std::string &suggestedDir = "") = 0;
  virtual bool UI_WriteBufferToFile(const std::string &filepath, uint8_t *buf, size_t size);

  std::vector<RawFile *> vRawFile;
  std::vector<VGMColl *> vVGMColl;
  std::vector<VGMFileVariant> vVGMFile;
};

extern VGMRoot *pRoot;
