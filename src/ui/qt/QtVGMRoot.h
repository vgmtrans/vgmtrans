/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QObject>
#include <QMetaType>
#include "Root.h"

Q_DECLARE_METATYPE(ToastType);

class QtVGMRoot final : public QObject, public VGMRoot {
  Q_OBJECT

public:
  QtVGMRoot();
  ~QtVGMRoot() override = default;

  std::string UI_getResourceDirPath() override;
  void UI_setRootPtr(VGMRoot** theRoot) override;
  void UI_addRawFile(RawFile* newFile) override;
  void UI_closeRawFile(RawFile* targFile) override;

  void UI_onBeginLoadRawFile() override;
  void UI_onEndLoadRawFile() override;
  void UI_addVGMFile(VGMFileVariant file) override;
  void UI_addVGMSeq(VGMSeq* theSeq) override;
  void UI_addVGMInstrSet(VGMInstrSet* theInstrSet) override;
  void UI_addVGMSampColl(VGMSampColl* theSampColl) override;
  void UI_addVGMMisc(VGMMiscFile* theMiscFile) override;
  void UI_addVGMColl(VGMColl* theColl) override;
  void UI_beginRemoveVGMFiles() override;
  void UI_endRemoveVGMFiles() override;
  void UI_beginRemoveVGMColls() override;
  void UI_endRemoveVGMColls() override;
  void UI_toast(const std::string& message, ToastType type, int duration_ms) override;
  void UI_addItem(VGMItem* item, VGMItem* parent, const std::string& itemName,
                  void* UI_specific) override;
  std::string UI_getSaveFilePath(const std::string& suggestedFilename,
                                          const std::string& extension) override;
  std::string UI_getSaveDirPath(const std::string& suggestedDir) override;

private:
  int rawFileLoadRecurseStack = 0;

signals:
  void UI_beganLoadingRawFile();
  void UI_endedLoadingRawFile();
  void UI_beganRemovingVGMFiles();
  void UI_endedRemovingVGMFiles();
  void UI_beganRemovingVGMColls();
  void UI_endedRemovingVGMColls();
  void UI_addedRawFile();
  void UI_removedRawFile();
  void UI_addedVGMFile();
  void UI_addedVGMColl();
  void UI_removedVGMColl();
  void UI_toastRequested(QString message, ToastType type, int duration_ms);
  void UI_removeVGMColl(VGMColl* targColl) override;
  void UI_removeVGMFile(VGMFile* targFile) override;
  void UI_log(LogItem* theLog) override;
};

extern QtVGMRoot qtVGMRoot;
