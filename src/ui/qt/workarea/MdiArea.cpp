/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MdiArea.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QShortcut>
#include <QTabBar>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <VGMFile.h>
#include <VGMColl.h>

#include "VGMFileView.h"
#include "InstructionHintLayout.h"
#include "Metrics.h"
#include "QtVGMRoot.h"
#include "UIHelpers.h"
#include "services/NotificationCenter.h"

namespace {

struct DetailedInstruction {
  QString iconPath;
  QString heading;
  QString subText;
};

struct DetailedInstructionLayout {
  DetailedInstruction instruction;
  QFont headingFont;
  QFont subFont;
  QFontMetrics headingMetrics;
  QFontMetrics subMetrics;
  int iconSide = 0;
  int iconGap = 0;
  int headingLeft = 0;
  int subTopOffset = 0;
  int rowHeight = 0;
  int rowWidth = 0;
  int rowSpacing = 0;

  DetailedInstructionLayout(const DetailedInstruction &instruction, QFont headingFont,
                            QFont subFont)
      : instruction(instruction),
        headingFont(std::move(headingFont)),
        subFont(std::move(subFont)),
        headingMetrics(this->headingFont),
        subMetrics(this->subFont) {}
};

DetailedInstructionLayout computeDetailedInstructionLayout(const DetailedInstruction &instruction,
                                                           const QFont &baseFont) {
  QFont headingFont = prepareInstructionFont(baseFont, 1.8, QFont::Normal, 0,
                                             QStringLiteral("Helvetica Neue"));
  QFont subFont = prepareInstructionFont(baseFont, 1.25, QFont::Normal, 0,
                                         QStringLiteral("Helvetica Neue"));
  DetailedInstructionLayout layout(instruction, headingFont, subFont);

  layout.iconSide = std::max(layout.headingMetrics.height(),
                             static_cast<int>(std::round(layout.headingMetrics.height() * 1.1)));
  layout.iconGap = std::max(8, static_cast<int>(std::round(layout.headingMetrics.height() * 0.45)));
  layout.headingLeft = layout.iconSide + layout.iconGap;

  const int subSpacing = std::max(6, static_cast<int>(std::round(layout.headingMetrics.height() * 0.35)));
  layout.subTopOffset = layout.headingMetrics.height() + subSpacing;

  const int headingWidth = layout.headingMetrics.horizontalAdvance(instruction.heading);
  const int subWidth = layout.subMetrics.horizontalAdvance(instruction.subText);
  const int textWidth = std::max(headingWidth, subWidth);
  layout.rowWidth = layout.headingLeft + textWidth;
  layout.rowHeight = layout.subTopOffset + layout.subMetrics.height();
  layout.rowSpacing = std::max(35, static_cast<int>(std::round(layout.headingMetrics.height() * 0.6)));
  return layout;
}

void paintDetailedInstruction(QPainter &painter, const DetailedInstructionLayout &layout,
                              const QPoint &origin, const QColor &accent, int blockWidth) {
  const int rowLeft = origin.x();
  const int headingTop = origin.y();
  const QSize iconSize(layout.iconSide, layout.iconSide);
  const qreal devicePixelRatio =
      painter.device() ? painter.device()->devicePixelRatioF() : qreal(1.0);
  const QPixmap icon = tintedIconPixmap(QIcon(layout.instruction.iconPath), iconSize, accent,
                                        devicePixelRatio);
  if (!icon.isNull()) {
    const int iconY = headingTop + (layout.headingMetrics.height() - layout.iconSide) / 2;
    painter.drawPixmap(rowLeft, iconY, icon);
  }

  painter.setFont(layout.headingFont);
  painter.setPen(accent);
  const QRect headingRect(rowLeft + layout.headingLeft, headingTop,
                          blockWidth - layout.headingLeft, layout.headingMetrics.height());
  painter.drawText(headingRect, Qt::AlignLeft | Qt::AlignTop, layout.instruction.heading);
  painter.setFont(layout.subFont);
  const QRect subRect(rowLeft + layout.headingLeft, headingTop + layout.subTopOffset,
                      blockWidth - layout.headingLeft, layout.subMetrics.height());
  painter.drawText(subRect, Qt::AlignLeft | Qt::AlignTop, layout.instruction.subText);
}

} // namespace

