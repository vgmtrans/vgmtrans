/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QShortcut>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

#include "VGMFileView.h"
#include "MdiArea.h"
#include "SequencePlayer.h"
#include "SplitterSnapProvider.h"
#include "VGMFile.h"
#include "VGMFileTreeView.h"
#include "VGMColl.h"
#include "VGMSeq.h"
#include "ScaleConversion.h"
#include "activenoteview/ActiveNoteView.h"
#include "hexview/HexView.h"
#include "pianorollview/PianoRollView.h"
#include "Helpers.h"
#include "Root.h"
#include "SeqEvent.h"
#include "SeqNoteUtils.h"
#include "SeqTrack.h"
#include "services/Settings.h"

namespace {
constexpr uint8_t kActiveNotePreviewVelocity = 127;

PanelViewKind sanitizeSeqPaneViewKind(int viewKind, PanelSide side) {
  switch (viewKind) {
    case static_cast<int>(PanelViewKind::Hex):
      return PanelViewKind::Hex;
    case static_cast<int>(PanelViewKind::Tree):
      return PanelViewKind::Tree;
    case static_cast<int>(PanelViewKind::ActiveNotes):
      return PanelViewKind::ActiveNotes;
    case static_cast<int>(PanelViewKind::PianoRoll):
      return PanelViewKind::PianoRoll;
    default:
      return side == PanelSide::Left ? PanelViewKind::Hex : PanelViewKind::PianoRoll;
  }
}

int previewMidiChannelForEvent(const SeqEvent* event) {
  if (!event) {
    return -1;
  }

  int channel = static_cast<int>(event->channel);
  if (event->parentTrack) {
    channel += event->parentTrack->channelGroup * 16;
  }

  if (channel < 0 || channel >= 128) {
    return -1;
  }
  return channel;
}

int previewMidiChannelForTrack(const VGMSeq* seq, int trackIndex) {
  if (trackIndex < 0) {
    return -1;
  }

  if (seq && trackIndex < static_cast<int>(seq->aTracks.size())) {
    if (const auto* track = seq->aTracks[static_cast<size_t>(trackIndex)]) {
      const int channel = track->channel + (track->channelGroup * 16);
      if (channel >= 0 && channel < 128) {
        return channel;
      }
    }
  }

  if (trackIndex >= 0 && trackIndex < 128) {
    return trackIndex;
  }
  return -1;
}

bool buildPreviewNoteForEvent(const SeqEvent* seqEvent, SequencePlayer::PreviewNote& outNote) {
  if (!seqEvent) {
    return false;
  }

  const int midiChannel = previewMidiChannelForEvent(seqEvent);
  if (midiChannel < 0) {
    return false;
  }

  const int rawKey = SeqNoteUtils::rawNoteKey(seqEvent);
  const int rawVelocity = SeqNoteUtils::rawNoteVelocity(seqEvent);
  if (rawKey < 0 || rawVelocity < 0) {
    return false;
  }
  uint8_t key = static_cast<uint8_t>(rawKey);
  uint8_t velocity = static_cast<uint8_t>(rawVelocity);

  // Match the same velocity scaling used during MIDI conversion.
  if (seqEvent->parentTrack && seqEvent->parentTrack->usesLinearAmplitudeScale()) {
    velocity = convert7bitPercentAmpToStdMidiVal(velocity);
  }

  outNote.channel = static_cast<uint8_t>(midiChannel);
  outNote.key = key;
  outNote.velocity = velocity;
  return true;
}
}  // namespace

VGMFileView::VGMFileView(VGMFile* vgmfile)
    : QMdiSubWindow(), m_vgmfile(vgmfile) {
  m_splitter = new QSplitter(Qt::Horizontal, this);

  setWindowTitle(QString::fromStdString(m_vgmfile->name()));
  setWindowIcon(iconForFile(vgmFileToVariant(vgmfile)));
  setAttribute(Qt::WA_DeleteOnClose);
  // Keep a stable default cursor across MDI tabs; embedded RHI windows can leak resize cursors.
  setCursor(Qt::ArrowCursor);

  m_isSeqFile = dynamic_cast<VGMSeq*>(m_vgmfile) != nullptr;

  PanelViewKind initialLeftView = PanelViewKind::Hex;
  PanelViewKind initialRightView = PanelViewKind::Tree;
  if (m_isSeqFile) {
    const auto& settings = Settings::the()->VGMSeqFileView;
    initialLeftView = sanitizeSeqPaneViewKind(settings.leftPaneView(), PanelSide::Left);
    initialRightView = sanitizeSeqPaneViewKind(settings.rightPaneView(), PanelSide::Right);
  }

  panel(PanelSide::Left) = createPanel(PanelSide::Left);
  panel(PanelSide::Right) = createPanel(PanelSide::Right);
  setPanelView(PanelSide::Left, initialLeftView);
  setPanelView(PanelSide::Right, initialRightView);
  // Keep one HexView always available so font shortcuts and split-width logic have a source.
  ensurePanelViewCreated(PanelSide::Left, PanelViewKind::Hex);

  m_splitter->addWidget(panel(PanelSide::Left).container);
  m_splitter->addWidget(panel(PanelSide::Right).container);
  m_splitter->setStretchFactor(0, 1);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->setSizes(QList<int>{1, 1});
  m_defaultSplitterHandleWidth = m_splitter->handleWidth();
  qApp->installEventFilter(this);
  m_splitter->installEventFilter(this);

  if (panel(PanelSide::Left).hexView) {
    m_defaultHexFont = panel(PanelSide::Left).hexView->font();
    applyHexViewFont(m_defaultHexFont);
  }

  connect(m_splitter, &QSplitter::splitterMoved, this, &VGMFileView::onSplitterMoved);

  connect(new QShortcut(QKeySequence::ZoomIn, this),
          &QShortcut::activated,
          this,
          &VGMFileView::increaseHexViewFont);

  auto* shortcutEqual = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
  connect(shortcutEqual, &QShortcut::activated, this, &VGMFileView::increaseHexViewFont);

  connect(new QShortcut(QKeySequence::ZoomOut, this),
          &QShortcut::activated,
          this,
          &VGMFileView::decreaseHexViewFont);

  connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this),
          &QShortcut::activated,
          this,
          &VGMFileView::resetHexViewFont);

  connect(&SequencePlayer::the(),
          &SequencePlayer::playbackPositionChanged,
          this,
          &VGMFileView::onPlaybackPositionChanged);
  connect(&SequencePlayer::the(),
          &SequencePlayer::statusChange,
          this,
          &VGMFileView::onPlayerStatusChanged);

  setWidget(m_splitter);

  const int storedLeftPaneWidth = Settings::the()->VGMSeqFileView.leftPaneWidth();
  if (storedLeftPaneWidth > 0) {
    m_preferredLeftPaneWidth = storedLeftPaneWidth;
  }

  const bool storedRightPaneHidden =
      m_isSeqFile ? Settings::the()->VGMSeqFileView.rightPaneHidden() : false;
  if (storedRightPaneHidden) {
    setSinglePaneMode(true);
  }
}

