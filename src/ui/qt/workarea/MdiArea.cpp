/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MdiArea.h"

#include "application/WorkspaceController.h"
#include "InstructionHintLayout.h"
#include "Metrics.h"
#include "UIHelpers.h"
#include "VGMFileView.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>
#include <vector>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPixmap>
#include <QShortcut>
#include <QTabBar>

namespace {

QIcon assetIcon(const vgmtrans::core::Asset& asset) {
  if (std::holds_alternative<vgmtrans::core::SequenceProgramAsset>(asset)) {
    return QIcon(QStringLiteral(":/icons/sequence.svg"));
  }
  if (std::holds_alternative<vgmtrans::core::SoundBankAsset>(asset)) {
    return QIcon(QStringLiteral(":/icons/instrument-set.svg"));
  }
  if (std::holds_alternative<vgmtrans::core::SamplePoolAsset>(asset)) {
    return QIcon(QStringLiteral(":/icons/sample-collection.svg"));
  }
  return QIcon(QStringLiteral(":/icons/binary.svg"));
}

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

  DetailedInstructionLayout(const DetailedInstruction& instruction, QFont headingFont, QFont subFont)
      : instruction(instruction), headingFont(std::move(headingFont)), subFont(std::move(subFont)),
        headingMetrics(this->headingFont), subMetrics(this->subFont) {}
};

DetailedInstructionLayout computeDetailedInstructionLayout(const DetailedInstruction& instruction,
                                                           const QFont& baseFont) {
  QFont headingFont = prepareInstructionFont(baseFont, 1.8, QFont::Normal, 0, QStringLiteral("Helvetica Neue"));
  QFont subFont = prepareInstructionFont(baseFont, 1.25, QFont::Normal, 0, QStringLiteral("Helvetica Neue"));
  DetailedInstructionLayout layout(instruction, headingFont, subFont);

  layout.iconSide =
      std::max(layout.headingMetrics.height(), static_cast<int>(std::round(layout.headingMetrics.height() * 1.1)));
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

void paintDetailedInstruction(QPainter& painter, const DetailedInstructionLayout& layout, const QPoint& origin,
                              const QColor& accent, int blockWidth) {
  const int rowLeft = origin.x();
  const int headingTop = origin.y();
  const QSize iconSize(layout.iconSide, layout.iconSide);
  const qreal devicePixelRatio = painter.device() ? painter.device()->devicePixelRatioF() : qreal(1.0);
  const QPixmap icon = tintedIconPixmap(QIcon(layout.instruction.iconPath), iconSize, accent, devicePixelRatio);
  if (!icon.isNull()) {
    const int iconY = headingTop + (layout.headingMetrics.height() - layout.iconSide) / 2;
    painter.drawPixmap(rowLeft, iconY, icon);
  }

  painter.setFont(layout.headingFont);
  painter.setPen(accent);
  const QRect headingRect(rowLeft + layout.headingLeft, headingTop, blockWidth - layout.headingLeft,
                          layout.headingMetrics.height());
  painter.drawText(headingRect, Qt::AlignLeft | Qt::AlignTop, layout.instruction.heading);
  painter.setFont(layout.subFont);
  const QRect subRect(rowLeft + layout.headingLeft, headingTop + layout.subTopOffset, blockWidth - layout.headingLeft,
                      layout.subMetrics.height());
  painter.drawText(subRect, Qt::AlignLeft | Qt::AlignTop, layout.instruction.subText);
}

}  // namespace