MdiArea::MdiArea(QWidget *parent) : QMdiArea(parent) {
  setViewMode(QMdiArea::TabbedView);
  setDocumentMode(true);
  setTabsMovable(true);
  setTabsClosable(true);
  updateBackgroundColor();

  connect(this, &QMdiArea::subWindowActivated, this, &MdiArea::onSubWindowActivated);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &MdiArea::onVGMFileSelected);
  connect(&qtVGMRoot, &QtVGMRoot::UI_addedRawFile, this, [this]() { viewport()->update(); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_endRemoveRawFiles, this, [this]() { viewport()->update(); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_addedVGMColl, this, [this]() { viewport()->update(); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_endRemoveVGMColls, this,[this]() { viewport()->update(); });
  connect(&qtVGMRoot, &QtVGMRoot::UI_removeVGMFile, this, [this](const VGMFile *file) { removeView(file); });

  if (auto *tab_bar = findChild<QTabBar *>()) {
    tab_bar->setStyleSheet(QString{"QTabBar::tab { height: %1; }"}.arg(Size::VTab));
    tab_bar->setExpanding(false);
    tab_bar->setUsesScrollButtons(true);
#ifdef Q_OS_MAC
    tab_bar->setElideMode(Qt::ElideNone);
#endif
  }

  // Create OS-specific keyboard shortcuts
  auto addShortcut = [this](const QKeySequence &seq, auto slot)
  {
    auto *sc = new QShortcut(seq, this);
    sc->setContext(Qt::WindowShortcut);
    connect(sc, &QShortcut::activated, this, slot);
  };

#ifdef Q_OS_MAC
  // Cmd ⇧ [  /  Cmd ⇧ ]
  addShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft),
              &QMdiArea::activatePreviousSubWindow);
  addShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight),
              &QMdiArea::activateNextSubWindow);

  // Cmd ⌥ ←  /  Cmd ⌥ →
  addShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left),
              &QMdiArea::activatePreviousSubWindow);
  addShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right),
              &QMdiArea::activateNextSubWindow);

  // Ctrl + PageDown / Ctrl + PageUp
  addShortcut(QKeySequence(Qt::META | Qt::Key_PageDown),
              &QMdiArea::activateNextSubWindow);
  addShortcut(QKeySequence(Qt::META | Qt::Key_PageUp),
              &QMdiArea::activatePreviousSubWindow);
#else   // Windows & Linux
  // Ctrl + Tab  /  Ctrl + Shift + Tab
  addShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab),
              &QMdiArea::activateNextSubWindow);
  addShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
              &QMdiArea::activatePreviousSubWindow);

  // Ctrl + PageDown / Ctrl + PageUp
  addShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown),
              &QMdiArea::activateNextSubWindow);
  addShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp),
              &QMdiArea::activatePreviousSubWindow);
#endif
}

void MdiArea::changeEvent(QEvent *event) {
  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::ApplicationPaletteChange) {
    updateBackgroundColor();
  }
  QMdiArea::changeEvent(event);
}

void MdiArea::paintEvent(QPaintEvent *event) {
  QMdiArea::paintEvent(event);

  if (!subWindowList().isEmpty()) {
    return;
  }

  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  QColor accent = palette().color(QPalette::WindowText);
  accent.setAlphaF(0.4);

  const bool hasRawFiles = !qtVGMRoot.rawFiles().empty();
  const bool hasCollections = !qtVGMRoot.vgmColls().empty();
  const QRect areaRect = viewport()->rect();
  const QFont baseFont = painter.font();

  if (!hasRawFiles) {
    InstructionHint dropHint{QStringLiteral(":/icons/tray-arrow-down.svg"), tr("Drop files to scan"),
                             2, 4.0, 0.3};
    const InstructionMetrics metrics = computeInstructionMetrics(dropHint, baseFont);
    const int left = areaRect.center().x() - metrics.size.width() / 2;
    const int top = areaRect.center().y() - metrics.size.height() / 2;
    paintInstruction(painter, metrics, QPoint(left, top), accent);
    return;
  }

  std::vector<DetailedInstruction> instructions;
  instructions.reserve(3);
  if (hasCollections) {
    instructions.push_back({QStringLiteral(":/icons/volume-high.svg"), tr("Preview"),
                            tr("Double-click a collection")});
  }
  instructions.push_back({QStringLiteral(":/icons/midi-port.svg"), tr("Convert"),
                          tr("Right-click a detected file or collection")});
  instructions.push_back({QStringLiteral(":/icons/magnify.svg"), tr("Analyze"),
                          tr("Double-click a detected file")});

  std::vector<DetailedInstructionLayout> layouts;
  layouts.reserve(instructions.size());
  int blockWidth = 0;
  int blockHeight = 0;
  for (const auto &instruction : instructions) {
    DetailedInstructionLayout layout = computeDetailedInstructionLayout(instruction, baseFont);
    blockWidth = std::max(blockWidth, layout.rowWidth);
    if (!layouts.empty()) {
      blockHeight += layouts.back().rowSpacing;
    }
    blockHeight += layout.rowHeight;
    layouts.push_back(std::move(layout));
  }

  if (layouts.empty()) {
    return;
  }

  const int blockLeft = areaRect.center().x() - blockWidth / 2;
  const int blockTop = areaRect.center().y() - blockHeight / 2;

  int currentY = blockTop;
  for (std::size_t i = 0; i < layouts.size(); ++i) {
    paintDetailedInstruction(painter, layouts[i], QPoint(blockLeft, currentY), accent, blockWidth);
    currentY += layouts[i].rowHeight;
    if (i + 1 < layouts.size()) {
      currentY += layouts[i].rowSpacing;
    }
  }
}

