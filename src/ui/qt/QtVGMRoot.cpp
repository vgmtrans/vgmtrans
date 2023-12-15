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

const std::wstring QtVGMRoot::UI_GetResourceDirPath() {
#if defined(Q_OS_WIN)
  return (QApplication::applicationDirPath() + "/").toStdWString();
#elif defined(Q_OS_OSX)
  return (QApplication::applicationDirPath() + "/../Resources/").toStdWString();
#elif defined(Q_OS_LINUX)
  return (QApplication::applicationDirPath() + "/").toStdWString();
#else
  return (QApplication::applicationDirPath() + "/").toStdWString();
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

void QtVGMRoot::UI_RemoveVGMColl(VGMColl*) {
  this->UI_RemovedVGMColl();
}

void QtVGMRoot::UI_BeginRemoveVGMFiles() {
  this->UI_BeganRemovingVGMFiles();
}

void QtVGMRoot::UI_EndRemoveVGMFiles() {
  this->UI_EndedRemovingVGMFiles();
}

void QtVGMRoot::UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName,
                           void* UI_specific) {
  auto treeview = static_cast<VGMFileTreeView*>(UI_specific);
  treeview->addVGMItem(item, parent, itemName);
}

std::wstring QtVGMRoot::UI_GetOpenFilePath(const std::wstring&, const std::wstring&) {
  std::wstring path = L"Placeholder";
  return path;
}

std::wstring QtVGMRoot::UI_GetSaveFilePath(const std::wstring& suggested_filename,
                                           const std::wstring& extension) {
  static auto selected_dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  QFileDialog dialog(QApplication::activeWindow());
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.selectFile(QString::fromStdWString(suggested_filename));
  dialog.setDirectory(selected_dir);
  dialog.setAcceptMode(QFileDialog::AcceptSave);

  if (extension == L"mid") {
    dialog.setDefaultSuffix("mid");
    dialog.setNameFilter("Standard MIDI (*.mid)");
  } else if (extension == L"dls") {
    dialog.setDefaultSuffix("dls");
    dialog.setNameFilter("Downloadable Sound (*.dls)");
  } else if (extension == L"sf2") {
    dialog.setDefaultSuffix("sf2");
    dialog.setNameFilter("SoundFont\u00AE 2 (*.sf2)");
  } else {
    dialog.setNameFilter("All files (*)");
  }

  if (dialog.exec()) {
    selected_dir = dialog.directory().absolutePath();
    return dialog.selectedFiles().at(0).toStdWString();
  } else {
    return {};
  }
}

std::wstring QtVGMRoot::UI_GetSaveDirPath(const std::wstring&) {
  static auto selected_dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  QFileDialog dialog(QApplication::activeWindow());
  dialog.setFileMode(QFileDialog::FileMode::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  dialog.setDirectory(selected_dir);

  if (dialog.exec()) {
    selected_dir = dialog.directory().absolutePath();
    return dialog.selectedFiles().at(0).toStdWString();
  } else {
    return {};
  }
}