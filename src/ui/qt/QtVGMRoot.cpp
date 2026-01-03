/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */


#include "VGMFileTreeView.h"
#include "UIHelpers.h"
#include <QApplication>
#include <QFileDialog>
#include <QString>
#include "QtVGMRoot.h"

QtVGMRoot qtVGMRoot;

std::filesystem::path QtVGMRoot::UI_getResourceDirPath() {
  auto toPath = [](const QString& qstr) {
    auto utf8 = qstr.toUtf8();
    std::u8string u8;
    u8.reserve(utf8.size());
    for (auto ch : utf8) {
      u8.push_back(static_cast<char8_t>(ch));
    }
    return std::filesystem::path(u8);
  };
#if defined(Q_OS_WIN)
  return toPath(QApplication::applicationDirPath() + "/");
#elif defined(Q_OS_MACOS)
  return toPath(QApplication::applicationDirPath() + "/../Resources/");
#elif defined(Q_OS_LINUX)
  return toPath(QApplication::applicationDirPath() + "/");
#else
  return toPath(QApplication::applicationDirPath() + "/");
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
