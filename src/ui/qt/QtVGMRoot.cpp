/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include <QStandardPaths>
#include <QFileDialog>
#include <QString>
#include "QtVGMRoot.h"
#include "VGMFileTreeView.h"

QtVGMRoot qtVGMRoot;

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

void QtVGMRoot::UI_RemoveVGMColl(VGMColl*) {
  this->UI_RemovedVGMColl();
}

void QtVGMRoot::UI_BeginRemoveVGMFiles() {
}

void QtVGMRoot::UI_EndRemoveVGMFiles() {
}

void QtVGMRoot::UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName,
                           void* UI_specific) {
  auto treeview = static_cast<VGMFileTreeView*>(UI_specific);
  treeview->addVGMItem(item, parent, itemName);
}

std::wstring QtVGMRoot::UI_GetOpenFilePath(const std::wstring&,
                                           const std::wstring&) {
  std::wstring path = L"Placeholder";
  return path;
}

std::wstring QtVGMRoot::UI_GetSaveFilePath(const std::wstring&,
                                           const std::wstring&) {
  return QFileDialog::getSaveFileName(
             QApplication::activeWindow(), "Save file...",
             QStandardPaths::writableLocation(QStandardPaths::MusicLocation), "All files (*)")
      .toStdWString();
}

std::wstring QtVGMRoot::UI_GetSaveDirPath(const std::wstring&) {
  return QFileDialog::getExistingDirectory(
             QApplication::activeWindow(), "Save file...",
             QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
             QFileDialog::ShowDirsOnly)
      .toStdWString();
}