/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MdiArea.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <VGMFile.h>
#include <VGMColl.h>

#include "VGMFileView.h"
#include "Metrics.h"
#include "QtVGMRoot.h"
#include "services/NotificationCenter.h"
#include "util/TintableSvgIconEngine.h"

namespace {

struct InstructionHint {
  QString iconPath;
  QString text;
  qreal fontScale;
  qreal iconScale;
  qreal spacingScale;
};

struct InstructionMetrics {
  InstructionHint hint;
  QFont font;
  QFontMetrics metrics;
  int iconSide;
  int spacing;
  QSize size;
};

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

QFont prepareFont(const QFont &base, qreal scale) {
  QFont font = base;
  if (font.pointSizeF() > 0) {
    font.setPointSizeF(font.pointSizeF() * scale);
  } else if (font.pixelSize() > 0) {
    font.setPixelSize(static_cast<int>(font.pixelSize() * scale));
  } else {
    font.setPointSize(scale >= 1.5 ? 18 : 14);
  }
  font.setBold(false);
  font.setWeight(QFont::Normal);
  font.setFamily(QStringLiteral("Helvetica Neue"));
  return font;
}

InstructionMetrics computeInstructionMetrics(const InstructionHint &hint, const QFont &baseFont) {
  QFont font = prepareFont(baseFont, hint.fontScale);
  QFontMetrics metrics(font);
  const int iconSide = static_cast<int>(metrics.height() * hint.iconScale);
  const int spacing = std::max(2, static_cast<int>(metrics.height() * hint.spacingScale));
  const int textWidth = metrics.horizontalAdvance(hint.text);
  const int width = std::max(iconSide, textWidth);
  const int height = iconSide + spacing + metrics.height();
  return {hint, font, metrics, iconSide, spacing, QSize(width, height)};
}

QPixmap tintedPixmap(const QString &iconPath, const QSize &size, const QColor &accent) {
  if (size.isEmpty()) {
    return {};
  }

  const QIcon icon(iconPath);
  const QPixmap pixmap = icon.pixmap(size);
  if (pixmap.isNull()) {
    return pixmap;
  }

  QPixmap tinted(size);
  tinted.fill(Qt::transparent);

  QPainter iconPainter(&tinted);
  iconPainter.setRenderHint(QPainter::Antialiasing, true);
  iconPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  iconPainter.drawPixmap(0, 0, pixmap);
  iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  iconPainter.fillRect(tinted.rect(), accent);
  return tinted;
}

void paintInstruction(QPainter &painter, const InstructionMetrics &metrics, const QPoint &topLeft,
                      const QColor &accent) {
  const QSize iconSize(metrics.iconSide, metrics.iconSide);
  const int iconX = topLeft.x() + (metrics.size.width() - metrics.iconSide) / 2;
  const int iconY = topLeft.y();
  const QPixmap icon = tintedPixmap(metrics.hint.iconPath, iconSize, accent);
  if (!icon.isNull()) {
    painter.drawPixmap(iconX, iconY, icon);
  }

  painter.setFont(metrics.font);
  painter.setPen(accent);

  const int textTop = iconY + metrics.iconSide + metrics.spacing;
  const QRect textRect(topLeft.x(), textTop, metrics.size.width(), metrics.metrics.height());
  painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, metrics.hint.text);
}