MdiArea::MdiArea(QWidget* parent) : QMdiArea(parent) {
  setViewMode(QMdiArea::TabbedView);
  setDocumentMode(true);
  setTabsMovable(true);
  setTabsClosable(true);
  updateBackgroundColor();

  connect(this, &QMdiArea::subWindowActivated, this, &MdiArea::onSubWindowActivated);

  if (auto* tab_bar = findChild<QTabBar*>()) {
    tab_bar->setStyleSheet(QString{"QTabBar::tab { height: %1; }"}.arg(Size::VTab));
    tab_bar->setExpanding(false);
    tab_bar->setUsesScrollButtons(true);
#ifdef Q_OS_MAC
    tab_bar->setElideMode(Qt::ElideNone);
#endif
  }

  // Create OS-specific keyboard shortcuts
  auto addShortcut = [this](const QKeySequence& seq, auto slot) {
    auto* sc = new QShortcut(seq, this);
    sc->setContext(Qt::WindowShortcut);
    connect(sc, &QShortcut::activated, this, slot);
  };

#ifdef Q_OS_MAC
  // Cmd ⇧ [  /  Cmd ⇧ ]
  addShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft), &QMdiArea::activatePreviousSubWindow);
  addShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight), &QMdiArea::activateNextSubWindow);

  // Cmd ⌥ ←  /  Cmd ⌥ →
  addShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left), &QMdiArea::activatePreviousSubWindow);
  addShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right), &QMdiArea::activateNextSubWindow);

  // Ctrl + PageDown / Ctrl + PageUp
  addShortcut(QKeySequence(Qt::META | Qt::Key_PageDown), &QMdiArea::activateNextSubWindow);
  addShortcut(QKeySequence(Qt::META | Qt::Key_PageUp), &QMdiArea::activatePreviousSubWindow);
#else  // Windows & Linux
  // Ctrl + Tab  /  Ctrl + Shift + Tab
  addShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), &QMdiArea::activateNextSubWindow);
  addShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), &QMdiArea::activatePreviousSubWindow);

  // Ctrl + PageDown / Ctrl + PageUp
  addShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown), &QMdiArea::activateNextSubWindow);
  addShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp), &QMdiArea::activatePreviousSubWindow);
#endif
}

void MdiArea::changeEvent(QEvent* event) {
  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
    updateBackgroundColor();
  }
  QMdiArea::changeEvent(event);
}

