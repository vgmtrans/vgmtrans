/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QApplication>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <utility>

#include "VGMFileView.h"
#include "MdiArea.h"
#include "SequencePlayer.h"
#include "SnappingSplitter.h"
#include "VGMFile.h"
#include "VGMFileTreeView.h"
#include "VGMColl.h"
#include "VGMSeq.h"
#include "activenoteview/ActiveNoteView.h"
#include "hexview/HexView.h"
#include "pianorollview/PianoRollView.h"
#include "Helpers.h"
#include "Root.h"
#include "SeqEvent.h"
#include "SeqTrack.h"

namespace {
QToolButton* createPanelButton(const QString& label, QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setText(label);
  button->setCheckable(true);
  button->setAutoRaise(true);
  button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  return button;
}
}  // namespace

VGMFileView::VGMFileView(VGMFile* vgmfile)
    : QMdiSubWindow(), m_vgmfile(vgmfile) {
  m_splitter = new SnappingSplitter(Qt::Horizontal, this);

  setWindowTitle(QString::fromStdString(m_vgmfile->name()));
  setWindowIcon(iconForFile(vgmFileToVariant(vgmfile)));
  setAttribute(Qt::WA_DeleteOnClose);
  // Keep a stable default cursor across MDI tabs; embedded RHI windows can leak resize cursors.
  setCursor(Qt::ArrowCursor);

  const bool isSeqFile = dynamic_cast<VGMSeq*>(m_vgmfile) != nullptr;

  panel(PanelSide::Left) = createPanel(PanelSide::Left, isSeqFile);
  panel(PanelSide::Right) = createPanel(PanelSide::Right, isSeqFile);

  m_splitter->addWidget(panel(PanelSide::Left).container);
  m_splitter->addWidget(panel(PanelSide::Right).container);
  m_splitter->setStretchFactor(0, 1);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->setSizes(QList<int>{1, 1});
  m_splitter->persistState();
  m_defaultSplitterHandleWidth = m_splitter->handleWidth();
  resetSnapRanges();

  m_defaultHexFont = panel(PanelSide::Left).hexView->font();
  applyHexViewFont(m_defaultHexFont);

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    auto& panelUi = panel(side);

    connect(panelUi.hexView, &HexView::selectionChanged, this, &VGMFileView::onSelectionChange);
    connect(panelUi.hexView, &HexView::seekToEventRequested, this, &VGMFileView::seekToEvent);
    connect(panelUi.pianoRollView, &PianoRollView::selectionChanged, this, &VGMFileView::onSelectionChange);

    connect(panelUi.treeView, &VGMFileTreeView::currentItemChanged,
            this,
            [this](const QTreeWidgetItem* item, QTreeWidgetItem*) {
              if (item == nullptr) {
                onSelectionChange(nullptr);
                return;
              }
              auto* vgmitem = static_cast<VGMItem*>(item->data(0, Qt::UserRole).value<void*>());
              onSelectionChange(vgmitem);
            });

    connect(panelUi.viewButtons,
            &QButtonGroup::idClicked,
            this,
            [this, side](int id) {
              setPanelView(side, static_cast<PanelViewKind>(id));
            });
  }

  connect(panel(PanelSide::Left).singlePaneToggle,
          &QToolButton::toggled,
          this,
          &VGMFileView::setSinglePaneMode);

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
}