VGMFileView::PanelUi VGMFileView::createPanel(PanelSide side) {
  PanelUi panelUi;

  panelUi.container = new QWidget(this);
  auto* containerLayout = new QVBoxLayout(panelUi.container);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);

  panelUi.stack = new QStackedWidget(panelUi.container);
  for (size_t i = 0; i < panelUi.placeholders.size(); ++i) {
    auto* placeholder = new QWidget(panelUi.stack);
    placeholder->setVisible(false);
    panelUi.placeholders[i] = placeholder;
    panelUi.stack->addWidget(placeholder);
  }

  if (side == PanelSide::Left) {
    panelUi.currentKind = PanelViewKind::Hex;
  } else {
    panelUi.currentKind = PanelViewKind::Tree;
  }
  panelUi.stack->setCurrentIndex(static_cast<int>(panelUi.currentKind));
  containerLayout->addWidget(panelUi.stack, 1);

  return panelUi;
}

QWidget* VGMFileView::panelWidget(const PanelUi& panelUi, PanelViewKind viewKind) const {
  switch (viewKind) {
    case PanelViewKind::Hex:
      return panelUi.hexView;
    case PanelViewKind::Tree:
      return panelUi.treeView;
    case PanelViewKind::ActiveNotes:
      return panelUi.activeNoteView;
    case PanelViewKind::PianoRoll:
      return panelUi.pianoRollView;
  }
  return nullptr;
}

void VGMFileView::applySelectionStateToPanelView(PanelSide side, PanelViewKind viewKind) const {
  const auto& panelUi = panel(side);
  switch (viewKind) {
    case PanelViewKind::Hex:
      if (panelUi.hexView) {
        panelUi.hexView->setSelectedItems(m_selectedItems, m_primarySelectedItem);
      }
      break;
    case PanelViewKind::Tree:
      if (panelUi.treeView) {
        panelUi.treeView->setSelectedItems(m_selectedItems, m_primarySelectedItem);
      }
      break;
    case PanelViewKind::ActiveNotes:
      break;
    case PanelViewKind::PianoRoll:
      if (panelUi.pianoRollView) {
        panelUi.pianoRollView->setSelectedItems(m_selectedItems, m_primarySelectedItem);
      }
      break;
  }
}

void VGMFileView::applyPlaybackStateToPanelView(PanelSide side, PanelViewKind viewKind) const {
  const auto& panelUi = panel(side);
  switch (viewKind) {
    case PanelViewKind::Hex:
      if (!panelUi.hexView) {
        break;
      }
      panelUi.hexView->setPlaybackActive(m_playbackTickActive);
      if (m_playbackVisualsActive) {
        panelUi.hexView->setPlaybackSelectionsForItems(m_playbackItems);
      } else {
        panelUi.hexView->clearPlaybackSelections(false);
      }
      break;
    case PanelViewKind::Tree:
      if (!panelUi.treeView) {
        break;
      }
      if (m_playbackVisualsActive) {
        panelUi.treeView->setPlaybackItems(m_playbackItems);
      } else {
        panelUi.treeView->setPlaybackItems({});
      }
      break;
    case PanelViewKind::ActiveNotes:
      if (panelUi.activeNoteView && !m_playbackVisualsActive) {
        panelUi.activeNoteView->clearPlaybackState();
      }
      break;
    case PanelViewKind::PianoRoll:
      if (panelUi.pianoRollView && !m_playbackVisualsActive) {
        panelUi.pianoRollView->clearPlaybackState();
      }
      break;
  }
}