void MdiArea::updateBackgroundColor() {
  setBackground(palette().color(QPalette::Window));
}

void MdiArea::newView(VGMFile *file) {
  auto it = fileToWindowMap.find(file);
  // Check if a fileview for this vgmfile already exists
  if (it != fileToWindowMap.end()) {
    // If it does, let's focus it
    auto *vgmfile_view = it->second;
    vgmfile_view->setFocus();
  } else {
    // No VGMFileView could be found, we have to make one
    auto *vgmfile_view = new VGMFileView(file);
    auto tab = addSubWindow(vgmfile_view, Qt::SubWindow);
    fileToWindowMap.insert(std::make_pair(file, tab));
    windowToFileMap.insert(std::make_pair(tab, file));
    tab->showMaximized();
    tab->setFocus();

#ifdef Q_OS_MAC
    auto newTitle = " " + vgmfile_view->windowTitle() + " ";
    vgmfile_view->setWindowTitle(newTitle);
#endif
  }
}

void MdiArea::removeView(const VGMFile *file) {
  // Let's check if we have a VGMFileView to remove
  auto it = fileToWindowMap.find(file);
  if (it != fileToWindowMap.end()) {
    // Sanity check
    if (it->second) {
      // Close the tab (automatically deletes it)
      // Workaround for QTBUG-5446 (removeMdiSubWindow would be a better option)
      it->second->close();
    }
    // Get rid of the saved pointers
    windowToFileMap.erase(it->second);
    fileToWindowMap.erase(file);
  }
}

void MdiArea::onSubWindowActivated(QMdiSubWindow *window) {
  if (!window)
    return;

  // For some reason, if multiple documents are open, closing one document causes the others
  // to become windowed instead of maximized. This fixes the problem.
  ensureMaximizedSubWindow(window);

  // Another quirk: paintEvents for all subWindows, not just the active one, are fired
  // unless we manually hide them.
  for (auto subWindow : subWindowList()) {
    subWindow->widget()->setHidden(subWindow != window);
  }

  if (window) {
    auto it = windowToFileMap.find(window);
    if (it != windowToFileMap.end()) {
      VGMFile *file = it->second;
      NotificationCenter::the()->selectVGMFile(file, this);
    }
  }
}

void MdiArea::onVGMFileSelected(const VGMFile *file, QWidget *caller) {
  if (caller == this || file == nullptr)
    return;

  auto it = fileToWindowMap.find(file);
  if (it != fileToWindowMap.end()) {

    QWidget* focusedWidget = QApplication::focusWidget();
    bool callerHadFocus = focusedWidget && caller && caller->isAncestorOf(focusedWidget);
    QMdiSubWindow *window = it->second;
    setActiveSubWindow(window);

    // Reassert the focus back to the caller
    if (caller && callerHadFocus) {
      caller->setFocus();
    }
  }
}

void MdiArea::ensureMaximizedSubWindow(QMdiSubWindow *window) {
  if (window && !window->isMaximized()) {
    window->showMaximized();
  }
}