DetailedInstructionLayout computeDetailedInstructionLayout(const DetailedInstruction &instruction,
                                                           const QFont &baseFont) {
  QFont headingFont = prepareFont(baseFont, 1.8);
  QFont subFont = prepareFont(baseFont, 1.25);
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
  const QPixmap icon = tintedPixmap(layout.instruction.iconPath, iconSize, accent);
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

constexpr int kTabControlSpacing = 2;
constexpr int kTabControlOuterMargin = 4;

QColor tabControlGlyphColor(const QWidget *widget) {
  QColor color = widget ? widget->palette().color(QPalette::WindowText) : QColor(Qt::white);
  color.setAlphaF(0.72);
  return color;
}

QIcon panelButtonIcon(const QColor &tint) {
  return QIcon(new TintableSvgIconEngine(QStringLiteral(":/icons/midi-port.svg"), tint));
}

QIcon iconForPanelView(PanelViewKind viewKind) {
  switch (viewKind) {
    case PanelViewKind::Hex:
      return QIcon(QStringLiteral(":/icons/binary.svg"));
    case PanelViewKind::Tree:
      return QIcon(QStringLiteral(":/icons/file.svg"));
    case PanelViewKind::ActiveNotes:
      return QIcon(QStringLiteral(":/icons/note.svg"));
    case PanelViewKind::PianoRoll:
      return QIcon(QStringLiteral(":/icons/track.svg"));
  }
  return QIcon(QStringLiteral(":/icons/file.svg"));
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
  setupTabBarControls();

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

// Extracts a VGMFileView from an MDI subwindow (or its hosted widget).
VGMFileView *MdiArea::asFileView(QMdiSubWindow *window) {
  if (!window) {
    return nullptr;
  }

  if (auto *fileView = qobject_cast<VGMFileView *>(window)) {
    return fileView;
  }

  return qobject_cast<VGMFileView *>(window->widget());
}

// Lazily creates the right-side tab-strip controls and their menus.
void MdiArea::setupTabBarControls() {
  if (!m_tabBar) {
    m_tabBar = findChild<QTabBar *>();
    if (!m_tabBar) {
      return;
    }

    m_tabBar->setExpanding(false);
    m_tabBar->setUsesScrollButtons(true);
#ifdef Q_OS_MAC
    m_tabBar->setElideMode(Qt::ElideNone);
#endif
    m_tabBar->installEventFilter(this);
    // QMdiArea lays out the tab bar inside a host container; reserve space against that host.
    m_tabBarHost = m_tabBar->parentWidget();
    if (m_tabBarHost) {
      m_tabBarHost->installEventFilter(this);
    }
  }

  if (!m_tabControls) {
    // Keep controls outside the tab bar so we can hard-limit the tab bar width.
    QWidget *controlsParent = m_tabBarHost ? m_tabBarHost : m_tabBar;
    m_tabControls = new QWidget(controlsParent);
    auto *controlsLayout = new QHBoxLayout(m_tabControls);
    controlsLayout->setContentsMargins(0, 2, 0, 2);
    controlsLayout->setSpacing(kTabControlSpacing);

    auto createIconButton = [this](QWidget *parent, const QString &toolTip) {
      auto *button = new QToolButton(parent);
      button->setAutoRaise(false);
      button->setToolButtonStyle(Qt::ToolButtonIconOnly);
      button->setIconSize(QSize(18, 18));
      button->setFixedSize(QSize(30, 18));
      button->setFocusPolicy(Qt::NoFocus);
      button->setToolTip(toolTip);
      return button;
    };

    m_singlePaneButton = createIconButton(m_tabControls, tr("Toggle 1-pane layout"));
    m_singlePaneButton->setCheckable(true);
    controlsLayout->addWidget(m_singlePaneButton);

    m_leftPaneButton = createIconButton(m_tabControls, tr("Select left pane view"));
    m_leftPaneButton->setPopupMode(QToolButton::InstantPopup);
    controlsLayout->addWidget(m_leftPaneButton);

    m_rightPaneButton = createIconButton(m_tabControls, tr("Select right pane view"));
    m_rightPaneButton->setPopupMode(QToolButton::InstantPopup);
    controlsLayout->addWidget(m_rightPaneButton);

    auto createPaneMenu = [this](QToolButton *button, PanelSide side, PaneActions &actions) {
      auto *menu = new QMenu(button);
      auto *group = new QActionGroup(menu);
      group->setExclusive(true);

      auto addAction = [this, menu, group, side](PanelViewKind kind,
                                                  const QString &text,
                                                  QAction *&slot) {
        slot = menu->addAction(iconForPanelView(kind), text);
        slot->setCheckable(true);
        group->addAction(slot);
        connect(slot, &QAction::triggered, this, [this, side, kind]() {
          if (auto *fileView = asFileView(activeSubWindow())) {
            fileView->setPanelView(side, kind);
            updateTabBarControls();
          }
        });
      };

      addAction(PanelViewKind::Hex, tr("Hex"), actions.hex);
      addAction(PanelViewKind::Tree, tr("Tree"), actions.tree);
      addAction(PanelViewKind::ActiveNotes, tr("Active Notes"), actions.activeNotes);
      addAction(PanelViewKind::PianoRoll, tr("Piano Roll"), actions.pianoRoll);

      // Both pane menus expose the same view list; the target pane is captured by `side`.
      button->setMenu(menu);
    };

    createPaneMenu(m_leftPaneButton, PanelSide::Left, m_leftPaneActions);
    createPaneMenu(m_rightPaneButton, PanelSide::Right, m_rightPaneActions);

    connect(m_singlePaneButton, &QToolButton::toggled, this, [this](bool singlePane) {
      if (auto *fileView = asFileView(activeSubWindow())) {
        fileView->setSinglePaneMode(singlePane);
      }
      updateTabBarControls();
    });
  }

  refreshTabControlAppearance();
  applyTabBarStyle();
  repositionTabBarControls();
  updateTabBarControls();
}

// Syncs tab-strip control state with the active VGMFileView.
void MdiArea::updateTabBarControls() {
  if (!m_tabControls || !m_singlePaneButton || !m_leftPaneButton || !m_rightPaneButton) {
    return;
  }

  auto *fileView = asFileView(activeSubWindow());
  const bool hasFileView = fileView != nullptr;

  auto setPaneSelection = [](const PaneActions &actions, PanelViewKind kind) {
    if (actions.hex) {
      actions.hex->setChecked(kind == PanelViewKind::Hex);
    }
    if (actions.tree) {
      actions.tree->setChecked(kind == PanelViewKind::Tree);
    }
    if (actions.activeNotes) {
      actions.activeNotes->setChecked(kind == PanelViewKind::ActiveNotes);
    }
    if (actions.pianoRoll) {
      actions.pianoRoll->setChecked(kind == PanelViewKind::PianoRoll);
    }
  };

  auto setSeqOnlyEnabled = [](PaneActions &actions, bool enabled) {
    if (actions.activeNotes) {
      actions.activeNotes->setEnabled(enabled);
    }
    if (actions.pianoRoll) {
      actions.pianoRoll->setEnabled(enabled);
    }
  };

  m_singlePaneButton->setEnabled(hasFileView);
  m_leftPaneButton->setEnabled(hasFileView);

  if (!hasFileView) {
    // No active file: keep controls visible but inert.
    QSignalBlocker blocker(m_singlePaneButton);
    m_singlePaneButton->setChecked(false);
    m_rightPaneButton->setEnabled(false);
    setSeqOnlyEnabled(m_leftPaneActions, false);
    setSeqOnlyEnabled(m_rightPaneActions, false);
    return;
  }

  const bool singlePane = fileView->singlePaneMode();
  {
    QSignalBlocker blocker(m_singlePaneButton);
    m_singlePaneButton->setChecked(singlePane);
  }

  setPaneSelection(m_leftPaneActions, fileView->panelView(PanelSide::Left));
  setPaneSelection(m_rightPaneActions, fileView->panelView(PanelSide::Right));

  const bool sequenceViewsAvailable = fileView->supportsSequenceViews();
  setSeqOnlyEnabled(m_leftPaneActions, sequenceViewsAvailable);
  setSeqOnlyEnabled(m_rightPaneActions, sequenceViewsAvailable);

  m_rightPaneButton->setEnabled(!singlePane);
}

// Repositions controls and reserves layout space so tabs never overlap them.
void MdiArea::repositionTabBarControls() {
  if (!m_tabBar || !m_tabControls) {
    return;
  }

  QWidget *host = m_tabBarHost ? m_tabBarHost : m_tabBar->parentWidget();
  if (!host) {
    return;
  }

  const QSize controlsSize = m_tabControls->sizeHint();
  const int reservedRightMargin = controlsSize.width() + (kTabControlOuterMargin * 2);
  if (m_reservedTabBarRightMargin != reservedRightMargin) {
    m_reservedTabBarRightMargin = reservedRightMargin;
  }

  QRect tabGeometry = m_tabBar->geometry();
  // Reserve a fixed strip on the right so tab text and overflow arrows stop before the controls.
  const int targetTabWidth = std::max(0, host->width() - tabGeometry.x() - m_reservedTabBarRightMargin);
  if (tabGeometry.width() != targetTabWidth) {
    tabGeometry.setWidth(targetTabWidth);
    m_tabBar->setGeometry(tabGeometry);
  } else {
    tabGeometry = m_tabBar->geometry();
  }

  const int x = std::max(0, host->width() - controlsSize.width() - kTabControlOuterMargin);
  const int y = std::max(0, tabGeometry.y() + (tabGeometry.height() - controlsSize.height()) / 2);
  m_tabControls->setGeometry(x, y, controlsSize.width(), controlsSize.height());
  m_tabControls->raise();
}

// Applies the flat, stencil-like visual style used by the tab-strip controls.
void MdiArea::refreshTabControlAppearance() {
  if (!m_tabControls) {
    return;
  }

  const QString controlsStyle = QStringLiteral(
      "QToolButton {"
      " border: none;"
      " background: transparent;"
      " padding: 0px;"
      " margin: 0px;"
      "}"
      "QToolButton:hover { background: transparent; }"
      "QToolButton:pressed { background: transparent; }"
      "QToolButton:checked { background: transparent; }"
      "QToolButton::menu-indicator { image: none; width: 0px; }");
  if (m_tabControls->styleSheet() != controlsStyle) {
    m_tabControls->setStyleSheet(controlsStyle);
  }

  const QIcon glyphIcon = panelButtonIcon(tabControlGlyphColor(m_tabControls));
  if (m_singlePaneButton) {
    m_singlePaneButton->setIcon(glyphIcon);
  }
  if (m_leftPaneButton) {
    m_leftPaneButton->setIcon(glyphIcon);
  }
  if (m_rightPaneButton) {
    m_rightPaneButton->setIcon(glyphIcon);
  }
}

// Applies tab-bar styling in an idempotent way to avoid repolish loops.
void MdiArea::applyTabBarStyle() {
  if (!m_tabBar) {
    return;
  }

  const QString styleSheet =
      QStringLiteral("QTabBar::tab { height: %1px; }").arg(Size::VTab);
  // Re-setting the same stylesheet can trigger repolish churn and recursive style events.
  if (m_tabBar->styleSheet() == styleSheet) {
    return;
  }
  m_tabBar->setStyleSheet(styleSheet);
}

void MdiArea::changeEvent(QEvent *event) {
  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::ApplicationPaletteChange) {
    updateBackgroundColor();
    refreshTabControlAppearance();
  }
  QMdiArea::changeEvent(event);
}

bool MdiArea::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_tabBar || watched == m_tabBarHost) {
    // Track host + tab-bar layout changes to keep the reserved right strip aligned.
    switch (event->type()) {
      case QEvent::Show:
      case QEvent::Resize:
      case QEvent::Move:
      case QEvent::LayoutRequest:
      case QEvent::StyleChange:
        repositionTabBarControls();
        break;
      default:
        break;
    }
  }

  return QMdiArea::eventFilter(watched, event);
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
  setupTabBarControls();

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

  updateTabBarControls();
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

  updateTabBarControls();
}

void MdiArea::onSubWindowActivated(QMdiSubWindow *window) {
  setupTabBarControls();

  if (!window) {
    updateTabBarControls();
    return;
  }

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

  updateTabBarControls();
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

  updateTabBarControls();
}

void MdiArea::ensureMaximizedSubWindow(QMdiSubWindow *window) {
  if (window && !window->isMaximized()) {
    window->showMaximized();
  }
}
