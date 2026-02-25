/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileTreeView.h"
#include "UIHelpers.h"
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>
#include <filesystem>
#include "QtVGMRoot.h"

QtVGMRoot qtVGMRoot;

std::filesystem::path QtVGMRoot::UI_getResourceDirPath() {
  std::filesystem::path appDir = std::filesystem::path(QApplication::applicationDirPath().toStdWString());

#if defined(Q_OS_MACOS)
  std::filesystem::path resDir = (appDir / ".." / "Resources").lexically_normal();
  if (std::filesystem::exists(resDir / "mame_roms.json")) {
    return resDir;
  }
#endif

#if defined(VGMTRANS_DATA_DIR)
  std::filesystem::path dataDir = std::filesystem::path(VGMTRANS_DATA_DIR);
  if (std::filesystem::exists(dataDir / "mame_roms.json")) {
    return dataDir;
  }
#endif

#if defined(DEV_ENV_BUILD_TREE)
  std::filesystem::path devDir = std::filesystem::path(DEV_ENV_BUILD_TREE);
  if (std::filesystem::exists(devDir / "mame_roms.json")) {
    return devDir;
  }
#endif

  return appDir;
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

std::filesystem::path QtVGMRoot::UI_openFolder(const std::filesystem::path& suggestedPath,
                                               std::string_view reason) {
  return openFolderDialog(suggestedPath, reason);
}

bool QtVGMRoot::openRawFileWithAccessRetry(const std::filesystem::path& requestedPath) {
  enum class FileAccess {
    Missing,
    Readable,
    NotReadable
  };

  auto toQString = [](const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
  };

  auto getFileAccess = [&toQString](const std::filesystem::path& path) {
    const QFileInfo info(toQString(path));
    if (!info.exists() || !info.isFile()) {
      return FileAccess::Missing;
    }

    QFile file(info.filePath());
    if (file.open(QIODevice::ReadOnly)) {
      return FileAccess::Readable;
    }

    return FileAccess::NotReadable;
  };

  auto toastOpenError = [this, &toQString](const std::filesystem::path& path) {
    const QString message = QStringLiteral("Error opening file at path: %1").arg(toQString(path));
    UI_toast(message.toUtf8().toStdString(), ToastType::Error);
  };

  const FileAccess requestedAccess = getFileAccess(requestedPath);
  if (requestedAccess == FileAccess::Readable) {
    return openRawFile(requestedPath);
  }

  if (requestedAccess == FileAccess::Missing) {
    return openRawFile(requestedPath);
  }

  QMessageBox prompt(QMessageBox::Icon::Warning,
                     "Permission required",
                     "VGMTrans needs access to the folder containing this file.",
                     QMessageBox::Cancel,
                     QApplication::activeWindow());
  auto* grantButton = prompt.addButton("Grant Access", QMessageBox::AcceptRole);
  prompt.setInformativeText(toQString(requestedPath));
  prompt.exec();

  if (prompt.clickedButton() != grantButton) {
    toastOpenError(requestedPath);
    return false;
  }

  const QString filename = toQString(requestedPath.filename());
  const QString reason = QStringLiteral("Select the folder containing '%1'").arg(filename);
  const std::filesystem::path chosenFolder =
      UI_openFolder(requestedPath.parent_path(), reason.toUtf8().toStdString());

  if (chosenFolder.empty()) {
    toastOpenError(requestedPath);
    return false;
  }

  if (getFileAccess(requestedPath) == FileAccess::Readable) {
    return openRawFile(requestedPath);
  }

  const std::filesystem::path retryPath = chosenFolder / requestedPath.filename();
  if (getFileAccess(retryPath) == FileAccess::Readable) {
    return openRawFile(retryPath);
  }

  toastOpenError(requestedPath);
  return false;
}
