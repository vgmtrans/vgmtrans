#ifndef VGMTRANS_CORE_H
#define VGMTRANS_CORE_H

#include "common.h"
#include "main/VGMTag.h"

class UICallbacks;
class VGMColl;
class VGMFile;
class RawFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;
class VGMLoader;
class VGMScanner;
class VGMItem;
class LogItem;

//namespace Core
//{



class Core {

 public:
  Core();

  bool Init(UICallbacks *root);
  void Reset(void);
  void Exit(void);
  bool OpenRawFile(const std::wstring &filename);
  bool CreateVirtFile(uint8_t *databuf,
                      uint32_t fileSize,
                      const std::wstring &filename,
                      const std::wstring &parRawFileFullPath = L"",
                      const VGMTag tag = VGMTag());
  bool SetupNewRawFile(RawFile *newRawFile);
  bool CloseRawFile(RawFile *targFile);
  void AddVGMFile(VGMFile *theFile);
  void RemoveVGMFile(VGMFile *theFile, bool bRemoveFromRaw = true);
  void AddVGMColl(VGMColl *theColl);
  void RemoveVGMColl(VGMColl *theFile);
  bool SaveAllAsRaw();

  bool WriteBufferToFile(const wstring &filepath, uint8_t *buf, uint32_t size);
  void AddLogItem(LogItem *theLog);
  void AddScanner(const std::string &formatname);
  template<class T>
  void AddLoader() {
    vLoader.push_back(new T());
  }

  void AddItem(VGMItem *item, VGMItem *parent, const std::wstring &itemName, void *UI_specific);

  std::wstring GetOpenFilePath(const std::wstring &suggestedFilename = L"", const std::wstring &extension = L"");
  std::wstring GetSaveFilePath(const std::wstring &suggestedFilename, const std::wstring &extension = L"");
  std::wstring GetSaveDirPath(const std::wstring &suggestedDir = L"");

  std::vector<RawFile *> vRawFile;
  std::vector<VGMFile *> vVGMFile;
  std::vector<VGMColl *> vVGMColl;
  std::vector<LogItem *> vLogItem;

  std::vector<VGMLoader *> vLoader;
  std::vector<VGMScanner *> vScanner;

  UICallbacks *ui;
};

extern Core core;

//} // namespace

#endif //VGMTRANS_CORE_H
