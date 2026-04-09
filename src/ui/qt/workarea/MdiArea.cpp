/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MdiArea.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QMetaObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPoint>
#include <QScopedValueRollback>
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
#include "UIHelpers.h"
#include "services/NotificationCenter.h"
#include "util/TintableSvgIconEngine.h"
#ifdef Q_OS_LINUX
#include "widgets/WaylandMenuToolButton.h"
#endif

namespace {

class TabControlStrip final : public QWidget {
public:
  explicit TabControlStrip(QWidget *parent = nullptr) : QWidget(parent) {}

  void setBackgroundColor(QColor backgroundColor) {
    if (m_backgroundColor == backgroundColor) {
      return;
    }
    m_backgroundColor = std::move(backgroundColor);
    update();
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), m_backgroundColor.isValid() ? m_backgroundColor
                                                         : palette().color(QPalette::Window));
  }

private:
  QColor m_backgroundColor;
};

struct FlatTabBarColors {
  QColor stripBackground;
  QColor activeTabBackground;
  QColor inactiveTabBackground;
  QColor hoveredInactiveTabBackground;
  QColor activeText;
  QColor inactiveText;
  QColor disabledText;
  QColor hoverFill;
  QColor pressedFill;
};

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

FlatTabBarColors flatTabBarColors(const QWidget *context) {
  const QWidget *paletteSource = context && context->window() ? context->window() : context;
  const QPalette palette = paletteSource ? paletteSource->palette() : qApp->palette();
  auto colorFor = [&](QPalette::ColorRole primary,
                      QPalette::ColorRole fallback,
                      const QColor &defaultColor = QColor()) {
    QColor color = palette.color(primary);
    if (!color.isValid()) {
      color = palette.color(fallback);
    }
    return color.isValid() ? color : defaultColor;
  };
  auto readableTextFor = [](const QColor &background, const QColor &preferred) {
    QColor text = preferred;
    if (!text.isValid()) {
      text = qGray(background.rgb()) < 128 ? QColor(244, 244, 244) : QColor(32, 32, 32);
    }
    if ((qGray(background.rgb()) < 128) == (qGray(text.rgb()) < 128)) {
      text = qGray(background.rgb()) < 128 ? QColor(244, 244, 244) : QColor(32, 32, 32);
    }
    return text;
  };
  auto withAlpha = [](QColor color, int alpha) {
    color.setAlpha(alpha);
    return color;
  };

  const QColor stripBackground = colorFor(QPalette::Window, QPalette::Base, QColor(48, 48, 48));
  const QColor activeTabBackground = colorFor(QPalette::Button, QPalette::Window, stripBackground);
  const bool darkPalette = qGray(activeTabBackground.rgb()) < 128;
  const QColor inactiveTabBackground =
      darkPalette ? activeTabBackground.darker(152) : activeTabBackground.darker(116);
  const QColor hoveredInactiveTabBackground =
      darkPalette ? inactiveTabBackground.lighter(108) : inactiveTabBackground.lighter(104);
  QColor textColor = colorFor(QPalette::ButtonText, QPalette::WindowText);
  const QColor activeText = readableTextFor(activeTabBackground, textColor);
  const QColor inactiveText = readableTextFor(inactiveTabBackground, textColor);
  const QColor interactionColor = readableTextFor(inactiveTabBackground, textColor);

  return {
      inactiveTabBackground,
      activeTabBackground,
      inactiveTabBackground,
      hoveredInactiveTabBackground,
      activeText,
      withAlpha(inactiveText, 170),
      withAlpha(inactiveText, 96),
      withAlpha(interactionColor, 24),
      withAlpha(interactionColor, 40),
  };
}
void paintInstruction(QPainter &painter, const InstructionMetrics &metrics, const QPoint &topLeft,
                      const QColor &accent) {
  const QSize iconSize(metrics.iconSide, metrics.iconSide);
  const int iconX = topLeft.x() + (metrics.size.width() - metrics.iconSide) / 2;
  const int iconY = topLeft.y();
  const qreal devicePixelRatio =
      painter.device() ? painter.device()->devicePixelRatioF() : qreal(1.0);
  const QPixmap icon =
      tintedIconPixmap(QIcon(metrics.hint.iconPath), iconSize, accent, devicePixelRatio);
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

constexpr int kTabControlSpacing = 8;
constexpr int kTabControlOuterMargin = 8;

// Replace these paths when final per-view artwork is ready.
const QString kLeftPaneButtonIconPath = QStringLiteral(":/icons/left-pane.svg");
const QString kRightPaneButtonIconPath = QStringLiteral(":/icons/right-pane.svg");
const QString kSequenceControlBarButtonIconPath = QStringLiteral(":/icons/control.svg");
const QString kHexViewIconPath = QStringLiteral(":/icons/binary.svg");
const QString kTreeViewIconPath = QStringLiteral(":/icons/file.svg");
const QString kActiveNotesViewIconPath = QStringLiteral(":/icons/note.svg");
const QString kPianoRollViewIconPath = QStringLiteral(":/icons/track.svg");
const QString kHiddenRightPaneIconPath = QStringLiteral(":/icons/view-split-vertical.svg");

QString iconPathForPanelView(PanelViewKind viewKind) {
  switch (viewKind) {
    case PanelViewKind::Hex:
      return kHexViewIconPath;
    case PanelViewKind::Tree:
      return kTreeViewIconPath;
    case PanelViewKind::ActiveNotes:
      return kActiveNotesViewIconPath;
    case PanelViewKind::PianoRoll:
      return kPianoRollViewIconPath;
  }
  return kTreeViewIconPath;
}

QIcon panelButtonIcon(const QString &iconPath, const QColor &tint) {
  return QIcon(new TintableSvgIconEngine(iconPath, tint));
}

QIcon iconForPanelView(PanelViewKind viewKind) {
  return QIcon(iconPathForPanelView(viewKind));
}

QIcon iconForHiddenRightPane() {
  return QIcon(kHiddenRightPaneIconPath);
}

} // namespace

