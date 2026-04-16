/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"

#include "base/Types.h"
#include "Helpers.h"
#include "hexview/HexView.h"
#include "MdiArea.h"
#include "Root.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"
#include "SeqTrack.h"
#include "SequencePlayer.h"
#include "SnappingSplitter.h"
#include "VGMColl.h"
#include "VGMFile.h"
#include "VGMFileTreeView.h"
#include "VGMSeq.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include <QApplication>
#include <QShortcut>
#include <QtGlobal>

namespace {
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

bool buildPreviewNoteForEvent(const SeqEvent* event, SequencePlayer::PreviewNote& outNote) {
  if (!event) {
    return false;
  }

  uint8_t key = 0;
  uint8_t velocity = 0;
  if (const auto* noteOn = dynamic_cast<const NoteOnSeqEvent*>(event)) {
    key = noteOn->absKey;
    velocity = noteOn->vel;
  } else if (const auto* durNote = dynamic_cast<const DurNoteSeqEvent*>(event)) {
    key = durNote->absKey;
    velocity = durNote->vel;
  } else {
    return false;
  }

  const int midiChannel = previewMidiChannelForEvent(event);
  if (midiChannel < 0) {
    return false;
  }

  if (event->parentTrack && event->parentTrack->usesLinearAmplitudeScale()) {
    velocity = convert7bitPercentAmpToStdMidiVal(velocity);
  }

  outNote.channel = static_cast<uint8_t>(midiChannel);
  outNote.key = key;
  outNote.velocity = velocity;
  return true;
}

uint16_t previewChannelAndKey(const SequencePlayer::PreviewNote& note) {
  return static_cast<uint16_t>((static_cast<uint16_t>(note.channel) << 8) | note.key);
}
}  // namespace

VGMFileView::VGMFileView(VGMFile *vgmfile)
    : QMdiSubWindow(), m_vgmfile(vgmfile), m_hexview(new HexView(vgmfile)) {
  m_splitter = new SnappingSplitter(Qt::Horizontal, this);

  setWindowTitle(QString::fromStdString(m_vgmfile->name()));
  setWindowIcon(iconForFile(vgmFileToVariant(vgmfile)));
  setAttribute(Qt::WA_DeleteOnClose);
  // Keep a stable default cursor across MDI tabs; embedded RHI windows can leak resize cursors.
  setCursor(Qt::ArrowCursor);

  m_treeview = new VGMFileTreeView(m_vgmfile, this);

  m_hexview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_splitter->addWidget(m_hexview);
  m_splitter->addWidget(m_treeview);
  m_splitter->setSizes(QList<int>{hexViewFullWidth(), treeViewMinimumWidth});
  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);
  m_splitter->persistState();
  resetSnapRanges();
  m_hexview->setMaximumWidth(hexViewFullWidth());
  m_treeview->setMinimumWidth(treeViewMinimumWidth);

  m_defaultHexFont = m_hexview->font();

  connect(m_hexview, &HexView::selectionChanged, this, &VGMFileView::onSelectionChange);
  connect(m_hexview, &HexView::seekToEventRequested, this, &VGMFileView::seekToEvent);
  connect(m_hexview, &HexView::notePreviewRequested, this, &VGMFileView::previewNotesForEvent);
  connect(m_hexview, &HexView::notePreviewStopped, this, &VGMFileView::stopNotePreview);

  connect(m_treeview, &VGMFileTreeView::currentItemChanged,
          [&](const QTreeWidgetItem *item, QTreeWidgetItem *) {
            if (item == nullptr) {
              // If the VGMFileTreeView deselected, then so should the HexView
              onSelectionChange(nullptr);
              return;
            }
            auto vgmitem = static_cast<VGMItem *>(item->data(0, Qt::UserRole).value<void *>());
            onSelectionChange(vgmitem);
          });

  connect(new QShortcut(QKeySequence::ZoomIn, this), &QShortcut::activated,
          this, &VGMFileView::increaseHexViewFont);

  auto *shortcutEqual = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
  connect(shortcutEqual, &QShortcut::activated, this, &VGMFileView::increaseHexViewFont);

  connect(new QShortcut(QKeySequence::ZoomOut, this), &QShortcut::activated,
          this, &VGMFileView::decreaseHexViewFont);

  connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this), &QShortcut::activated,
          this, &VGMFileView::resetHexViewFont);

  connect(&SequencePlayer::the(), &SequencePlayer::playbackPositionChanged,
          this, &VGMFileView::onPlaybackPositionChanged);
  connect(&SequencePlayer::the(), &SequencePlayer::statusChange,
          this, &VGMFileView::onPlayerStatusChanged);

  setWidget(m_splitter);
}

void VGMFileView::focusInEvent(QFocusEvent* event) {
  QMdiSubWindow::focusInEvent(event);

  m_treeview->updateStatusBar();
}

