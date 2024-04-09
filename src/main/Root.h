#pragma once

#include <ghc/filesystem.hpp>

#include "common.h"
#include "Loader.h"
#include "Scanner.h"
#include "LogItem.h"

namespace fs = ghc::filesystem;

class VGMColl;
class VGMFile;
class RawFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;
class LogItem;

// global switch indicating if we are in CLI mode
inline bool g_isCliMode = false;

class VGMRoot {
 public:
  VGMRoot(void);
 public:
  virtual ~VGMRoot(void);

  bool Init(void);
  void Reset(void);
  void Exit(void);
  virtual bool OpenRawFile(const std::string &filename);
  bool CreateVirtFile(uint8_t *databuf,
                      uint32_t fileSize,
                      const std::string &filename,
                      const std::string &parRawFileFullPath = "",
                      const VGMTag tag = VGMTag());
  bool SetupNewRawFile(RawFile *newRawFile);
  bool CloseRawFile(RawFile *targFile);
  void AddVGMFile(VGMFile *theFile);
  void RemoveVGMFile(VGMFile *theFile, bool bRemoveFromRaw = true);
  void AddVGMColl(VGMColl *theColl);
  void RemoveVGMColl(VGMColl *theColl);
  void AddLogItem(LogItem *theLog);
  void AddScanner(const std::string &formatname);

  template<class T>
  void AddLoader() {
    vLoader.push_back(new T());
  }

  virtual const std::string UI_GetResourceDirPath();
  virtual void UI_SetRootPtr(VGMRoot **theRoot) = 0;
  virtual void UI_PreExit() { }
  virtual void UI_Exit() = 0;
  virtual void UI_AddRawFile(RawFile *newFile) { }
  virtual void UI_CloseRawFile(RawFile *targFile) { }

  virtual void UI_OnBeginLoadRawFile() { }
  virtual void UI_OnEndLoadRawFile() { }
  virtual void UI_OnBeginScan() { }
  virtual void UI_SetScanInfo() { }
  virtual void UI_OnEndScan() { }
  virtual void UI_AddVGMFile(VGMFile *theFile);
  virtual void UI_AddVGMSeq(VGMSeq *theSeq) { }
  virtual void UI_AddVGMInstrSet(VGMInstrSet *theInstrSet) { }
  virtual void UI_AddVGMSampColl(VGMSampColl *theSampColl) { }
  virtual void UI_AddVGMMisc(VGMMiscFile *theMiscFile) { }
  virtual void UI_AddVGMColl(VGMColl *theColl) { }
  virtual void UI_AddLogItem(LogItem *theLog) { }
  virtual void UI_RemoveVGMFile(VGMFile *theFile) { }
  virtual void UI_BeginRemoveVGMFiles() { }
  virtual void UI_EndRemoveVGMFiles() { }
  virtual void UI_RemoveVGMColl(VGMColl *theColl) { }
  //virtual void UI_RemoveVGMFileRange(VGMFile* first, VGMFile* last) {}
  virtual void UI_AddItem(VGMItem *item, VGMItem *parent, const std::string &itemName, void *UI_specific) { }
  virtual std::string
      UI_GetOpenFilePath(const std::string &suggestedFilename = "", const std::string &extension = "") = 0;
  virtual std::string
      UI_GetSaveFilePath(const std::string &suggestedFilename, const std::string &extension = "") = 0;
  virtual std::string UI_GetSaveDirPath(const std::string &suggestedDir = "") = 0;
  virtual bool UI_WriteBufferToFile(const std::string &filepath, uint8_t *buf, uint32_t size);


  bool SaveAllAsRaw();

 public:
  //MatchMaker matchmaker;
  std::vector<RawFile *> vRawFile;
  std::vector<VGMFile *> vVGMFile;
  std::vector<VGMColl *> vVGMColl;
  std::vector<LogItem *> vLogItem;

  std::vector<VGMLoader *> vLoader;
  std::vector<VGMScanner *> vScanner;
};

extern VGMRoot *pRoot;
