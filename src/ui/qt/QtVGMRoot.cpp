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

const std::string QtVGMRoot::UI_GetResourceDirPath() {
#if defined(Q_OS_WIN)
  return (QApplication::applicationDirPath() + "/").toStdString();
#elif defined(Q_OS_OSX)
  return (QApplication::applicationDirPath() + "/../Resources/").toStdString();
#elif defined(Q_OS_LINUX)
  return (QApplication::applicationDirPath() + "/").toStdString();
#else
  return (QApplication::applicationDirPath() + "/").toStdString();
#endif
}

void QtVGMRoot::UI_SetRootPtr(VGMRoot** theRoot) {
  *theRoot = &qtVGMRoot;
}

void QtVGMRoot::UI_PreExit() {
}

void QtVGMRoot::UI_Exit() {
}

void QtVGMRoot::UI_AddRawFile(RawFile*) {
  this->UI_AddedRawFile();
}

void QtVGMRoot::UI_CloseRawFile(RawFile*) {
  this->UI_RemovedRawFile();
}

void QtVGMRoot::UI_OnBeginLoadRawFile() {
  if (rawFileLoadRecurseStack++ == 0)
    this->UI_BeganLoadingRawFile();
}

void QtVGMRoot::UI_OnEndLoadRawFile() {
  if (--rawFileLoadRecurseStack == 0)
    this->UI_EndedLoadingRawFile();
}

void QtVGMRoot::UI_OnBeginScan() {
}

void QtVGMRoot::UI_SetScanInfo() {
}

void QtVGMRoot::UI_OnEndScan() {
}

void QtVGMRoot::UI_AddVGMFile(VGMFile*) {
  this->UI_AddedVGMFile();
}

void QtVGMRoot::UI_AddVGMSeq(VGMSeq*) {
}

void QtVGMRoot::UI_AddVGMInstrSet(VGMInstrSet*) {
}

void QtVGMRoot::UI_AddVGMSampColl(VGMSampColl*) {
}

void QtVGMRoot::UI_AddVGMMisc(VGMMiscFile*) {
}

void QtVGMRoot::UI_AddVGMColl(VGMColl*) {
  this->UI_AddedVGMColl();
}

void QtVGMRoot::UI_BeginRemoveVGMFiles() {
  this->UI_BeganRemovingVGMFiles();
}

void QtVGMRoot::UI_EndRemoveVGMFiles() {
  this->UI_EndedRemovingVGMFiles();
}

void QtVGMRoot::UI_AddItem(VGMItem* item, VGMItem* parent, const std::string& itemName,
                           void* UI_specific) {
  auto treeview = static_cast<VGMFileTreeView*>(UI_specific);
  treeview->addVGMItem(item, parent, itemName);
}

std::string QtVGMRoot::UI_GetOpenFilePath(const std::string&, const std::string&) {
  std::string path = "Placeholder";
  return path;
}

std::string QtVGMRoot::UI_GetSaveFilePath(const std::string& suggested_filename,
                                           const std::string& extension) {
  return OpenSaveFileDialog(suggested_filename, extension);
}

std::string QtVGMRoot::UI_GetSaveDirPath(const std::string&) {
  return OpenSaveDirDialog();
}