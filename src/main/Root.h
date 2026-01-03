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

constexpr int DEFAULT_TOAST_DURATION = 8000;

template <typename T>
T* variantToType(VGMFileVariant variant) {
  T* vgmFilePtr = nullptr;
  std::visit([&vgmFilePtr](auto *vgm) { vgmFilePtr = dynamic_cast<T*>(vgm); }, variant);
  return vgmFilePtr;
}
VGMFile* variantToVGMFile(VGMFileVariant variant);
VGMFileVariant vgmFileToVariant(VGMFile* vgmfile);

enum class ToastType { Info, Warning, Error, Success };

class VGMRoot {
public:
  VGMRoot() = default;
  virtual ~VGMRoot() = default;

  virtual bool init();
  virtual bool openRawFile(const std::filesystem::path &filePath);
  bool createVirtFile(const uint8_t* databuf, uint32_t fileSize, const std::string& filename,
                      const std::filesystem::path& parRawFileFullPath = {}, const VGMTag& tag = VGMTag());
  bool loadRawFile(RawFile* newRawFile);
  bool removeRawFile(RawFile *targFile);
  void addVGMFile(VGMFileVariant file);
  void removeVGMFile(VGMFileVariant file, bool bRemoveEmptyRawFile = true);
  void addVGMColl(VGMColl *theColl);
  void removeVGMColl(VGMColl *coll);
  void removeAllFilesAndCollections();

  void pushLoadRawFile();
  void popLoadRawFile();
  void pushRemoveRawFiles();
  void popRemoveRawFiles();
  void pushRemoveVGMFiles();
  void popRemoveVGMFiles();
  void pushRemoveVGMColls();
  void popRemoveVGMColls();
  void pushRemoveAll();
  void popRemoveAll();

  void log(LogItem *theLog);

  virtual std::filesystem::path UI_getResourceDirPath();
  virtual void UI_setRootPtr(VGMRoot **theRoot) = 0;
  virtual void UI_loadRawFile(RawFile *) {}
  virtual void UI_beginLoadRawFile() {}
  virtual void UI_endLoadRawFile() {}
  virtual void UI_addVGMFile(VGMFileVariant file);
  virtual void UI_addVGMSeq(VGMSeq *) {}
  virtual void UI_addVGMInstrSet(VGMInstrSet *) {}
  virtual void UI_addVGMSampColl(VGMSampColl *) {}
  virtual void UI_addVGMMisc(VGMMiscFile *) {}
  virtual void UI_addVGMColl(VGMColl *) {}
  virtual void UI_removeRawFile(RawFile *) {}
  virtual void UI_removeVGMFile(VGMFile *) {}
  virtual void UI_removeVGMColl(VGMColl *) {}

  virtual void UI_beginRemoveRawFiles() {}
  virtual void UI_endRemoveRawFiles() {}
  virtual void UI_beginRemoveVGMFiles() {}
  virtual void UI_endRemoveVGMFiles() {}
  virtual void UI_beginRemoveVGMColls() {}
  virtual void UI_endRemoveVGMColls() {}

  virtual void UI_addItem(VGMItem *, VGMItem *, const std::string &, void *) {}
  virtual std::filesystem::path UI_getSaveFilePath(const std::string &suggestedFilename,
                                         const std::string &extension = "") = 0;
  virtual std::filesystem::path UI_getSaveDirPath(const std::filesystem::path &suggestedDir = {}) = 0;
  virtual bool UI_writeBufferToFile(const std::filesystem::path &filepath, uint8_t *buf, size_t size);

  virtual void UI_log(LogItem *) { }
  virtual void UI_toast(std::u8string_view message, ToastType type = ToastType::Info,
                        int duration_ms = DEFAULT_TOAST_DURATION) {}

  const std::vector<RawFile*>& rawFiles() { return m_rawfiles; }
  const std::vector<VGMFileVariant>& vgmFiles() { return m_vgmfiles; }
  const std::vector<VGMColl*>& vgmColls() { return m_vgmcolls; }

private:
  int rawFileLoadRecurseStack = 0;
  int rawFileRemoveStack = 0;
  int vgmFileRemoveStack = 0;
  int vgmCollRemoveStack = 0;

  std::vector<RawFile *> m_rawfiles;
  std::vector<VGMFileVariant> m_vgmfiles;
  std::vector<VGMColl *> m_vgmcolls;
};

extern VGMRoot *pRoot;