bool VGMFileView::ensurePanelViewCreated(PanelSide side, PanelViewKind viewKind) {
  if (!supportsViewKind(viewKind)) {
    return false;
  }

  auto& panelUi = panel(side);
  if (!panelUi.stack) {
    return false;
  }

  if (panelWidget(panelUi, viewKind)) {
    return true;
  }

  QWidget* createdWidget = nullptr;
  switch (viewKind) {
    case PanelViewKind::Hex: {
      panelUi.hexView = new HexView(m_vgmfile, panelUi.stack);
      panelUi.hexView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      connect(panelUi.hexView, &HexView::selectionChanged, this, &VGMFileView::onSelectionChange);
      connect(panelUi.hexView, &HexView::seekToEventRequested, this, &VGMFileView::seekToEvent);
      connect(panelUi.hexView, &HexView::notePreviewRequested, this, &VGMFileView::previewNotesForEvent);
      connect(panelUi.hexView, &HexView::notePreviewStopped, this, &VGMFileView::stopNotePreview);
      createdWidget = panelUi.hexView;
      break;
    }
    case PanelViewKind::Tree: {
      panelUi.treeView = new VGMFileTreeView(m_vgmfile, panelUi.stack);
      panelUi.treeView->setMinimumWidth(rightPaneMinimumWidth);
      connect(panelUi.treeView,
              &VGMFileTreeView::currentItemChanged,
              this,
              [this](const QTreeWidgetItem* item, QTreeWidgetItem*) {
                if (item == nullptr) {
                  onSelectionChange(nullptr);
                  return;
                }
                auto* vgmitem = static_cast<VGMItem*>(item->data(0, Qt::UserRole).value<void*>());
                onSelectionChange(vgmitem);
              });
      createdWidget = panelUi.treeView;
      break;
    }
    case PanelViewKind::ActiveNotes: {
      panelUi.activeNoteView = new ActiveNoteView(panelUi.stack);
      if (auto* seq = dynamic_cast<VGMSeq*>(m_vgmfile)) {
        panelUi.activeNoteView->setTrackCount(effectiveTrackCountForSeq(seq));
      }
      connect(panelUi.activeNoteView, &ActiveNoteView::notePreviewRequested, this, &VGMFileView::previewActiveNote);
      connect(panelUi.activeNoteView, &ActiveNoteView::notePreviewStopped, this, &VGMFileView::stopNotePreview);
      createdWidget = panelUi.activeNoteView;
      break;
    }
    case PanelViewKind::PianoRoll: {
      panelUi.pianoRollView = new PianoRollView(panelUi.stack);
      if (auto* seq = dynamic_cast<VGMSeq*>(m_vgmfile)) {
        const int trackCount = effectiveTrackCountForSeq(seq);
        panelUi.pianoRollView->setTrackCount(trackCount);
        panelUi.pianoRollView->setSequence(seq);
      }

      connect(panelUi.pianoRollView,
              &PianoRollView::seekRequested,
              this,
              [this](int tick) {
                if (tick < 0) {
                  return;
                }

                if (m_vgmfile->assocColls.empty()) {
                  return;
                }

                auto* assocColl = m_vgmfile->assocColls.front();
                auto& seqPlayer = SequencePlayer::the();
                if (seqPlayer.activeCollection() != assocColl) {
                  seqPlayer.setActiveCollection(assocColl);
                }

                if (seqPlayer.activeCollection() == assocColl) {
                  seqPlayer.seek(tick, PositionChangeOrigin::SeekBar);
                }
              });
      connect(panelUi.pianoRollView, &PianoRollView::selectionSetChanged, this, &VGMFileView::onSelectionSetChange);
      connect(panelUi.pianoRollView, &PianoRollView::notePreviewRequested, this, &VGMFileView::previewPianoRollNotes);
      connect(panelUi.pianoRollView, &PianoRollView::notePreviewStopped, this, &VGMFileView::stopNotePreview);
      createdWidget = panelUi.pianoRollView;
      break;
    }
  }

  if (!createdWidget) {
    return false;
  }

  const int viewIndex = static_cast<int>(viewKind);
  QWidget* placeholder = panelUi.placeholders[static_cast<size_t>(viewIndex)];
  if (placeholder) {
    if (panelUi.stack->indexOf(placeholder) >= 0) {
      panelUi.stack->removeWidget(placeholder);
    }
    placeholder->deleteLater();
    panelUi.placeholders[static_cast<size_t>(viewIndex)] = nullptr;
  }

  panelUi.stack->insertWidget(viewIndex, createdWidget);

  if (viewKind == PanelViewKind::Hex && panelUi.hexView) {
    auto* leftHex = panel(PanelSide::Left).hexView;
    if (leftHex && leftHex != panelUi.hexView) {
      panelUi.hexView->setFont(leftHex->font());
    }
    panelUi.hexView->setMaximumWidth(panelUi.hexView->getViewportFullWidth());
  }

  applySelectionStateToPanelView(side, viewKind);
  applyPlaybackStateToPanelView(side, viewKind);
  return true;
}

void VGMFileView::focusInEvent(QFocusEvent* event) {
  QMdiSubWindow::focusInEvent(event);
  enforceSplitterPolicyForResize();

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    const auto& panelUi = panel(side);
    if (!panelUi.container || !panelUi.container->isVisible()) {
      continue;
    }
    if (panelUi.currentKind == PanelViewKind::Tree && panelUi.treeView) {
      panelUi.treeView->updateStatusBar();
      return;
    }
  }

  if (panel(PanelSide::Left).treeView) {
    panel(PanelSide::Left).treeView->updateStatusBar();
  } else if (panel(PanelSide::Right).treeView) {
    panel(PanelSide::Right).treeView->updateStatusBar();
  }
}

void VGMFileView::resizeEvent(QResizeEvent* event) {
  QMdiSubWindow::resizeEvent(event);
  enforceSplitterPolicyForResize();
}

bool VGMFileView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_splitter && event &&
      (event->type() == QEvent::Show || event->type() == QEvent::ShowToParent)) {
    enforceSplitterPolicyForResize();
  }

  if (!event) {
    return QMdiSubWindow::eventFilter(watched, event);
  }

  QPoint globalPos;
  bool shouldShowPaneMenu = false;
  if (event->type() == QEvent::ContextMenu) {
    if (auto* contextMenuEvent = static_cast<QContextMenuEvent*>(event)) {
      globalPos = contextMenuEvent->globalPos();
      shouldShowPaneMenu = true;
    }
  } else if (event->type() == QEvent::MouseButtonPress) {
    if (auto* mouseEvent = static_cast<QMouseEvent*>(event);
        mouseEvent && mouseEvent->button() == Qt::RightButton) {
      globalPos = mouseEvent->globalPosition().toPoint();
      shouldShowPaneMenu = true;
    }
  }

  if (shouldShowPaneMenu) {
    PanelSide side = PanelSide::Left;
    if (paneSideAtGlobalPos(globalPos, side)) {
      MdiArea::the()->showPaneViewMenu(this, side, globalPos);
      return true;
    }
  }

  return QMdiSubWindow::eventFilter(watched, event);
}

