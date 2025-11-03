/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include <QFileDialog>
#include <QString>
#include "QtVGMRoot.h"
#include "VGMFileTreeView.h"
#include "UIHelpers.h"

QtVGMRoot qtVGMRoot;

std::string QtVGMRoot::UI_getResourceDirPath() {
#if defined(Q_OS_WIN)
  return (QApplication::applicationDirPath() + "/").toStdString();
#elif defined(Q_OS_MACOS)
  return (QApplication::applicationDirPath() + "/../Resources/").toStdString();
#elif defined(Q_OS_LINUX)
  return (QApplication::applicationDirPath() + "/").toStdString();
#else
  return (QApplication::applicationDirPath() + "/").toStdString();
#endif
}

void QtVGMRoot::UI_setRootPtr(VGMRoot** theRoot) {
  *theRoot = &qtVGMRoot;
}

void QtVGMRoot::UI_addRawFile(RawFile*) {
  this->UI_addedRawFile();
}

void QtVGMRoot::UI_removeRawFile(RawFile*) {
  this->UI_removedRawFile();
}

void QtVGMRoot::UI_beginRemoveRawFiles(int startIdx, int endIdx) {
  this->UI_beganRemovingRawFiles(startIdx, endIdx);
}

void QtVGMRoot::UI_endRemoveRawFiles() {
  this->UI_endedRemovingRawFiles();
}

void QtVGMRoot::UI_onBeginLoadRawFile() {
  if (rawFileLoadRecurseStack++ == 0)
    this->UI_beganLoadingRawFile();
}

void QtVGMRoot::UI_onEndLoadRawFile() {
  if (--rawFileLoadRecurseStack == 0)
    this->UI_endedLoadingRawFile();
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

void QtVGMRoot::UI_beginRemoveVGMFiles(int startIdx, int endIdx) {
  this->UI_beganRemovingVGMFiles(startIdx, endIdx);
}

void QtVGMRoot::UI_endRemoveVGMFiles() {
  this->UI_endedRemovingVGMFiles();
}

void QtVGMRoot::UI_beginRemoveVGMColls() {
  this->UI_beganRemovingVGMColls();
}

void QtVGMRoot::UI_endRemoveVGMColls() {
  this->UI_endedRemovingVGMColls();
}

void QtVGMRoot::UI_toast(const std::string& message, ToastType type, int duration_ms) {
  this->UI_toastRequested(QString::fromStdString(message), type, duration_ms);
}

void QtVGMRoot::UI_addItem(VGMItem* item, VGMItem* parent, const std::string& itemName,
                           void* UI_specific) {
  auto treeview = static_cast<VGMFileTreeView*>(UI_specific);
  treeview->addVGMItem(item, parent, itemName);
}

std::string QtVGMRoot::UI_getSaveFilePath(const std::string& suggested_filename,
                                           const std::string& extension) {
  return openSaveFileDialog(suggested_filename, extension);
}

std::string QtVGMRoot::UI_getSaveDirPath(const std::string&) {
  return openSaveDirDialog();
}