void MdiArea::paintEvent(QPaintEvent* event) {
  QMdiArea::paintEvent(event);

  if (!subWindowList().isEmpty()) {
    return;
  }

  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  QColor accent = palette().color(QPalette::WindowText);
  accent.setAlphaF(0.4);

  const bool hasRawFiles = m_workspace != nullptr && !m_workspace->snapshot().sources().empty();
  const bool hasCollections = m_workspace != nullptr && !m_workspace->snapshot().collections().empty();
  const QRect areaRect = viewport()->rect();
  const QFont baseFont = painter.font();

  if (!hasRawFiles) {
    InstructionHint dropHint{QStringLiteral(":/icons/tray-arrow-down.svg"), tr("Drop files to scan"), 2, 4.0, 0.3};
    const InstructionMetrics metrics = computeInstructionMetrics(dropHint, baseFont);
    const int left = areaRect.center().x() - metrics.size.width() / 2;
    const int top = areaRect.center().y() - metrics.size.height() / 2;
    paintInstruction(painter, metrics, QPoint(left, top), accent);
    return;
  }

  std::vector<DetailedInstruction> instructions;
  instructions.reserve(3);
  if (hasCollections) {
    instructions.push_back({QStringLiteral(":/icons/volume-high.svg"), tr("Preview"), tr("Double-click a collection")});
  }
  instructions.push_back(
      {QStringLiteral(":/icons/midi-port.svg"), tr("Convert"), tr("Right-click a detected file or collection")});
  instructions.push_back({QStringLiteral(":/icons/magnify.svg"), tr("Analyze"), tr("Double-click a detected file")});

  std::vector<DetailedInstructionLayout> layouts;
  layouts.reserve(instructions.size());
  int blockWidth = 0;
  int blockHeight = 0;
  for (const auto& instruction : instructions) {
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

void MdiArea::setWorkspace(vgmtrans::ui::WorkspaceController* workspace) {
  m_workspace = workspace;
  workspaceChanged();
}

void MdiArea::newView(vgmtrans::core::AssetId asset) {
  if (m_workspace == nullptr) {
    return;
  }
  const auto* value = m_workspace->snapshot().asset(asset);
  if (value == nullptr) {
    return;
  }

  if (auto it = assetToWindowMap.find(asset.value); it != assetToWindowMap.end()) {
    const InspectorWindow existing = it->second;
    if (existing.window && existing.content && subWindowList().contains(existing.window.data()) &&
        existing.window->widget() == existing.content.data()) {
      setActiveSubWindow(existing.window.data());
      // Activation can synchronously deliver signals that close a pending tab,
      // so re-check both guarded objects before moving keyboard focus.
      if (existing.window && existing.content && existing.window->widget() == existing.content.data()) {
        existing.content->setFocus();
      }
      return;
    }
    if (existing.window) {
      windowToAssetMap.erase(existing.window.data());
    }
    assetToWindowMap.erase(it);
  }

  auto inspection = m_workspace->inspect(asset);
  if (!inspection) {
    return;
  }
  auto* inspector = new VGMFileView(std::move(inspection), *value);
  inspector->setSeekModifierActive(m_seekModifierActive);
  if (asset == m_playbackSequence) {
    inspector->setPlaybackTimeline(m_playbackSpans);
    inspector->onPlaybackPositionChanged(m_playbackPosition, m_playbackMaximum, PositionChangeOrigin::HexView);
  }
  const QString name = inspector->windowTitle();
  QMdiSubWindow* window = addSubWindow(inspector, Qt::SubWindow);
  window->setWindowIcon(assetIcon(*value));
  assetToWindowMap.emplace(asset.value, InspectorWindow{.window = window, .content = inspector});
  windowToAssetMap.emplace(window, asset.value);
  connect(inspector, &VGMFileView::statusChanged, this, &MdiArea::inspectorStatusChanged);
  connect(inspector, &VGMFileView::playbackSeekRequested, this, &MdiArea::playbackSeekRequested);
  connect(window, &QObject::destroyed, this, [this, assetValue = asset.value, window]() {
    const auto found = assetToWindowMap.find(assetValue);
    if (found != assetToWindowMap.end() && (found->second.window.isNull() || found->second.window.data() == window)) {
      assetToWindowMap.erase(found);
    }
    windowToAssetMap.erase(window);
  });
  const QPointer<QMdiSubWindow> windowGuard(window);
  const QPointer<VGMFileView> inspectorGuard(inspector);
  window->showMaximized();
  if (windowGuard && inspectorGuard && windowGuard->widget() == inspectorGuard.data()) {
    inspectorGuard->setFocus();
  }
#ifdef Q_OS_MAC
  if (inspectorGuard) {
    inspectorGuard->setWindowTitle(QStringLiteral(" %1 ").arg(name));
  }
#endif
}

void MdiArea::workspaceChanged() {
  if (m_workspace != nullptr) {
    for (auto it = assetToWindowMap.begin(); it != assetToWindowMap.end();) {
      if (m_workspace->snapshot().asset(vgmtrans::core::AssetId{it->first}) != nullptr) {
        ++it;
        continue;
      }
      const QPointer<QMdiSubWindow> window = it->second.window;
      if (window) {
        windowToAssetMap.erase(window.data());
      }
      it = assetToWindowMap.erase(it);
      if (window) {
        window->close();
      }
    }
  }
  viewport()->update();
}

void MdiArea::onSubWindowActivated(QMdiSubWindow* window) {
  emit hexViewAvailableChanged(window != nullptr && qobject_cast<VGMFileView*>(window->widget()) != nullptr);
  if (!window) {
    emit inspectorStatusChanged({}, {}, {}, -1, -1);
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

  if (const auto it = windowToAssetMap.find(window); it != windowToAssetMap.end()) {
    emit assetSelected(vgmtrans::core::AssetId{it->second}, this);
  }
  if (auto* view = activeFileView()) {
    view->setSeekModifierActive(m_seekModifierActive);
    view->refreshStatus();
  }
}

VGMFileView* MdiArea::activeFileView() const {
  QMdiSubWindow* window = activeSubWindow();
  return window != nullptr ? qobject_cast<VGMFileView*>(window->widget()) : nullptr;
}

void MdiArea::increaseActiveHexFont() {
  if (auto* view = activeFileView()) {
    view->increaseHexViewFont();
  }
}

void MdiArea::decreaseActiveHexFont() {
  if (auto* view = activeFileView()) {
    view->decreaseHexViewFont();
  }
}

void MdiArea::resetActiveHexFont() {
  if (auto* view = activeFileView()) {
    view->resetHexViewFont();
  }
}

void MdiArea::setSeekModifierActive(bool active) {
  m_seekModifierActive = active;
  if (auto* view = activeFileView()) {
    view->setSeekModifierActive(active);
  }
}

void MdiArea::setPlaybackSequence(vgmtrans::core::AssetId sequence,
                                  std::span<const vgmtrans::core::SourcePlaybackSpan> timeline) {
  if (m_playbackSequence.valid() && m_playbackSequence != sequence) {
    const auto previous = assetToWindowMap.find(m_playbackSequence.value);
    if (previous != assetToWindowMap.end()) {
      if (auto* view = qobject_cast<VGMFileView*>(previous->second.content.data())) {
        view->setPlaybackTimeline({});
      }
    }
  }

  m_playbackSequence = sequence;
  m_playbackSpans.assign(timeline.begin(), timeline.end());

  const auto current = assetToWindowMap.find(sequence.value);
  if (current != assetToWindowMap.end()) {
    if (auto* view = qobject_cast<VGMFileView*>(current->second.content.data())) {
      view->setPlaybackTimeline(m_playbackSpans);
      view->onPlaybackPositionChanged(m_playbackPosition, m_playbackMaximum, PositionChangeOrigin::HexView);
    }
  }
}

void MdiArea::setPlaybackPosition(int current, int maximum, PositionChangeOrigin origin) {
  m_playbackPosition = current;
  m_playbackMaximum = maximum;
  if (!m_playbackSequence.valid()) {
    return;
  }
  const auto found = assetToWindowMap.find(m_playbackSequence.value);
  if (found != assetToWindowMap.end()) {
    if (auto* view = qobject_cast<VGMFileView*>(found->second.content.data())) {
      view->onPlaybackPositionChanged(current, maximum, origin);
    }
  }
}

void MdiArea::clearPlayback() {
  if (m_playbackSequence.valid()) {
    const auto found = assetToWindowMap.find(m_playbackSequence.value);
    if (found != assetToWindowMap.end()) {
      if (auto* view = qobject_cast<VGMFileView*>(found->second.content.data())) {
        view->setPlaybackTimeline({});
      }
    }
  }
  m_playbackSequence = {};
  m_playbackSpans.clear();
  m_playbackPosition = 0;
  m_playbackMaximum = 1;
}

void MdiArea::selectAsset(vgmtrans::core::AssetId asset, QWidget* caller) {
  if (caller == this || !asset.valid()) {
    return;
  }

  auto it = assetToWindowMap.find(asset.value);
  if (it == assetToWindowMap.end()) {
    return;
  }
  const InspectorWindow inspector = it->second;
  const QPointer<QMdiSubWindow> window = inspector.window;
  if (!window || !inspector.content || !subWindowList().contains(window.data()) ||
      window->widget() != inspector.content.data()) {
    if (window) {
      windowToAssetMap.erase(window.data());
    }
    assetToWindowMap.erase(it);
    return;
  }

  QWidget* focusedWidget = QApplication::focusWidget();
  const bool callerHadFocus =
      caller != nullptr && focusedWidget != nullptr && (focusedWidget == caller || caller->isAncestorOf(focusedWidget));
  setActiveSubWindow(window.data());

  // Selecting an item may activate its analysis tab, but keyboard focus stays
  // in the list that initiated the selection.
  if (callerHadFocus) {
    caller->setFocus();
  }
}

void MdiArea::ensureMaximizedSubWindow(QMdiSubWindow* window) {
  if (window && !window->isMaximized()) {
    window->showMaximized();
  }
}