MdiArea::MdiArea(QWidget *parent) : QMdiArea(parent) {
  setViewMode(QMdiArea::TabbedView);
  setDocumentMode(true);
  setTabsMovable(true);
  setTabsClosable(true);
  updateBackgroundColor();

  connect(this, &QMdiArea::subWindowActivated, this, &MdiArea::onSubWindowActivated);
  qApp->installEventFilter(this);
  connect(NotificationCenter::the(), &NotificationCenter::vgmFileSelected, this, &MdiArea::onVGMFileSelected);
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
    auto *window = containingSubWindowForWidget(now);
    if (!window || window != activeSubWindow()) {
      return;
    }
    syncSelectedFileForWindow(window);
  });
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

VGMFileView *MdiArea::currentFileView() const {
  return asFileView(currentSubWindow());
}

void MdiArea::setPaneSelection(const PaneActions &actions, PanelViewKind kind, bool hiddenSelected) {
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
  if (actions.hidden) {
    actions.hidden->setChecked(hiddenSelected);
  }
}

void MdiArea::setSeqOnlyEnabled(PaneActions &actions, bool enabled) {
  if (actions.activeNotes) {
    actions.activeNotes->setEnabled(enabled);
  }
  if (actions.pianoRoll) {
    actions.pianoRoll->setEnabled(enabled);
  }
}

void MdiArea::applyEmptyPaneSelection() {
  // No active file: keep controls visible but inert.
  setPaneSelection(m_leftPaneActions, PanelViewKind::Hex, false);
  setPaneSelection(m_rightPaneActions, PanelViewKind::Tree, false);
  setSeqOnlyEnabled(m_leftPaneActions, false);
  setSeqOnlyEnabled(m_rightPaneActions, false);
  if (m_sequenceControlBarButton) {
    QSignalBlocker blocker(m_sequenceControlBarButton);
    m_sequenceControlBarButton->setChecked(false);
    m_sequenceControlBarButton->setEnabled(false);
  }
}