bool VGMFileView::paneSideAtGlobalPos(const QPoint& globalPos, PanelSide& side) const {
  if (!m_splitter || !m_splitter->isVisible()) {
    return false;
  }

  const QPoint localPos = m_splitter->mapFromGlobal(globalPos);
  if (localPos.x() < 0 || localPos.y() < 0 ||
      localPos.x() >= m_splitter->width() || localPos.y() >= m_splitter->height()) {
    return false;
  }

  const QList<int> sizes = m_splitter->sizes();
  if (sizes.size() < 2) {
    return false;
  }

  const int leftPaneAndHandleWidth = std::max(0, sizes.first()) + std::max(0, m_splitter->handleWidth());
  side = localPos.x() < leftPaneAndHandleWidth ? PanelSide::Left : PanelSide::Right;
  return true;
}

// Switches a pane to the requested view, ignoring unsupported view kinds.
void VGMFileView::setPanelView(PanelSide side, PanelViewKind viewKind) {
  // Guard against sequence-only views on non-sequence files.
  if (!supportsViewKind(viewKind)) {
    return;
  }

  if (!ensurePanelViewCreated(side, viewKind)) {
    return;
  }

  auto& panelUi = panel(side);
  QWidget* targetWidget = panelWidget(panelUi, viewKind);
  if (!targetWidget) {
    return;
  }

  if (panelUi.currentKind == viewKind && panelUi.stack &&
      panelUi.stack->currentWidget() == targetWidget) {
    return;
  }

  panelUi.currentKind = viewKind;
  panelUi.stack->setCurrentWidget(targetWidget);

  if (viewKind == PanelViewKind::Tree && panelUi.treeView) {
    panelUi.treeView->updateStatusBar();
  } else if (viewKind == PanelViewKind::PianoRoll && panelUi.pianoRollView) {
    panelUi.pianoRollView->refreshSequenceData(true);
  }

  if (m_isSeqFile) {
    if (side == PanelSide::Left) {
      Settings::the()->VGMSeqFileView.setLeftPaneView(static_cast<int>(viewKind));
    } else {
      Settings::the()->VGMSeqFileView.setRightPaneView(static_cast<int>(viewKind));
    }
  }

  if (side == PanelSide::Left && m_splitter && m_splitter->count() >= 2) {
    enforceSplitterPolicyForResize();
  }

  const int playbackTick = std::max(0, SequencePlayer::the().elapsedTicks());
  if (m_playbackVisualsActive || SequencePlayer::the().playing() || playbackTick > 0) {
    onPlaybackPositionChanged(playbackTick, 0, PositionChangeOrigin::HexView);
  }
}

// Returns the currently visible view kind for the given pane.
PanelViewKind VGMFileView::panelView(PanelSide side) const {
  return panel(side).currentKind;
}

// Reports whether the requested view kind is valid for this file type.
bool VGMFileView::supportsViewKind(PanelViewKind viewKind) const {
  switch (viewKind) {
    case PanelViewKind::ActiveNotes:
    case PanelViewKind::PianoRoll:
      return m_isSeqFile;
    case PanelViewKind::Hex:
    case PanelViewKind::Tree:
      return true;
  }
  return false;
}

// True when sequence-only panes (Active Notes / Piano Roll) are available.
bool VGMFileView::supportsSequenceViews() const {
  return m_isSeqFile;
}

// Collapses/restores the right pane while preserving prior split sizes.
void VGMFileView::setSinglePaneMode(bool singlePane) {
  auto& rightPanel = panel(PanelSide::Right);
  if (!rightPanel.container) {
    return;
  }

  if (m_singlePaneMode == singlePane) {
    return;
  }
  m_singlePaneMode = singlePane;

  if (m_isSeqFile) {
    Settings::the()->VGMSeqFileView.setRightPaneHidden(singlePane);
  }

  if (singlePane) {
    ensurePreferredLeftPaneWidth();
    rightPanel.container->setVisible(false);
    m_splitter->setHandleWidth(0);
    setSplitterSizes(std::max(1, width()), 0);
  } else {
    rightPanel.container->setVisible(true);
    m_splitter->setHandleWidth(m_defaultSplitterHandleWidth);
    enforceSplitterPolicyForResize();
  }
}

// True when the right pane is currently collapsed.
bool VGMFileView::singlePaneMode() const {
  return m_singlePaneMode;
}

int VGMFileView::hexViewFullWidth() const {
  if (const auto* leftHex = panel(PanelSide::Left).hexView) {
    return leftHex->getViewportFullWidth();
  }
  return 1;
}

void VGMFileView::updateHexViewFont(qreal sizeIncrement) {
  if (!ensurePanelViewCreated(PanelSide::Left, PanelViewKind::Hex)) {
    return;
  }
  auto* leftHex = panel(PanelSide::Left).hexView;
  if (!leftHex) {
    return;
  }

  // Increment the font size until it has an actual effect on width.
  QFont font = leftHex->font();
  QFontMetricsF fontMetrics(font);
  const qreal origWidth = fontMetrics.horizontalAdvance("A");
  qreal fontSize = font.pointSizeF();
  for (int i = 0; i < 3; i++) {
    fontSize += sizeIncrement;
    font.setPointSizeF(fontSize);
    fontMetrics = QFontMetricsF(font);
    if (!qFuzzyCompare(fontMetrics.horizontalAdvance("A"), origWidth)) {
      break;
    }
  }

  applyHexViewFont(font);
}

void VGMFileView::applyHexViewFont(QFont font) {
  const QList<int> splitterSizes = m_splitter->sizes();
  const bool canAdjustSplitter =
      m_splitter && widget() == m_splitter && m_splitter->width() > 0 && !m_singlePaneMode;
  const int actualWidthBeforeResize =
      (canAdjustSplitter && !splitterSizes.isEmpty() && splitterSizes.first() > 0)
          ? splitterSizes.first()
          : hexViewFullWidth();
  const int fullWidthBeforeResize = std::max(1, hexViewFullWidth());

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    auto* hex = panel(side).hexView;
    if (!hex) {
      continue;
    }
    hex->setFont(font);
    hex->setMaximumWidth(hex->getViewportFullWidth());
  }

  const float percentHexViewVisible = static_cast<float>(actualWidthBeforeResize) /
                                      static_cast<float>(fullWidthBeforeResize);
  const int fullWidthAfterResize = std::max(1, hexViewFullWidth());
  const int widthChange = fullWidthAfterResize - fullWidthBeforeResize;
  const auto scaledWidthChange = static_cast<int>(
      std::round(static_cast<float>(widthChange) * percentHexViewVisible));
  const int newWidth = std::max(1, actualWidthBeforeResize + scaledWidthChange);
  if (!canAdjustSplitter) {
    return;
  }
  setLeftPaneWidth(newWidth, true);
  enforceSplitterPolicyForResize();
}