VGMFileView::PanelUi VGMFileView::createPanel(PanelSide side, bool isSeqFile) {
  PanelUi panelUi;

  panelUi.container = new QWidget(this);
  auto* containerLayout = new QVBoxLayout(panelUi.container);
  containerLayout->setContentsMargins(0, 0, 0, 0);
  containerLayout->setSpacing(0);

  auto* toolbar = new QWidget(panelUi.container);
  auto* toolbarLayout = new QHBoxLayout(toolbar);
  toolbarLayout->setContentsMargins(4, 4, 4, 4);
  toolbarLayout->setSpacing(4);

  if (side == PanelSide::Left) {
    auto* singlePaneToggle = createPanelButton("1 Pane", toolbar);
    singlePaneToggle->setToolTip("Hide the right panel and show only the left panel");
    panelUi.singlePaneToggle = singlePaneToggle;
    toolbarLayout->addWidget(singlePaneToggle);
  }

  panelUi.viewButtons = new QButtonGroup(panelUi.container);
  panelUi.viewButtons->setExclusive(true);

  auto* hexButton = createPanelButton("Hex", toolbar);
  auto* treeButton = createPanelButton("Tree", toolbar);
  auto* activeButton = createPanelButton("Active", toolbar);
  auto* pianoButton = createPanelButton("Piano", toolbar);
  activeButton->setEnabled(isSeqFile);
  pianoButton->setEnabled(isSeqFile);

  panelUi.viewButtons->addButton(hexButton, static_cast<int>(PanelViewKind::Hex));
  panelUi.viewButtons->addButton(treeButton, static_cast<int>(PanelViewKind::Tree));
  panelUi.viewButtons->addButton(activeButton, static_cast<int>(PanelViewKind::ActiveNotes));
  panelUi.viewButtons->addButton(pianoButton, static_cast<int>(PanelViewKind::PianoRoll));

  toolbarLayout->addWidget(hexButton);
  toolbarLayout->addWidget(treeButton);
  toolbarLayout->addWidget(activeButton);
  toolbarLayout->addWidget(pianoButton);
  toolbarLayout->addStretch(1);

  panelUi.stack = new QStackedWidget(panelUi.container);
  panelUi.hexView = new HexView(m_vgmfile, panelUi.stack);
  panelUi.treeView = new VGMFileTreeView(m_vgmfile, panelUi.stack);
  panelUi.activeNoteView = new ActiveNoteView(panelUi.stack);
  panelUi.pianoRollView = new PianoRollView(panelUi.stack);

  panelUi.hexView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  panelUi.treeView->setMinimumWidth(treeViewMinimumWidth);

  if (isSeqFile) {
    auto* seq = dynamic_cast<VGMSeq*>(m_vgmfile);
    panelUi.activeNoteView->setTrackCount(seq ? static_cast<int>(seq->aTracks.size()) : 0);
    panelUi.pianoRollView->setTrackCount(seq ? static_cast<int>(seq->aTracks.size()) : 0);
    panelUi.pianoRollView->setSequence(seq);
  }

  if (panelUi.pianoRollView) {
    connect(panelUi.pianoRollView, &PianoRollView::seekRequested, this, [this](int tick) {
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
  }

  panelUi.stack->addWidget(panelUi.hexView);
  panelUi.stack->addWidget(panelUi.treeView);
  panelUi.stack->addWidget(panelUi.activeNoteView);
  panelUi.stack->addWidget(panelUi.pianoRollView);

  if (side == PanelSide::Left) {
    panelUi.currentKind = PanelViewKind::Hex;
  } else {
    panelUi.currentKind = PanelViewKind::Tree;
  }
  panelUi.stack->setCurrentIndex(static_cast<int>(panelUi.currentKind));

  panelUi.viewButtons->button(static_cast<int>(panelUi.currentKind))->setChecked(true);

  containerLayout->addWidget(toolbar);
  containerLayout->addWidget(panelUi.stack, 1);

  return panelUi;
}

void VGMFileView::focusInEvent(QFocusEvent* event) {
  QMdiSubWindow::focusInEvent(event);

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

  panel(PanelSide::Left).treeView->updateStatusBar();
}

void VGMFileView::setPanelView(PanelSide side, PanelViewKind viewKind) {
  auto& panelUi = panel(side);
  if (panelUi.currentKind == viewKind) {
    return;
  }

  panelUi.currentKind = viewKind;
  panelUi.stack->setCurrentIndex(static_cast<int>(viewKind));

  if (viewKind == PanelViewKind::Tree && panelUi.treeView) {
    panelUi.treeView->updateStatusBar();
  } else if (viewKind == PanelViewKind::PianoRoll && panelUi.pianoRollView) {
    panelUi.pianoRollView->refreshSequenceData(true);
  }
}

void VGMFileView::setSinglePaneMode(bool singlePane) {
  auto& rightPanel = panel(PanelSide::Right);
  if (!rightPanel.container) {
    return;
  }

  if (singlePane) {
    m_lastSplitSizes = m_splitter->sizes();
    rightPanel.container->setVisible(false);
    m_splitter->setHandleWidth(0);
    m_splitter->setSizes(QList<int>{std::max(1, width()), 0});
  } else {
    rightPanel.container->setVisible(true);
    m_splitter->setHandleWidth(m_defaultSplitterHandleWidth);
    if (!m_lastSplitSizes.isEmpty() && m_lastSplitSizes.size() == 2) {
      m_splitter->setSizes(m_lastSplitSizes);
    } else {
      m_splitter->setSizes(QList<int>{1, 1});
    }
  }
  m_splitter->persistState();
}

void VGMFileView::resetSnapRanges() const {
  if (!m_splitter) {
    return;
  }

  m_splitter->clearSnapRanges();
  m_splitter->addSnapRange(0, hexViewWidthSansAsciiAndAddress(), hexViewWidthSansAscii());
  m_splitter->addSnapRange(0, hexViewWidthSansAscii(), hexViewFullWidth());
}

int VGMFileView::hexViewFullWidth() const {
  return panel(PanelSide::Left).hexView->getViewportFullWidth();
}

int VGMFileView::hexViewWidthSansAscii() const {
  return panel(PanelSide::Left).hexView->getViewportWidthSansAscii();
}

int VGMFileView::hexViewWidthSansAsciiAndAddress() const {
  return panel(PanelSide::Left).hexView->getViewportWidthSansAsciiAndAddress();
}

void VGMFileView::updateHexViewFont(qreal sizeIncrement) const {
  // Increment the font size until it has an actual effect on width.
  QFont font = panel(PanelSide::Left).hexView->font();
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

void VGMFileView::applyHexViewFont(QFont font) const {
  const QList<int> splitterSizes = m_splitter->sizes();
  const int actualWidthBeforeResize = splitterSizes.isEmpty() ? hexViewFullWidth() : splitterSizes.first();
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
  resetSnapRanges();
  m_splitter->setSizes(QList<int>{newWidth, treeViewMinimumWidth});
  m_splitter->persistState();
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

void VGMFileView::onSelectionChange(VGMItem* item) const {
  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    if (auto* hex = panel(side).hexView) {
      hex->setSelectedItem(item);
    }
  }

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    auto* tree = panel(side).treeView;
    if (!tree) {
      continue;
    }
    if (item) {
      auto* widgetItem = tree->getTreeWidgetItem(item);
      tree->blockSignals(true);
      tree->setCurrentItem(widgetItem);
      tree->blockSignals(false);
    } else {
      tree->setCurrentItem(nullptr);
      tree->clearSelection();
    }
  }
}

void VGMFileView::seekToEvent(VGMItem* item) const {
  auto* event = dynamic_cast<SeqEvent*>(item);
  if (!event || !event->parentTrack || !event->parentTrack->parentSeq) {
    return;
  }
  if (!m_vgmfile->assocColls.empty()) {
    auto* assocColl = m_vgmfile->assocColls.front();
    if (SequencePlayer::the().activeCollection() != assocColl) {
      auto& seqPlayer = SequencePlayer::the();
      seqPlayer.setActiveCollection(assocColl);
    }
  }

  const auto& timeline = event->parentTrack->parentSeq->timedEventIndex();
  if (!timeline.finalized()) {
    return;
  }
  uint32_t tick = 0;
  if (!timeline.firstStartTick(event, tick)) {
    return;
  }

  SequencePlayer::the().seek(static_cast<int>(tick), PositionChangeOrigin::HexView);
}

void VGMFileView::clearPlaybackVisuals() {
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
    return;
  }

  if (m_trackIndexSeq == seq && m_trackIndexByPtr.size() == seq->aTracks.size()) {
    return;
  }

  m_trackIndexSeq = seq;
  m_trackIndexByPtr.clear();
  for (size_t i = 0; i < seq->aTracks.size(); ++i) {
    auto* track = seq->aTracks[i];
    if (track) {
      m_trackIndexByPtr[track] = static_cast<int>(i);
    }
  }
}

int VGMFileView::noteKeyForEvent(const SeqEvent* event) {
  if (!event) {
    return -1;
  }
  if (auto* noteOn = dynamic_cast<const NoteOnSeqEvent*>(event)) {
    return noteOn->absKey;
  }
  if (auto* durNote = dynamic_cast<const DurNoteSeqEvent*>(event)) {
    return durNote->absKey;
  }
  return -1;
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
    clearPlaybackVisuals();
    return;
  }

  const auto* coll = SequencePlayer::the().activeCollection();
  if (!coll || !coll->containsVGMFile(m_vgmfile)) {
    clearPlaybackVisuals();
    return;
  }

  const bool shouldHighlight = SequencePlayer::the().playing() || current > 0;
  if (!shouldHighlight) {
    clearPlaybackVisuals();
    return;
  }

  const bool playbackActive = current > 0;
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
  std::vector<ActiveNoteView::ActiveKey> activeKeys;
  activeKeys.reserve(m_activeTimedEvents.size());
  for (const auto* timed : m_activeTimedEvents) {
    if (!timed || !timed->event || !timed->event->parentTrack) {
      continue;
    }

    const int noteKey = noteKeyForEvent(timed->event);
    if (noteKey < 0 || noteKey >= 128) {
      continue;
    }

    const auto trackIt = m_trackIndexByPtr.find(timed->event->parentTrack);
    if (trackIt == m_trackIndexByPtr.end()) {
      continue;
    }

    activeKeys.push_back({trackIt->second, noteKey});
  }

  for (PanelSide side : {PanelSide::Left, PanelSide::Right}) {
    const auto& panelUi = panel(side);
    if (!panelUi.container || !panelUi.container->isVisible()) {
      continue;
    }
    if (panelUi.activeNoteView && panelUi.activeNoteView->isVisible()) {
      panelUi.activeNoteView->setTrackCount(static_cast<int>(seq->aTracks.size()));
      panelUi.activeNoteView->setActiveNotes(activeKeys, playbackActive);
    }
    if (panelUi.pianoRollView && panelUi.pianoRollView->isVisible()) {
      panelUi.pianoRollView->setTrackCount(static_cast<int>(seq->aTracks.size()));
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
  m_playbackCursor.reset();
  m_playbackTimeline = nullptr;
  m_trackIndexSeq = nullptr;
  m_trackIndexByPtr.clear();

  clearPlaybackVisuals();
}