void MdiArea::applyPaneViewSelection(VGMFileView *fileView) {
  if (!fileView) {
    applyEmptyPaneSelection();
    return;
  }

  const bool singlePane = fileView->singlePaneMode();
  setPaneSelection(m_leftPaneActions, fileView->panelView(PanelSide::Left), false);
  setPaneSelection(m_rightPaneActions, fileView->panelView(PanelSide::Right), singlePane);

  const bool sequenceViewsAvailable = fileView->supportsSequenceViews();
  setSeqOnlyEnabled(m_leftPaneActions, sequenceViewsAvailable);
  setSeqOnlyEnabled(m_rightPaneActions, sequenceViewsAvailable);
  if (m_sequenceControlBarButton) {
    m_sequenceControlBarButton->setEnabled(sequenceViewsAvailable);
    QSignalBlocker blocker(m_sequenceControlBarButton);
    m_sequenceControlBarButton->setChecked(sequenceViewsAvailable &&
                                           fileView->sequenceControlBarVisible());
  }
}

void MdiArea::setPaneButtonsEnabled(bool enabled) {
  if (m_leftPaneButton) {
    m_leftPaneButton->setEnabled(enabled);
  }
  if (m_rightPaneButton) {
    m_rightPaneButton->setEnabled(enabled);
  }
  if (m_sequenceControlBarButton) {
    m_sequenceControlBarButton->setEnabled(enabled);
  }
}

void MdiArea::setPaneView(PanelSide side, PanelViewKind kind) {
  auto *fileView = currentFileView();
  if (!fileView) {
    return;
  }

  if (side == PanelSide::Right) {
    // Apply the requested right-pane view first (while hidden if needed) so
    // unhide does not instantiate the previously-hidden view kind.
    fileView->setPanelView(side, kind);
    fileView->setSinglePaneMode(false);
    updateTabBarControls();
    return;
  }

  fileView->setPanelView(side, kind);
  updateTabBarControls();
}

void MdiArea::setRightPaneHidden(bool hidden) {
  auto *fileView = currentFileView();
  if (!fileView) {
    return;
  }

  fileView->setSinglePaneMode(hidden);
  updateTabBarControls();
}

void MdiArea::setSequenceControlBarVisible(bool visible) {
  auto *fileView = currentFileView();
  if (!fileView || !fileView->supportsSequenceViews()) {
    return;
  }

  fileView->setSequenceControlBarVisible(visible);
  updateTabBarControls();
}

QMdiSubWindow *MdiArea::containingSubWindowForWidget(QWidget *widget) const {
  QWidget *cursor = widget;
  while (cursor) {
    if (auto *subWindow = qobject_cast<QMdiSubWindow *>(cursor)) {
      return windowToFileMap.find(subWindow) != windowToFileMap.end() ? subWindow : nullptr;
    }
    cursor = cursor->parentWidget();
  }
  return nullptr;
}

void MdiArea::syncSelectedFileForWindow(QMdiSubWindow *window) {
  if (!window) {
    return;
  }

  const auto it = windowToFileMap.find(window);
  if (it == windowToFileMap.end()) {
    return;
  }

  auto *file = it->second;
  if (!file || file == m_lastNotifiedFile) {
    return;
  }

  m_lastNotifiedFile = file;
  NotificationCenter::the()->selectVGMFile(file, this);
}