void VGMFileView::resetSnapRanges() const {
  m_splitter->clearSnapRanges();
  m_splitter->addSnapRange(0, hexViewWidthSansAsciiAndAddress(), hexViewWidthSansAscii());
  m_splitter->addSnapRange(0, hexViewWidthSansAscii(), hexViewFullWidth());
}

int VGMFileView::hexViewFullWidth() const {
  return m_hexview->getViewportFullWidth();
}

int VGMFileView::hexViewWidthSansAscii() const {
  return m_hexview->getViewportWidthSansAscii();
}

int VGMFileView::hexViewWidthSansAsciiAndAddress() const {
  return m_hexview->getViewportWidthSansAsciiAndAddress();
}

void VGMFileView::updateHexViewFont(qreal sizeIncrement) const {
  // Increment the font size until it has an actual effect on width
  QFont font = m_hexview->font();
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

  m_hexview->setFont(font);
  m_hexview->setMaximumWidth(hexViewFullWidth());

  const float percentHexViewVisible = static_cast<float>(actualWidthBeforeResize) /
                                      static_cast<float>(fullWidthBeforeResize);
  const int fullWidthAfterResize = std::max(1, hexViewFullWidth());
  const int widthChange = fullWidthAfterResize - fullWidthBeforeResize;
  const auto scaledWidthChange = static_cast<int>(std::round(static_cast<float>(widthChange) * percentHexViewVisible));
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

void VGMFileView::closeEvent(QCloseEvent *) {
  MdiArea::the()->removeView(m_vgmfile);
}

void VGMFileView::onSelectionChange(VGMItem *item) const {
  m_hexview->setSelectedItem(item);
  if (item) {
    auto widget_item = m_treeview->getTreeWidgetItem(item);
    m_treeview->blockSignals(true);
    m_treeview->setCurrentItem(widget_item);
    m_treeview->blockSignals(false);
  } else {
    m_treeview->setCurrentItem(nullptr);
    m_treeview->clearSelection();
  }
}

bool VGMFileView::prepareSeqEventForPlayback(SeqEvent* event, uint32_t& tick) const {
  if (!event || !event->parentTrack || !event->parentTrack->parentSeq) {
    return false;
  }
  if (!m_vgmfile->assocColls.empty()) {
    auto* assocColl = m_vgmfile->assocColls.front();
    auto& seqPlayer = SequencePlayer::the();
    if (seqPlayer.activeCollection() != assocColl) {
      seqPlayer.setActiveCollection(assocColl);
    }
    if (seqPlayer.activeCollection() != assocColl) {
      return false;
    }
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

  std::vector<SequencePlayer::PreviewNote> previewNotes;
  previewNotes.reserve(includeActiveNotesAtTick ? 8 : 1);
  std::unordered_set<uint16_t> seenChannelAndKeys;

  const auto appendPreviewNote = [&](const SeqEvent* seqEvent) {
    SequencePlayer::PreviewNote note;
    if (!buildPreviewNoteForEvent(seqEvent, note)) {
      return false;
    }
    if (!seenChannelAndKeys.emplace(previewChannelAndKey(note)).second) {
      return true;
    }
    previewNotes.push_back(note);
    return true;
  };

  if (!appendPreviewNote(event)) {
    SequencePlayer::the().stopPreviewNote();
    return;
  }

  if (includeActiveNotesAtTick && event->parentTrack && event->parentTrack->parentSeq) {
    const auto& timeline = event->parentTrack->parentSeq->timedEventIndex();
    if (timeline.finalized()) {
      std::vector<const SeqTimedEvent*> activeTimedEvents;
      timeline.getActiveAt(tick, activeTimedEvents);
      for (const auto* timed : activeTimedEvents) {
        if (!timed || !timed->event) {
          continue;
        }
        appendPreviewNote(timed->event);
      }
    }
  }

  SequencePlayer::the().previewNotesAtTick(previewNotes, tick);
}

void VGMFileView::stopNotePreview() const {
  SequencePlayer::the().stopPreviewNote();
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
    if (!track) {
      continue;
    }
    const int trackIndex = static_cast<int>(i);
    m_trackIndexByPtr[track] = trackIndex;
    if (track->pMidiTrack) {
      m_trackIndexByMidiPtr.emplace(track->pMidiTrack, trackIndex);
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

void VGMFileView::onPlaybackPositionChanged(int current, int max, PositionChangeOrigin origin) {
  if (!isVisible()) {
    return;
  }
  const bool hexVisible = m_hexview && m_hexview->isVisible();
  const bool treeVisible = m_treeview && m_treeview->isVisible();
  if (!hexVisible && !treeVisible) {
    return;
  }

  auto* seq = dynamic_cast<VGMSeq*>(m_vgmfile);
  if (!seq) {
    return;
  }

  const auto* coll = SequencePlayer::the().activeCollection();
  if (!coll || !coll->containsVGMFile(m_vgmfile)) {
    if (hexVisible) {
      m_hexview->setPlaybackActive(false);
      m_hexview->clearPlaybackSelections(false);
      m_lastPlaybackItems.clear();
      m_lastPlaybackItemColors.clear();
    }
    if (treeVisible) {
      m_treeview->setPlaybackItems({});
    }
    return;
  }

  const bool shouldHighlight = SequencePlayer::the().playing() || current > 0;
  if (!shouldHighlight) {
    if (hexVisible) {
      m_hexview->setPlaybackActive(false);
      m_hexview->clearPlaybackSelections(false);
      m_lastPlaybackItems.clear();
      m_lastPlaybackItemColors.clear();
    }
    if (treeVisible) {
      m_treeview->setPlaybackItems({});
    }
    return;
  }

  const auto& timeline = seq->timedEventIndex();
  if (!timeline.finalized()) {
    m_playbackCursor.reset();
    m_playbackTimeline = nullptr;
    if (hexVisible) {
      m_hexview->setPlaybackActive(false);
      m_hexview->clearPlaybackSelections(false);
      m_lastPlaybackItems.clear();
      m_lastPlaybackItemColors.clear();
    }
    if (treeVisible) {
      m_treeview->setPlaybackItems({});
    }
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
      if (tickDiff <= 20)
        m_playbackCursor->getActiveInRange(m_lastPlaybackPosition, current, m_playbackTimedEvents);
      else
        m_playbackCursor->getActiveAt(current, m_playbackTimedEvents);
      break;
    case PositionChangeOrigin::SeekBar:
      // This intended to distinguish between a drag and a seek to a further position of a sequence.
      // We want a drag to highlight passed through events.
      if (tickDiff >= -500 && tickDiff <= 500) {
        // Select all events passed through. We will fade the ones skipped past in the next step.
        int lesser = std::min(m_lastPlaybackPosition, current);
        int greater = std::max(m_lastPlaybackPosition, current);
        m_playbackCursor->getActiveInRange(lesser, greater, m_playbackTimedEvents);

        // Select only the items at the current position to make the others fade - we accomplish
        // this by calling this method with origin HexView, which will use a getActiveAt selection.
        QTimer::singleShot(0, m_hexview, [this, current, max] {
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

  ensureTrackIndexMap(seq);
  m_playbackItems.clear();
  m_playbackItemColors.clear();
  m_playbackItems.reserve(m_playbackTimedEvents.size());
  m_playbackItemColors.reserve(m_playbackTimedEvents.size());
  for (const auto* timed : m_playbackTimedEvents) {
    if (!timed || !timed->event) {
      continue;
    }
    auto* event = timed->event;
    m_playbackItems.push_back(event);
    QColor glowColor = colorForItemType(event->type);
    if (const auto* seqEvent = dynamic_cast<const SeqEvent*>(event)) {
      const int trackIndex = trackIndexForEvent(seqEvent);
      if (trackIndex >= 0) {
        glowColor = colorForTrackIndex(trackIndex);
      }
    }
    m_playbackItemColors.push_back(glowColor);
  }

  if (treeVisible) {
    m_treeview->setPlaybackItems(m_playbackItems);
  }

  if (hexVisible) {
    m_hexview->setPlaybackActive(shouldHighlight);
  }

  if (m_playbackItems == m_lastPlaybackItems &&
      m_playbackItemColors == m_lastPlaybackItemColors) {
    if (hexVisible) {
      m_hexview->requestPlaybackFrame();
    }
    return;
  }

  m_lastPlaybackItems = m_playbackItems;
  m_lastPlaybackItemColors = m_playbackItemColors;
  if (hexVisible) {
    m_hexview->setPlaybackSelectionsForItems(m_playbackItems, m_playbackItemColors);
  }
}

void VGMFileView::onPlayerStatusChanged(bool playing) {
  if (playing || !m_hexview)
    return;
  if (SequencePlayer::the().activeCollection() != nullptr)
    return;
  m_playbackTimedEvents.clear();
  m_playbackItems.clear();
  m_playbackItemColors.clear();
  m_lastPlaybackItems.clear();
  m_lastPlaybackItemColors.clear();
  m_lastPlaybackPosition = 0;
  m_playbackCursor.reset();
  m_playbackTimeline = nullptr;
  m_trackIndexSeq = nullptr;
  m_trackIndexByPtr.clear();
  m_trackIndexByMidiPtr.clear();
  m_hexview->setPlaybackActive(false);
  m_hexview->clearPlaybackSelections(false);
  if (m_treeview) {
    m_treeview->setPlaybackItems({});
  }
}
