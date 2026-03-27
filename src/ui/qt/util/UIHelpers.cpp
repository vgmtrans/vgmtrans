/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "UIHelpers.h"
#include "TintableSvgIconEngine.h"
#include <QIcon>
#include <QWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QStyle>
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

int horizontalScrollBarReservedHeight(const QAbstractScrollArea* area) {
  const QScrollBar* hbar = area->horizontalScrollBar();

  const int extent = hbar->sizeHint().height();
  const int overlap = hbar->style()->pixelMetric(
      QStyle::PM_ScrollView_ScrollBarOverlap,
      nullptr,
      hbar
  );

  return std::max(0, extent - overlap);
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

QIcon stencilSvgIcon(const QString &iconPath, const QColor &color) {
  return QIcon(new TintableSvgIconEngine(iconPath, color));
}

QIcon gradientStencilSvgIcon(const QString &iconPath, const QColor &startColor, const QColor &endColor,
                             int angleDegrees) {
  return QIcon(new TintableSvgIconEngine(iconPath, startColor, endColor, angleDegrees));
}

QPixmap tintedIconPixmap(const QIcon &icon, const QSize &size, const QColor &color, qreal devicePixelRatio) {
  if (size.isEmpty()) {
    return {};
  }

  QPixmap pixmap = icon.pixmap(size, std::max(devicePixelRatio, qreal(1.0)));
  if (pixmap.isNull()) {
    return pixmap;
  }

  QPainter painter(&pixmap);
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(pixmap.rect(), color);
  return pixmap;
}

QString cssColor(const QColor &color) {
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

QColor blendColors(const QColor &foreground, const QColor &background, qreal foregroundWeight) {
  const qreal backgroundWeight = 1.0 - foregroundWeight;
  return QColor::fromRgbF(foreground.redF() * foregroundWeight + background.redF() * backgroundWeight,
                          foreground.greenF() * foregroundWeight + background.greenF() * backgroundWeight,
                          foreground.blueF() * foregroundWeight + background.blueF() * backgroundWeight,
                          foreground.alphaF() * foregroundWeight + background.alphaF() * backgroundWeight);
}

bool isDarkPalette(const QPalette &palette) {
  return palette.color(QPalette::Window).lightnessF() < 0.5;
}

void configureToolButton(QToolButton *button, const QString &toolTip, const QSize &buttonSize,
                         const QSize &iconSize, bool textOnly) {
  if (!button) {
    return;
  }

  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setToolTip(toolTip);
  button->setCursor(Qt::ArrowCursor);
  button->setToolButtonStyle(textOnly ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);

  if (buttonSize.isValid()) {
    button->setFixedSize(buttonSize);
  }

  if (iconSize.isValid()) {
    button->setIconSize(iconSize);
  }
}

QString toolBarButtonStyle(const QPalette &palette, bool checkable) {
  const bool darkPalette = isDarkPalette(palette);
  QColor hoverFill = palette.color(QPalette::Text);
  hoverFill.setAlpha(darkPalette ? 18 : 12);
  QColor pressedFill = palette.color(QPalette::Text);
  pressedFill.setAlpha(darkPalette ? 28 : 20);

  QString style = QStringLiteral(
      "QToolButton {"
      " border: none;"
      " background: transparent;"
      " border-radius: 6px;"
      " padding: 0px;"
      " margin: 0px;"
      "}"
      "QToolButton:hover { background: %1; }"
      "QToolButton:pressed { background: %2; }")
                      .arg(cssColor(hoverFill))
                      .arg(cssColor(pressedFill));

  if (checkable) {
    QColor checkedFill = palette.color(QPalette::Text);
    checkedFill.setAlpha(darkPalette ? 38 : 33);
    style += QStringLiteral("QToolButton:checked { background: %1; }").arg(cssColor(checkedFill));
  }

  return style;
}

QColor toolBarButtonIconColor(const QPalette &palette, bool enabled) {
  const QColor windowColor = palette.color(QPalette::Window);
  const bool darkPalette = isDarkPalette(palette);
  const QColor textColor =
      enabled ? palette.color(QPalette::Text) : palette.color(QPalette::Disabled, QPalette::Text);
  return blendColors(textColor, windowColor, enabled ? (darkPalette ? 0.72 : 0.56) : (darkPalette ? 0.6 : 0.46));
}

void refreshStencilToolButton(QToolButton *button, const QString &iconPath, const QPalette &palette,
                              bool checkable) {
  if (!button) {
    return;
  }

  button->setStyleSheet(toolBarButtonStyle(palette, checkable));
  button->setIcon(stencilSvgIcon(iconPath, toolBarButtonIconColor(palette, button->isEnabled())));
}

QString toolBarTextButtonStyle(const QPalette &palette, int leftMargin) {
  return QStringLiteral(
      "QToolButton { border: none; background: transparent; padding: 0px; margin: 0px 0px 0px %1px; color: %2; }"
      "QToolButton::menu-indicator { image: none; width: 0px; }")
      .arg(leftMargin)
      .arg(cssColor(toolBarButtonIconColor(palette)));
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

std::filesystem::path openFolderDialog(const std::filesystem::path& suggestedPath,
                                       std::string_view reason) {
  QFileDialog dialog(QApplication::activeWindow());
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  dialog.setWindowTitle(QString::fromUtf8(reason.data(), static_cast<int>(reason.size())));
  dialog.setDirectory(QString::fromStdWString(suggestedPath.wstring()));

  if (dialog.exec()) {
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

  dialog.selectFile(QString::fromStdString(suggested_filename.string()));

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