// Lazily creates the right-side tab-strip controls and their menus.
void MdiArea::setupTabBarControls() {
  if (!m_tabBar) {
    m_tabBar = findChild<QTabBar *>();
    if (!m_tabBar) {
      return;
    }

    m_tabBar->setDrawBase(false);
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
    m_tabControls = new TabControlStrip(controlsParent);
    m_tabControls->setObjectName(QStringLiteral("TabControlStrip"));
    m_tabControls->setAttribute(Qt::WA_StyledBackground, false);
    auto *controlsLayout = new QHBoxLayout(m_tabControls);
    controlsLayout->setContentsMargins(kTabControlOuterMargin, 0, kTabControlOuterMargin, 0);
    controlsLayout->setSpacing(kTabControlSpacing);

    auto configureButton = [](QToolButton *button, const QString &toolTip) {
      button->setAutoRaise(false);
      button->setToolButtonStyle(Qt::ToolButtonIconOnly);
      button->setIconSize(QSize(24, 18));
      button->setFixedSize(QSize(32, 24));
      button->setFocusPolicy(Qt::NoFocus);
      button->setToolTip(toolTip);
      return button;
    };

    auto createIconButton = [&configureButton](QWidget *parent, const QString &toolTip) {
      return configureButton(new QToolButton(parent), toolTip);
    };
    auto createMenuButton = [&configureButton, &createIconButton](QWidget *parent, const QString &toolTip) {
#ifdef Q_OS_LINUX
      // On Wayland, QToolButton::showMenu() intermittently fails. So we use a workaround.
      return configureButton(new WaylandMenuToolButton(parent), toolTip);
#else
      auto *button = createIconButton(parent, toolTip);
      button->setPopupMode(QToolButton::InstantPopup);
      return button;
#endif
    };

    m_leftPaneButton = createMenuButton(m_tabControls, tr("Select left pane view"));
    controlsLayout->addWidget(m_leftPaneButton);

    m_rightPaneButton = createMenuButton(m_tabControls, tr("Select or hide right pane view"));
    controlsLayout->addWidget(m_rightPaneButton);

    m_sequenceControlBarButton = createIconButton(m_tabControls, tr("Show sequence control bar"));
    m_sequenceControlBarButton->setCheckable(true);
    controlsLayout->addWidget(m_sequenceControlBarButton);
    connect(m_sequenceControlBarButton,
            &QToolButton::toggled,
            this,
            [this](bool visible) { setSequenceControlBarVisible(visible); });

    auto createPaneMenu = [this](QToolButton *button,
                                 PanelSide side,
                                 PaneActions &actions,
                                 bool includeHiddenOption) {
      auto *menu = new QMenu(button);
      auto *group = new QActionGroup(menu);
      group->setExclusive(true);

      auto addAction = [this, menu, group, side](PanelViewKind kind, const QString &text, QAction *&slot) {
        slot = menu->addAction(iconForPanelView(kind), text);
        slot->setIconVisibleInMenu(true);
        slot->setCheckable(true);
        group->addAction(slot);
        connect(slot, &QAction::triggered, this, [this, side, kind]() { setPaneView(side, kind); });
      };

      if (includeHiddenOption) {
        actions.hidden = menu->addAction(iconForHiddenRightPane(), tr("Hidden"));
        actions.hidden->setIconVisibleInMenu(true);
        actions.hidden->setCheckable(true);
        group->addAction(actions.hidden);
        connect(actions.hidden, &QAction::triggered, this, [this]() { setRightPaneHidden(true); });
      }

      addAction(PanelViewKind::Hex, tr("Hex"), actions.hex);
      addAction(PanelViewKind::Tree, tr("Tree"), actions.tree);
      addAction(PanelViewKind::ActiveNotes, tr("Active Notes"), actions.activeNotes);
      addAction(PanelViewKind::PianoRoll, tr("Piano Roll"), actions.pianoRoll);

      // The target pane is captured by `side`; the right menu adds a hide option.
      button->setMenu(menu);
    };

    createPaneMenu(m_leftPaneButton, PanelSide::Left, m_leftPaneActions, false);
    createPaneMenu(m_rightPaneButton, PanelSide::Right, m_rightPaneActions, true);
  }

  m_tabBar->setCursor(Qt::ArrowCursor);
  if (m_tabBarHost) {
    m_tabBarHost->setCursor(Qt::ArrowCursor);
  }
  m_tabControls->setCursor(Qt::ArrowCursor);

  applyTabBarStyle();
  refreshTabControlAppearance();
  ensureTabCloseButtonsOnRight();
  repositionTabBarControls();
  updateTabBarControls();
}