void VGMFileView::onSplitterMoved(int, int) {
  auto* splitterHandle = m_splitter ? m_splitter->handle(1) : nullptr;
  if (m_updatingSplitter || m_singlePaneMode || !splitterHandle ||
      !(QApplication::mouseButtons() & Qt::LeftButton) || !splitterHandle->underMouse()) {
    return;
  }

  const QList<int> sizes = m_splitter->sizes();
  if (sizes.size() < 2 || sizes.first() <= 0) {
    return;
  }

  int leftPaneWidth = std::clamp(sizes.first(), 1, maxLeftPaneWidth());
  leftPaneWidth = snapLeftPaneWidthForDrag(leftPaneWidth);
  leftPaneWidth = std::clamp(leftPaneWidth, 1, maxLeftPaneWidth());
  setLeftPaneWidth(leftPaneWidth, true);
}

void VGMFileView::ensurePreferredLeftPaneWidth() {
  if (m_preferredLeftPaneWidth > 0 || !m_splitter) {
    return;
  }

  const QList<int> sizes = m_splitter->sizes();
  if (sizes.size() >= 2 && sizes.first() > 0) {
    m_preferredLeftPaneWidth = sizes.first();
    return;
  }

  if (m_splitter->width() > 0) {
    m_preferredLeftPaneWidth = std::max(1, m_splitter->width() / 2);
    return;
  }

  m_preferredLeftPaneWidth = 1;
}

int VGMFileView::maxLeftPaneWidth() const {
  if (!m_splitter) {
    return 1;
  }

  const int maxWidth = m_splitter->width() - m_splitter->handleWidth() - rightPaneMinimumWidth;
  return std::max(1, maxWidth);
}

int VGMFileView::snapLeftPaneWidthForDrag(int leftPaneWidth) const {
  auto* snapProvider = leftPaneSnapProvider();
  if (!snapProvider) {
    return leftPaneWidth;
  }

  int snappedWidth = leftPaneWidth;
  for (const SplitterSnapRange& range : snapProvider->splitterSnapRanges()) {
    if (range.lowerBound >= range.upperBound) {
      continue;
    }

    const int halfway = range.upperBound + (range.lowerBound - range.upperBound) / 2;
    if (snappedWidth <= range.upperBound && snappedWidth > range.lowerBound) {
      snappedWidth = snappedWidth > halfway ? range.upperBound : range.lowerBound;
    }
  }
  return snappedWidth;
}

int VGMFileView::snapLeftPaneWidthForResize(int leftPaneWidth) const {
  auto* snapProvider = leftPaneSnapProvider();
  if (!snapProvider) {
    return leftPaneWidth;
  }

  int snappedWidth = leftPaneWidth;
  for (const SplitterSnapRange& range : snapProvider->splitterSnapRanges()) {
    if (range.lowerBound >= range.upperBound) {
      continue;
    }

    if (snappedWidth < range.upperBound && snappedWidth > range.lowerBound) {
      snappedWidth = range.lowerBound;
    }
  }
  return snappedWidth;
}

SplitterSnapProvider* VGMFileView::leftPaneSnapProvider() const {
  const auto& leftPanel = panel(PanelSide::Left);
  if (!leftPanel.stack) {
    return nullptr;
  }

  auto* currentWidget = leftPanel.stack->currentWidget();
  if (!currentWidget) {
    return nullptr;
  }

  return dynamic_cast<SplitterSnapProvider*>(currentWidget);
}

void VGMFileView::setSplitterSizes(int leftPaneWidth, int rightPaneWidth) {
  if (!m_splitter) {
    return;
  }

  m_updatingSplitter = true;
  m_splitter->setSizes(QList<int>{std::max(0, leftPaneWidth), std::max(0, rightPaneWidth)});
  m_updatingSplitter = false;
}

void VGMFileView::setLeftPaneWidth(int leftPaneWidth, bool persistWidth) {
  ensurePreferredLeftPaneWidth();
  m_preferredLeftPaneWidth = std::max(1, leftPaneWidth);

  if (!m_singlePaneMode && m_splitter) {
    const int clampedLeftPaneWidth = std::clamp(m_preferredLeftPaneWidth, 1, maxLeftPaneWidth());
    const int rightPaneWidth = std::max(0, m_splitter->width() - clampedLeftPaneWidth - m_splitter->handleWidth());
    setSplitterSizes(clampedLeftPaneWidth, rightPaneWidth);
  }

  if (persistWidth) {
    Settings::the()->VGMSeqFileView.setLeftPaneWidth(m_preferredLeftPaneWidth);
  }
}

void VGMFileView::enforceSplitterPolicyForResize() {
  if (!m_splitter || m_singlePaneMode || m_splitter->width() <= 0) {
    return;
  }

  ensurePreferredLeftPaneWidth();

  int leftPaneWidth = std::clamp(m_preferredLeftPaneWidth, 1, maxLeftPaneWidth());
  leftPaneWidth = snapLeftPaneWidthForResize(leftPaneWidth);
  leftPaneWidth = std::clamp(leftPaneWidth, 1, maxLeftPaneWidth());

  const int rightPaneWidth = std::max(0, m_splitter->width() - leftPaneWidth - m_splitter->handleWidth());
  setSplitterSizes(leftPaneWidth, rightPaneWidth);
}

void VGMFileView::resetHexViewFont() {
  applyHexViewFont(m_defaultHexFont);
}

void VGMFileView::increaseHexViewFont() {
  updateHexViewFont(+0.5);
}

void VGMFileView::decreaseHexViewFont() {
  updateHexViewFont(-0.5);
}

void VGMFileView::closeEvent(QCloseEvent*) {
  MdiArea::the()->removeView(m_vgmfile);
}

