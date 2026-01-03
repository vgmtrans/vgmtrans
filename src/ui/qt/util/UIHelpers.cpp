/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "UIHelpers.h"
#include <QWidget>
#include <QScrollArea>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPainter>
#include <QFileDialog>
#include <QStandardPaths>
#include <QApplication>

QScrollArea* getContainingScrollArea(const QWidget* widget) {
  QWidget* viewport = widget->parentWidget();
  if (viewport) {
    auto* scrollArea = qobject_cast<QScrollArea*>(viewport->parentWidget());
    if (scrollArea && scrollArea->viewport() == viewport) {
      return scrollArea;
    }
  }
  return nullptr;
}

void applyEffectToPixmap(QPixmap &src, QPixmap &tgt, QGraphicsEffect *effect, int extent)
{
  if(src.isNull()) return;  // Check if src is valid
  if(!effect) return;       // Check if effect is valid

  QGraphicsScene scene;
  QGraphicsPixmapItem item;
  item.setPixmap(src);
  item.setGraphicsEffect(effect);
  scene.addItem(&item);

  tgt.fill(Qt::transparent);
  QPainter ptr(&tgt);
  scene.render(&ptr, QRectF(), QRectF(-extent, -extent, src.width() + extent*2, src.height() + extent*2));
}

std::filesystem::path openSaveDirDialog() {
  static QString selected_dir =QStandardPaths::writableLocation(QStandardPaths::MusicLocation);

  QFileDialog dialog(QApplication::activeWindow());
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  dialog.setDirectory(selected_dir);

  if (dialog.exec()) {
    selected_dir = dialog.directory().absolutePath();
    const QString chosen = dialog.selectedFiles().at(0);
    return std::filesystem::path(chosen.toStdWString());
  }
  return {};
}

std::filesystem::path openSaveFileDialog(const std::filesystem::path& suggested_filename, const std::string& extension) {
  static QString selected_dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);

  QFileDialog dialog(QApplication::activeWindow());
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setDirectory(selected_dir);

  // Suggested filename -> QString
  dialog.selectFile(QString::fromStdString(suggested_filename.string()));

  // Filters / suffixes
  if (extension == "mid") {
    dialog.setDefaultSuffix("mid");
    dialog.setNameFilter("Standard MIDI (*.mid)");
  } else if (extension == "dls") {
    dialog.setDefaultSuffix("dls");
    dialog.setNameFilter("Downloadable Sound (*.dls)");
  } else if (extension == "sf2") {
    dialog.setDefaultSuffix("sf2");
    dialog.setNameFilter("SoundFont\u00AE 2 (*.sf2)");
  } else {
    dialog.setNameFilter("All files (*)");
  }

  if (dialog.exec()) {
    selected_dir = dialog.directory().absolutePath();

    const QString chosen = dialog.selectedFiles().at(0);
    return std::filesystem::path(chosen.toStdWString());
  }

  return {};
}