// Syncs tab-strip control state with the active VGMFileView.
void MdiArea::updateTabBarControls() {
  if (!m_tabControls || !m_leftPaneButton || !m_rightPaneButton || !m_sequenceControlBarButton) {
    return;
  }

  ensureTabCloseButtonsOnRight();

  auto *fileView = currentFileView();
  const bool hasFileView = fileView != nullptr;

  setPaneButtonsEnabled(hasFileView);

  if (!hasFileView) {
    applyEmptyPaneSelection();
    refreshTabControlAppearance();
    return;
  }

  applyPaneViewSelection(fileView);
  refreshTabControlAppearance();
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
  const int reservedRightMargin = controlsSize.width();
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

  const int x = std::max(0, host->width() - controlsSize.width());
  const int y = tabGeometry.y();
  m_tabControls->setGeometry(x, y, controlsSize.width(), tabGeometry.height());
  m_tabControls->raise();
}

// Applies the flat visual style shared by the tab-strip controls.
void MdiArea::refreshTabControlAppearance() {
  if (!m_tabControls || !m_leftPaneButton || !m_rightPaneButton || !m_sequenceControlBarButton) {
    return;
  }

  const FlatTabBarColors colors =
      flatTabBarColors(m_tabBar ? static_cast<const QWidget *>(m_tabBar)
                                : static_cast<const QWidget *>(m_tabControls));
  static_cast<TabControlStrip *>(m_tabControls)->setBackgroundColor(colors.stripBackground);

  const QString controlsStyle = QStringLiteral(
      "QWidget#TabControlStrip QToolButton {"
      " border: none;"
      " background: transparent;"
      " border-radius: 7px;"
      " padding: 0px;"
      " margin: 0px;"
      "}"
      "QWidget#TabControlStrip QToolButton:hover { background: %1; }"
      "QWidget#TabControlStrip QToolButton:pressed { background: %2; }"
      "QWidget#TabControlStrip QToolButton:disabled { background: transparent; }"
      "QWidget#TabControlStrip QToolButton::menu-indicator { image: none; width: 0px; }")
                                   .arg(cssColor(colors.hoverFill))
                                   .arg(cssColor(colors.pressedFill));
  if (m_tabControls->styleSheet() != controlsStyle) {
    m_tabControls->setStyleSheet(controlsStyle);
  }

  bool rightPaneHidden = false;
  bool sequenceControlBarVisible = false;
  if (auto *fileView = currentFileView()) {
    rightPaneHidden = fileView->singlePaneMode();
    sequenceControlBarVisible = fileView->sequenceControlBarVisible();
  }

  auto assignIconForState = [&](QToolButton *button, const QString &iconPath, bool onState) {
    if (!button) {
      return;
    }
    const QColor glyph = !button->isEnabled()
                             ? colors.disabledText
                             : (onState ? colors.activeText : colors.inactiveText);
    button->setIcon(panelButtonIcon(iconPath, glyph));
  };

  assignIconForState(m_leftPaneButton, kLeftPaneButtonIconPath, true);
  assignIconForState(m_rightPaneButton, kRightPaneButtonIconPath, !rightPaneHidden);
  assignIconForState(m_sequenceControlBarButton,
                     kSequenceControlBarButtonIconPath,
                     sequenceControlBarVisible);
}