void VGMFileView::onSelectionChange(VGMItem* item) {
  std::vector<VGMItem*> selectionItems;
  if (item) {
    selectionItems.push_back(item);
  }
  onSelectionSetChange(selectionItems, item);
}

void VGMFileView::onSelectionSetChange(const std::vector<VGMItem*>& items, VGMItem* primaryItem) {
  std::vector<const VGMItem*> normalizedItems;
  normalizedItems.reserve(items.size());
  std::unordered_set<const VGMItem*> itemSet;
  itemSet.reserve(items.size() * 2 + 1);
  for (const auto* item : items) {
    if (!item || !itemSet.insert(item).second) {
      continue;
    }
    normalizedItems.push_back(item);
  }

  const VGMItem* normalizedPrimary = nullptr;
  if (primaryItem && itemSet.find(primaryItem) != itemSet.end()) {
    normalizedPrimary = primaryItem;
  } else if (!normalizedItems.empty()) {
    normalizedPrimary = normalizedItems.front();
  }
  m_selectedItems = normalizedItems;
  m_primarySelectedItem = normalizedPrimary;

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    if (auto* hex = panel(side).hexView) {
      hex->setSelectedItems(normalizedItems, normalizedPrimary);
    }
    if (auto* piano = panel(side).pianoRollView) {
      piano->setSelectedItems(normalizedItems, normalizedPrimary);
    }
  }

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    auto* tree = panel(side).treeView;
    if (!tree) {
      continue;
    }
    tree->setSelectedItems(normalizedItems, normalizedPrimary);
  }
}

bool VGMFileView::prepareSeqEventForPlayback(SeqEvent* event, uint32_t& tick) const {
  if (!event || !event->parentTrack || !event->parentTrack->parentSeq) {
    return false;
  }
  if (!ensureAssociatedCollectionActive()) {
    return false;
  }

  const auto& timeline = event->parentTrack->parentSeq->timedEventIndex();
  if (!timeline.finalized()) {
    return false;
  }
  if (!timeline.firstStartTick(event, tick)) {
    return false;
  }

  return true;
}

bool VGMFileView::ensureAssociatedCollectionActive() const {
  if (m_vgmfile->assocColls.empty()) {
    return true;
  }

  auto* assocColl = m_vgmfile->assocColls.front();
  auto& seqPlayer = SequencePlayer::the();
  if (seqPlayer.activeCollection() != assocColl) {
    seqPlayer.setActiveCollection(assocColl);
  }
  return seqPlayer.activeCollection() == assocColl;
}

void VGMFileView::seekToEvent(VGMItem* item) const {
  auto* event = dynamic_cast<SeqEvent*>(item);
  uint32_t tick = 0;
  if (!prepareSeqEventForPlayback(event, tick)) {
    return;
  }

  SequencePlayer::the().seek(static_cast<int>(tick), PositionChangeOrigin::HexView);
}

void VGMFileView::previewNotesForEvent(VGMItem* item, bool includeActiveNotesAtTick) const {
  auto* event = dynamic_cast<SeqEvent*>(item);
  uint32_t tick = 0;
  if (!prepareSeqEventForPlayback(event, tick)) {
    SequencePlayer::the().stopPreviewNote();
    return;
  }
  auto* seq = (event && event->parentTrack) ? event->parentTrack->parentSeq : nullptr;

  SequencePlayer::PreviewNote selectedNote;
  if (!buildPreviewNoteForEvent(event, selectedNote)) {
    SequencePlayer::the().stopPreviewNote();
    return;
  }
  const int selectedTransposedKey = SeqNoteUtils::transposedNoteKeyAtTick(seq, event, tick);
  if (selectedTransposedKey < 0) {
    SequencePlayer::the().stopPreviewNote();
    return;
  }
  selectedNote.key = static_cast<uint8_t>(selectedTransposedKey);

  std::vector<SequencePlayer::PreviewNote> previewNotes;
  previewNotes.reserve(includeActiveNotesAtTick ? 8 : 1);
  std::unordered_set<uint16_t> seenKeys;

  const auto noteKey = [](const SequencePlayer::PreviewNote& note) {
    return static_cast<uint16_t>((static_cast<uint16_t>(note.channel) << 8) | note.key);
  };

  previewNotes.push_back(selectedNote);
  seenKeys.emplace(noteKey(selectedNote));

  // Modifier-preview mode layers in every note currently active at this tick.
  if (includeActiveNotesAtTick && seq) {
    const auto& timeline = seq->timedEventIndex();
    if (timeline.finalized()) {
      std::vector<const SeqTimedEvent*> activeTimedEvents;
      timeline.getActiveAt(tick, activeTimedEvents);
      for (const auto* timed : activeTimedEvents) {
        if (!timed || !timed->event) {
          continue;
        }
        SequencePlayer::PreviewNote activeNote;
        if (!buildPreviewNoteForEvent(timed->event, activeNote)) {
          continue;
        }
        const int transposedKey = SeqNoteUtils::transposedNoteKeyForTimedEvent(seq, timed);
        if (transposedKey < 0) {
          continue;
        }
        activeNote.key = static_cast<uint8_t>(transposedKey);
        if (!seenKeys.emplace(noteKey(activeNote)).second) {
          continue;
        }
        previewNotes.push_back(activeNote);
      }
    }
  }

  SequencePlayer::the().previewNotesAtTick(previewNotes, tick);
}

