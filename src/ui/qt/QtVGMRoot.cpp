/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileTreeView.h"
#include "UIHelpers.h"
#include "SequencePlayer.h"
#include <QApplication>
#include <QFileDialog>
#include <QString>
#include "QtVGMRoot.h"

QtVGMRoot qtVGMRoot;

std::filesystem::path QtVGMRoot::UI_getResourceDirPath() {
  std::filesystem::path appDir = std::filesystem::path(QApplication::applicationDirPath().toStdWString());

#if defined(Q_OS_MACOS)
  return (appDir / ".." / "Resources").lexically_normal();
#else
  return appDir;
#endif
}

void QtVGMRoot::UI_setRootPtr(VGMRoot** theRoot) {
  *theRoot = &qtVGMRoot;
}

void QtVGMRoot::UI_loadRawFile(RawFile*) {
  this->UI_addedRawFile();
}

void QtVGMRoot::UI_removeRawFile(RawFile*) {
  this->UI_removedRawFile();
}

void QtVGMRoot::UI_addVGMFile(VGMFileVariant file) {
  this->UI_addedVGMFile();
}

void QtVGMRoot::UI_addVGMSeq(VGMSeq*) {
}

void QtVGMRoot::UI_addVGMInstrSet(VGMInstrSet*) {
}

void QtVGMRoot::UI_addVGMSampColl(VGMSampColl*) {
}

void QtVGMRoot::UI_addVGMMisc(VGMMiscFile*) {
}

void QtVGMRoot::UI_addVGMColl(VGMColl*) {
  this->UI_addedVGMColl();
}

void QtVGMRoot::UI_toast(std::string_view message, ToastType type, int duration_ms) {
  this->UI_toastRequested(QString::fromUtf8(message), type, duration_ms);
}

void QtVGMRoot::UI_addItem(VGMItem* item, VGMItem* parent, const std::string& itemName,
                           void* UI_specific) {
  auto treeview = static_cast<VGMFileTreeView*>(UI_specific);
  treeview->addVGMItem(item, parent, itemName);
}

std::filesystem::path QtVGMRoot::UI_getSaveFilePath(const std::string& suggested_filename,
                                                    const std::string& extension) {
  return openSaveFileDialog(suggested_filename, extension);
}

std::filesystem::path QtVGMRoot::UI_getSaveDirPath(const std::filesystem::path&) {
  return openSaveDirDialog();
}