// Reapplies tab-strip colors after the theme transition settles.
void MdiArea::refreshTabControlsAfterThemeChange() {
  if (m_refreshingTabControlsAfterThemeChange) {
    return;
  }

  QScopedValueRollback<bool> refreshingGuard(m_refreshingTabControlsAfterThemeChange, true);
  updateBackgroundColor();
  applyTabBarStyle();
  refreshTabControlAppearance();
  ensureTabCloseButtonsOnRight();

  if (m_tabBar) {
    m_tabBar->update();
  }
  if (m_tabControls) {
    m_tabControls->update();
  }
}

// Applies tab-bar styling in an idempotent way to avoid repolish loops.
void MdiArea::applyTabBarStyle() {
  if (!m_tabBar) {
    return;
  }

  const FlatTabBarColors colors = flatTabBarColors(m_tabBar);
  const QColor barBackground = colors.stripBackground;
  const QColor selectedTabBackground = colors.activeTabBackground;
  QColor inactiveTabBorder = colors.activeText;
  inactiveTabBorder.setAlpha(22);
  const QString styleSheet = QStringLiteral(
      "QTabBar {"
      " background: %1;"
      " qproperty-drawBase: 0;"
      "}"
      "QTabBar::tab {"
      " background: %2;"
      " color: %3;"
      " border: none;"
      " border-right: 1px solid %11;"
      " border-radius: 0px;"
      " margin: 0px;"
      " padding: 0px 0px 0px 15px;"
      " min-height: %4px;"
      "}"
      "QTabBar::tab:selected {"
      " background: %5;"
      " color: %6;"
      " border-left-color: transparent;"
      " border-right-color: transparent;"
      "}"
      "QTabBar::tab:hover:!selected {"
      " background: %7;"
      " color: %6;"
      " border-right-color: %11;"
      "}"
      "QTabBar::tab:disabled {"
      " color: %8;"
      "}"
      "QTabBar QToolButton {"
      " border: none;"
      " background: %1;"
      " padding: 0px;"
      " margin: 0px;"
      "}"
      "QTabBar QToolButton:hover {"
      " background: %9;"
      "}"
      "QTabBar QToolButton:pressed {"
      " background: %10;"
      "}")
                                   .arg(cssColor(barBackground))
                                   .arg(cssColor(barBackground))
                                   .arg(cssColor(colors.inactiveText))
                                   .arg(Size::VTab)
                                   .arg(cssColor(selectedTabBackground))
                                   .arg(cssColor(colors.activeText))
                                   .arg(cssColor(colors.hoveredInactiveTabBackground))
                                   .arg(cssColor(colors.disabledText))
                                   .arg(cssColor(colors.hoverFill))
                                    .arg(cssColor(colors.pressedFill))
                                   .arg(cssColor(inactiveTabBorder));
  // Re-setting the same stylesheet can trigger repolish churn and recursive style events.
  if (m_tabBar->styleSheet() == styleSheet) {
    return;
  }
  m_tabBar->setStyleSheet(styleSheet);
}

void MdiArea::ensureTabCloseButtonsOnRight() {
  if (!m_tabBar || !m_tabBar->tabsClosable()) {
    return;
  }

  for (int index = 0; index < m_tabBar->count(); ++index) {
    QWidget *buttonWidget = m_tabBar->tabButton(index, QTabBar::RightSide);
    if (!buttonWidget) {
      buttonWidget = m_tabBar->tabButton(index, QTabBar::LeftSide);
    }
    if (!buttonWidget) {
      continue;
    }

    if (m_tabBar->tabButton(index, QTabBar::RightSide) != buttonWidget) {
      m_tabBar->setTabButton(index, QTabBar::LeftSide, nullptr);
      m_tabBar->setTabButton(index, QTabBar::RightSide, buttonWidget);
    }

    auto *button = qobject_cast<QAbstractButton *>(buttonWidget);
    if (!button || button->property("mdiCloseConnected").toBool()) {
      continue;
    }

    connect(button, &QAbstractButton::clicked, this, [this, button]() {
      if (!m_tabBar) {
        return;
      }

      for (int tabIndex = 0; tabIndex < m_tabBar->count(); ++tabIndex) {
        if (m_tabBar->tabButton(tabIndex, QTabBar::RightSide) == button ||
            m_tabBar->tabButton(tabIndex, QTabBar::LeftSide) == button) {
          QMetaObject::invokeMethod(m_tabBar, "tabCloseRequested", Q_ARG(int, tabIndex));
          return;
        }
      }
    });
    button->setProperty("mdiCloseConnected", true);
  }
}