void VGMFileView::previewPianoRollNotes(const std::vector<PianoRollView::PreviewSelection>& notes,
                                        int tick) const {
  if (!ensureAssociatedCollectionActive()) {
    SequencePlayer::the().stopPreviewNote();
    return;
  }

  std::vector<SequencePlayer::PreviewNote> previewNotes;
  previewNotes.reserve(notes.size());
  std::unordered_set<uint16_t> seenKeys;
  seenKeys.reserve(notes.size() * 2 + 1);
  const auto noteKey = [](const SequencePlayer::PreviewNote& note) {
    return static_cast<uint16_t>((static_cast<uint16_t>(note.channel) << 8) | note.key);
  };

  auto appendPreviewNote = [&](const PianoRollView::PreviewSelection& selection) {
    const auto* seqEvent = dynamic_cast<const SeqEvent*>(selection.item);
    if (!seqEvent) {
      return;
    }
    SequencePlayer::PreviewNote note;
    if (!buildPreviewNoteForEvent(seqEvent, note)) {
      return;
    }
    if (selection.key >= 0 && selection.key < 128) {
      note.key = static_cast<uint8_t>(selection.key);
    }
    if (!seenKeys.emplace(noteKey(note)).second) {
      return;
    }
    previewNotes.push_back(note);
  };

  for (const auto& selection : notes) {
    appendPreviewNote(selection);
  }

  if (previewNotes.empty()) {
    SequencePlayer::the().stopPreviewNote();
    return;
  }

  const uint32_t clampedTick = static_cast<uint32_t>(std::max(0, tick));
  SequencePlayer::the().updatePreviewNotesAtTick(previewNotes, clampedTick);
}

void VGMFileView::stopNotePreview() const {
  SequencePlayer::the().stopPreviewNote();
}

void VGMFileView::previewActiveNote(int trackIndex, int key) const {
  if (key < 0 || key >= 128) {
    SequencePlayer::the().stopPreviewNote();
    return;
  }

  auto* seq = dynamic_cast<VGMSeq*>(m_vgmfile);
  if (!seq) {
    SequencePlayer::the().stopPreviewNote();
    return;
  }

  auto& seqPlayer = SequencePlayer::the();
  if (!ensureAssociatedCollectionActive()) {
    seqPlayer.stopPreviewNote();
    return;
  }

  const int midiChannel = previewMidiChannelForTrack(seq, trackIndex);
  if (midiChannel < 0) {
    seqPlayer.stopPreviewNote();
    return;
  }

  const int tick = std::max(0, seqPlayer.elapsedTicks());
  seqPlayer.previewNoteOn(static_cast<uint8_t>(midiChannel),
                          static_cast<uint8_t>(key),
                          kActiveNotePreviewVelocity,
                          static_cast<uint32_t>(tick));
}

void VGMFileView::clearPlaybackVisuals() {
  m_playbackVisualsActive = false;
  m_playbackTickActive = false;
  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    if (auto* hex = panel(side).hexView) {
      hex->setPlaybackActive(false);
      hex->clearPlaybackSelections(false);
    }
    if (auto* tree = panel(side).treeView) {
      tree->setPlaybackItems({});
    }
    if (auto* active = panel(side).activeNoteView) {
      active->clearPlaybackState();
    }
    if (auto* piano = panel(side).pianoRollView) {
      piano->clearPlaybackState();
    }
  }
}

void VGMFileView::ensureTrackIndexMap(VGMSeq* seq) {
  if (!seq) {
    m_trackIndexSeq = nullptr;
    m_trackIndexByPtr.clear();
    m_trackIndexByMidiPtr.clear();
    return;
  }

  if (m_trackIndexSeq == seq && m_trackIndexByPtr.size() == seq->aTracks.size()) {
    return;
  }

  m_trackIndexSeq = seq;
  m_trackIndexByPtr.clear();
  m_trackIndexByMidiPtr.clear();
  for (size_t i = 0; i < seq->aTracks.size(); ++i) {
    auto* track = seq->aTracks[i];
    if (track) {
      const int trackIndex = static_cast<int>(i);
      m_trackIndexByPtr[track] = trackIndex;
      if (track->pMidiTrack) {
        m_trackIndexByMidiPtr.emplace(track->pMidiTrack, trackIndex);
      }
    }
  }
}

int VGMFileView::trackIndexForEvent(const SeqEvent* event) const {
  if (!event) {
    return -1;
  }

  if (event->parentTrack) {
    const auto trackIt = m_trackIndexByPtr.find(event->parentTrack);
    if (trackIt != m_trackIndexByPtr.end()) {
      return trackIt->second;
    }
    if (event->parentTrack->pMidiTrack) {
      const auto midiIt = m_trackIndexByMidiPtr.find(event->parentTrack->pMidiTrack);
      if (midiIt != m_trackIndexByMidiPtr.end()) {
        return midiIt->second;
      }
    }
  }

  return static_cast<int>(event->channel);
}

int VGMFileView::effectiveTrackCountForSeq(VGMSeq* seq) const {
  if (!seq) {
    return 0;
  }

  int trackCount = static_cast<int>(seq->aTracks.size());
  if (trackCount > 0) {
    return trackCount;
  }

  if (seq->nNumTracks > 0) {
    trackCount = static_cast<int>(seq->nNumTracks);
  }

  const auto& timeline = seq->timedEventIndex();
  if (timeline.finalized()) {
    int maxTrackIndex = trackCount - 1;
    for (size_t i = 0; i < timeline.size(); ++i) {
      const auto& timed = timeline.event(i);
      if (!timed.event) {
        continue;
      }
      maxTrackIndex = std::max(maxTrackIndex, static_cast<int>(timed.event->channel));
    }
    trackCount = std::max(trackCount, maxTrackIndex + 1);
  }

  return std::max(trackCount, 1);
}

