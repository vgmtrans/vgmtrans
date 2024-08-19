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

class VGMRoot {
public:
  VGMRoot() = default;
  virtual ~VGMRoot() = default;

  virtual bool init();
  virtual bool openRawFile(const std::string &filename);
  bool createVirtFile(const uint8_t* databuf, uint32_t fileSize, const std::string& filename,
                      const std::string& parRawFileFullPath = "", const VGMTag& tag = VGMTag());
  bool setupNewRawFile(RawFile* newRawFile);
  bool closeRawFile(RawFile *targFile);
  void addVGMFile(VGMFileVariant file);
  void removeVGMFile(VGMFileVariant file, bool bRemoveEmptyRawFile = true);
  void addVGMColl(VGMColl *theColl);
  void removeVGMColl(VGMColl *theFile);
  void log(LogItem *theLog);

  virtual std::string UI_getResourceDirPath();
  virtual void UI_setRootPtr(VGMRoot **theRoot) = 0;
  virtual void UI_addRawFile(RawFile *) {}
  virtual void UI_closeRawFile(RawFile *) {}

  virtual void UI_onBeginLoadRawFile() {}
  virtual void UI_onEndLoadRawFile() {}
  virtual void UI_addVGMFile(VGMFileVariant file);
  virtual void UI_addVGMSeq(VGMSeq *) {}
  virtual void UI_addVGMInstrSet(VGMInstrSet *) {}
  virtual void UI_addVGMSampColl(VGMSampColl *) {}
  virtual void UI_addVGMMisc(VGMMiscFile *) {}
  virtual void UI_addVGMColl(VGMColl *) {}
  virtual void UI_removeVGMFile(VGMFile *) {}
  virtual void UI_beginRemoveVGMFiles() {}
  virtual void UI_endRemoveVGMFiles() {}
  virtual void UI_log(LogItem *) { }

  virtual void UI_removeVGMColl(VGMColl *) {}
  virtual void UI_addItem(VGMItem *, VGMItem *, const std::string &, void *) {}
  virtual std::string UI_getSaveFilePath(const std::string &suggestedFilename,
                                         const std::string &extension = "") = 0;
  virtual std::string UI_getSaveDirPath(const std::string &suggestedDir = "") = 0;
  virtual bool UI_writeBufferToFile(const std::string &filepath, uint8_t *buf, size_t size);

  const std::vector<RawFile*>& rawFiles() { return m_rawfiles; }
  const std::vector<VGMFileVariant>& vgmFiles() { return m_vgmfiles; }
  const std::vector<VGMColl*>& vgmColls() { return m_vgmcolls; }

private:
  std::vector<RawFile *> m_rawfiles;
  std::vector<VGMFileVariant> m_vgmfiles;
  std::vector<VGMColl *> m_vgmcolls;
};

extern VGMRoot *pRoot;