void MdiArea::changeEvent(QEvent *event) {
  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::ApplicationPaletteChange ||
      event->type() == QEvent::StyleChange) {
    refreshTabControlsAfterThemeChange();
  }
  QMdiArea::changeEvent(event);
}

bool MdiArea::eventFilter(QObject *watched, QEvent *event) {
  if (m_tabBar && watched == m_tabBar->window() && event &&
      (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate ||
       event->type() == QEvent::ActivationChange)) {
    refreshTabControlAppearance();
    if (m_tabBar) {
      m_tabBar->update();
    }
    if (m_tabControls) {
      m_tabControls->update();
    }
  }

  if (watched == m_tabBar || watched == m_tabBarHost) {
    // Track host + tab-bar layout changes to keep the reserved right strip aligned.
    switch (event->type()) {
      case QEvent::Show:
      case QEvent::Resize:
      case QEvent::Move:
      case QEvent::LayoutRequest:
        repositionTabBarControls();
        refreshTabControlAppearance();
        ensureTabCloseButtonsOnRight();
        break;
      case QEvent::StyleChange:
      case QEvent::PaletteChange:
      case QEvent::ApplicationPaletteChange:
        repositionTabBarControls();
        refreshTabControlsAfterThemeChange();
        break;
      default:
        break;
    }
  }

  if (event && event->type() == QEvent::ContextMenu) {
    auto* fileView = currentFileView();
    auto* contextMenuEvent = static_cast<QContextMenuEvent*>(event);
    auto* watchedWidget = qobject_cast<QWidget*>(watched);
    const bool watchedBelongsToFileView =
        fileView && watchedWidget &&
        (watchedWidget == fileView ||
         fileView == containingSubWindowForWidget(watchedWidget) ||
         fileView->isAncestorOf(watchedWidget));
    if (fileView && contextMenuEvent &&
        !(watchedWidget && watchedWidget->windowType() == Qt::Popup) &&
        (!watchedWidget || watchedBelongsToFileView) &&
        fileView->showPaneContextMenuAt(contextMenuEvent->globalPos())) {
      return true;
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
    if (m_lastNotifiedFile == file) {
      m_lastNotifiedFile = nullptr;
    }
  }

  updateTabBarControls();
}

void MdiArea::showPaneViewMenu(VGMFileView* fileView, PanelSide side, const QPoint& globalPos) {
  if (!fileView) {
    return;
  }

  auto* window = qobject_cast<QMdiSubWindow*>(fileView);
  if (window && window != currentSubWindow()) {
    setActiveSubWindow(window);
  }

  setupTabBarControls();
  updateTabBarControls();

  QToolButton* button = side == PanelSide::Left ? m_leftPaneButton : m_rightPaneButton;
  if (!button || !button->menu()) {
    return;
  }

  QMenu contextMenu;
  const bool hasSpecificActions = fileView->appendPaneSpecificContextActions(side, contextMenu);
  if (hasSpecificActions && !button->menu()->actions().isEmpty()) {
    contextMenu.addSeparator();
  }
  contextMenu.addActions(button->menu()->actions());
  contextMenu.exec(globalPos);
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

  syncSelectedFileForWindow(window);

  updateTabBarControls();
}

void MdiArea::onVGMFileSelected(const VGMFile *file, QWidget *caller) {
  if (!file) {
    return;
  }

  if (caller != this) {
    m_lastNotifiedFile = file;
  }

  if (caller == this) {
    return;
  }

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