void VGMFileView::onPlaybackPositionChanged(int current, int max, PositionChangeOrigin origin) {
  Q_UNUSED(max);

  if (!isVisible()) {
    return;
  }

  const bool anyPanelVisible =
      (panel(PanelSide::Left).container && panel(PanelSide::Left).container->isVisible()) ||
      (panel(PanelSide::Right).container && panel(PanelSide::Right).container->isVisible());
  if (!anyPanelVisible) {
    return;
  }

  auto* seq = dynamic_cast<VGMSeq*>(m_vgmfile);
  if (!seq) {
    m_playbackVisualsActive = false;
    clearPlaybackVisuals();
    return;
  }

  const auto* coll = SequencePlayer::the().activeCollection();
  if (!coll || !coll->containsVGMFile(m_vgmfile)) {
    m_playbackVisualsActive = false;
    clearPlaybackVisuals();
    return;
  }

  const bool shouldHighlight = SequencePlayer::the().playing() || current > 0;
  if (!shouldHighlight) {
    m_playbackVisualsActive = false;
    m_playbackTickActive = false;
    clearPlaybackVisuals();
    return;
  }

  const bool playbackActive = current > 0;
  m_playbackVisualsActive = true;
  m_playbackTickActive = playbackActive;
  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    const auto& panelUi = panel(side);
    if (!panelUi.container || !panelUi.container->isVisible()) {
      continue;
    }
    if (panelUi.hexView) {
      panelUi.hexView->setPlaybackActive(playbackActive);
    }
  }

  const auto& timeline = seq->timedEventIndex();
  if (!timeline.finalized()) {
    m_playbackCursor.reset();
    m_playbackTimeline = nullptr;
    m_playbackVisualsActive = false;
    m_playbackTickActive = false;
    clearPlaybackVisuals();
    return;
  }

  if (m_playbackTimeline != &timeline || !m_playbackCursor) {
    m_playbackTimeline = &timeline;
    m_playbackCursor = std::make_unique<SeqEventTimeIndex::Cursor>(timeline);
  }

  const int tickDiff = current - m_lastPlaybackPosition;
  m_playbackTimedEvents.clear();
  switch (origin) {
    case PositionChangeOrigin::Playback:
      if (tickDiff <= 20) {
        m_playbackCursor->getActiveInRange(m_lastPlaybackPosition, current, m_playbackTimedEvents);
      } else {
        m_playbackCursor->getActiveAt(current, m_playbackTimedEvents);
      }
      break;
    case PositionChangeOrigin::SeekBar:
      // Distinguish between dragging and long seeks. Dragging highlights passed-through events.
      if (tickDiff >= -500 && tickDiff <= 500) {
        const int lesser = std::min(m_lastPlaybackPosition, current);
        const int greater = std::max(m_lastPlaybackPosition, current);
        m_playbackCursor->getActiveInRange(lesser, greater, m_playbackTimedEvents);

        // Immediately follow up with a current-tick-only pass so skip-faded events can settle.
        QTimer::singleShot(0, this, [this, current, max] {
          onPlaybackPositionChanged(current, max, PositionChangeOrigin::HexView);
        });
      } else {
        m_playbackCursor->getActiveAt(current, m_playbackTimedEvents);
      }
      break;
    case PositionChangeOrigin::HexView:
      m_playbackCursor->getActiveAt(current, m_playbackTimedEvents);
      break;
  }
  m_lastPlaybackPosition = current;

  m_playbackItems.clear();
  m_playbackItems.reserve(m_playbackTimedEvents.size());
  for (const auto* timed : m_playbackTimedEvents) {
    if (!timed || !timed->event) {
      continue;
    }
    m_playbackItems.push_back(timed->event);
  }

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    const auto& panelUi = panel(side);
    if (!panelUi.container || !panelUi.container->isVisible()) {
      continue;
    }
    if (panelUi.treeView) {
      panelUi.treeView->setPlaybackItems(m_playbackItems);
    }
  }

  if (m_playbackItems == m_lastPlaybackItems) {
    for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
      const auto& panelUi = panel(side);
      if (!panelUi.container || !panelUi.container->isVisible()) {
        continue;
      }
      if (panelUi.hexView) {
        panelUi.hexView->requestPlaybackFrame();
      }
    }
  } else {
    m_lastPlaybackItems = m_playbackItems;
    for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
      const auto& panelUi = panel(side);
      if (!panelUi.container || !panelUi.container->isVisible()) {
        continue;
      }
      if (panelUi.hexView) {
        panelUi.hexView->setPlaybackSelectionsForItems(m_playbackItems);
      }
    }
  }

  m_activeTimedEvents.clear();
  m_playbackCursor->getActiveAt(current, m_activeTimedEvents);

  ensureTrackIndexMap(seq);
  const int effectiveTrackCount = effectiveTrackCountForSeq(seq);
  std::vector<ActiveNoteView::ActiveKey> activeKeys;
  activeKeys.reserve(m_activeTimedEvents.size());
  for (const auto* timed : m_activeTimedEvents) {
    if (!timed || !timed->event) {
      continue;
    }

    const int noteKey = SeqNoteUtils::transposedNoteKeyForTimedEvent(seq, timed);
    if (noteKey < 0) {
      continue;
    }

    const int trackIndex = trackIndexForEvent(timed->event);
    if (trackIndex < 0) {
      continue;
    }

    activeKeys.push_back({trackIndex, noteKey});
  }

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    const auto& panelUi = panel(side);
    if (!panelUi.container || !panelUi.container->isVisible()) {
      continue;
    }
    if (panelUi.activeNoteView && panelUi.activeNoteView->isVisible()) {
      panelUi.activeNoteView->setTrackCount(effectiveTrackCount);
      panelUi.activeNoteView->setActiveNotes(activeKeys, playbackActive);
    }
    if (panelUi.pianoRollView && panelUi.pianoRollView->isVisible()) {
      panelUi.pianoRollView->setTrackCount(effectiveTrackCount);
      panelUi.pianoRollView->setSequence(seq);
      panelUi.pianoRollView->refreshSequenceData(false);
      panelUi.pianoRollView->setPlaybackTick(current, playbackActive);
    }
  }
}

void VGMFileView::onPlayerStatusChanged(bool playing) {
  if (playing) {
    return;
  }
  if (SequencePlayer::the().activeCollection() != nullptr) {
    return;
  }

  m_playbackTimedEvents.clear();
  m_activeTimedEvents.clear();
  m_playbackItems.clear();
  m_lastPlaybackItems.clear();
  m_lastPlaybackPosition = 0;
  m_playbackVisualsActive = false;
  m_playbackTickActive = false;
  m_playbackCursor.reset();
  m_playbackTimeline = nullptr;
  m_trackIndexSeq = nullptr;
  m_trackIndexByPtr.clear();
  m_trackIndexByMidiPtr.clear();

  clearPlaybackVisuals();
}
