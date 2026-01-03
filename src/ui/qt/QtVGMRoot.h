/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QObject>
#include "Root.h"

class QtVGMRoot final : public QObject, public VGMRoot {
  Q_OBJECT

public:
  ~QtVGMRoot() override = default;

  std::filesystem::path UI_getResourceDirPath() override;
  void UI_setRootPtr(VGMRoot** theRoot) override;
  void UI_loadRawFile(RawFile* newFile) override;
  void UI_removeRawFile(RawFile* targFile) override;

  void UI_addVGMFile(VGMFileVariant file) override;
  void UI_addVGMSeq(VGMSeq* theSeq) override;
  void UI_addVGMInstrSet(VGMInstrSet* theInstrSet) override;
  void UI_addVGMSampColl(VGMSampColl* theSampColl) override;
  void UI_addVGMMisc(VGMMiscFile* theMiscFile) override;
  void UI_addVGMColl(VGMColl* theColl) override;

  void UI_toast(std::u8string_view message, ToastType type, int duration_ms = DEFAULT_TOAST_DURATION) override;
  void UI_addItem(VGMItem* item, VGMItem* parent, const std::string& itemName,
                  void* UI_specific) override;
  std::filesystem::path UI_getSaveFilePath(const std::string& suggestedFilename,
                                          const std::string& extension) override;
  std::filesystem::path UI_getSaveDirPath(const std::filesystem::path& suggestedDir) override;

signals:
  void UI_beginLoadRawFile() override;
  void UI_endLoadRawFile() override;
  void UI_beginRemoveRawFiles() override;
  void UI_endRemoveRawFiles() override;
  void UI_beginRemoveVGMFiles() override;
  void UI_endRemoveVGMFiles() override;
  void UI_beginRemoveVGMColls() override;
  void UI_endRemoveVGMColls() override;
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
