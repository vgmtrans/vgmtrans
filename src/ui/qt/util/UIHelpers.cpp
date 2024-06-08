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

QScrollArea* getContainingScrollArea(QWidget* widget) {
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


std::string openSaveDirDialog() {
  static auto selected_dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  QFileDialog dialog(QApplication::activeWindow());
  dialog.setFileMode(QFileDialog::FileMode::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  dialog.setDirectory(selected_dir);

  if (dialog.exec()) {
    selected_dir = dialog.directory().absolutePath();
    return dialog.selectedFiles().at(0).toStdString();
  } else {
    return {};
  }
}


std::string openSaveFileDialog(const std::string& suggested_filename, const std::string& extension) {
  static auto selected_dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  QFileDialog dialog(QApplication::activeWindow());
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.selectFile(QString::fromStdString(suggested_filename));
  dialog.setDirectory(selected_dir);
  dialog.setAcceptMode(QFileDialog::AcceptSave);

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
    return dialog.selectedFiles().at(0).toStdString();
  } else {
    return {};
  }
}