/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QFont>
#include <QMdiSubWindow>

#include "SeqEventTimeIndex.h"
#include "pianorollview/PianoRollView.h"

class QStackedWidget;
class QWidget;
class QSplitter;
class QPoint;
class QObject;
class QEvent;
class QResizeEvent;
class QShowEvent;
class VGMFile;
class VGMSeq;
class SeqTrack;
class MidiTrack;
class HexView;
class VGMFileTreeView;
class ActiveNoteView;
class VGMItem;
class SeqEvent;
class SplitterSnapProvider;
struct SeqTimedEvent;
enum class PositionChangeOrigin;

enum class PanelSide : uint8_t {
  Left = 0,
  Right = 1,
};

enum class PanelViewKind : uint8_t {
  Hex = 0,
  Tree = 1,
  ActiveNotes = 2,
  PianoRoll = 3,
};

class VGMFileView final : public QMdiSubWindow {
  Q_OBJECT

public:
  explicit VGMFileView(VGMFile* vgmFile);
  void setPanelView(PanelSide side, PanelViewKind viewKind);
  [[nodiscard]] PanelViewKind panelView(PanelSide side) const;
  void setSinglePaneMode(bool singlePane);
  [[nodiscard]] bool singlePaneMode() const;
  [[nodiscard]] bool supportsViewKind(PanelViewKind viewKind) const;
  [[nodiscard]] bool supportsSequenceViews() const;

private:
  struct PanelUi {
    QWidget* container = nullptr;
    QStackedWidget* stack = nullptr;
    HexView* hexView = nullptr;
    VGMFileTreeView* treeView = nullptr;
    ActiveNoteView* activeNoteView = nullptr;
    PianoRollView* pianoRollView = nullptr;
    PanelViewKind currentKind = PanelViewKind::Hex;
  };

  static constexpr int rightPaneMinimumWidth = 220;

  bool eventFilter(QObject* watched, QEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void closeEvent(QCloseEvent* closeEvent) override;

  PanelUi createPanel(PanelSide side);
  bool ensurePanelViewCreated(PanelSide side, PanelViewKind viewKind);
  QWidget* panelWidget(const PanelUi& panelUi, PanelViewKind viewKind) const;
  void applySelectionStateToPanelView(PanelSide side, PanelViewKind viewKind) const;
  void applyPlaybackStateToPanelView(PanelSide side, PanelViewKind viewKind) const;

  [[nodiscard]] int hexViewFullWidth() const;
  void updateHexViewFont(qreal sizeIncrement);
  void applyHexViewFont(QFont font, bool persistSetting = true);
  void refreshPlaybackVisualsIfNeeded();
  void onSplitterMoved(int pos, int index);
  void ensurePreferredLeftPaneWidth();
  [[nodiscard]] int maxLeftPaneWidth() const;
  [[nodiscard]] int snapLeftPaneWidthForDrag(int leftPaneWidth) const;
  [[nodiscard]] int snapLeftPaneWidthForResize(int leftPaneWidth) const;
  [[nodiscard]] SplitterSnapProvider* leftPaneSnapProvider() const;
  bool paneSideAtGlobalPos(const QPoint& globalPos, PanelSide& side) const;
  void setSplitterSizes(int leftPaneWidth, int rightPaneWidth);
  void setLeftPaneWidth(int leftPaneWidth, bool persistWidth);
  void enforceSplitterPolicyForResize();

  void clearPlaybackVisuals();
  void ensureTrackIndexMap(VGMSeq* seq);
  [[nodiscard]] int trackIndexForEvent(const SeqEvent* event) const;
  [[nodiscard]] int effectiveTrackCountForSeq(VGMSeq* seq) const;
  [[nodiscard]] bool ensureAssociatedCollectionActive() const;
  bool prepareSeqEventForPlayback(SeqEvent* event, uint32_t& tick) const;

  PanelUi& panel(PanelSide side) { return m_panels[static_cast<size_t>(side)]; }
  const PanelUi& panel(PanelSide side) const { return m_panels[static_cast<size_t>(side)]; }

  VGMFile* m_vgmfile{};
  QSplitter* m_splitter = nullptr;
  std::array<PanelUi, 2> m_panels{};
  int m_defaultSplitterHandleWidth = 0;
  int m_preferredLeftPaneWidth = -1;
  bool m_updatingSplitter = false;
  QFont m_defaultHexFont;
  QFont m_activeHexFont;
  bool m_isSeqFile = false;
  bool m_singlePaneMode = false;
  bool m_playbackVisualsActive = false;
  bool m_playbackTickActive = false;
  std::vector<const VGMItem*> m_selectedItems;
  const VGMItem* m_primarySelectedItem = nullptr;

  std::vector<const SeqTimedEvent*> m_playbackTimedEvents;
  std::vector<const SeqTimedEvent*> m_activeTimedEvents;
  std::vector<const VGMItem*> m_playbackItems;
  std::vector<const VGMItem*> m_lastPlaybackItems;
  int m_lastPlaybackPosition = 0;
  const SeqEventTimeIndex* m_playbackTimeline = nullptr;
  std::unique_ptr<SeqEventTimeIndex::Cursor> m_playbackCursor;
  VGMSeq* m_trackIndexSeq = nullptr;
  std::unordered_map<const SeqTrack*, int> m_trackIndexByPtr;
  std::unordered_map<const MidiTrack*, int> m_trackIndexByMidiPtr;

public slots:
  void onSelectionChange(VGMItem* item);
  void onSelectionSetChange(const std::vector<VGMItem*>& items, VGMItem* primaryItem);
  void seekToEvent(VGMItem* item) const;
  void previewNotesForEvent(VGMItem* item, bool includeActiveNotesAtTick) const;
  void previewPianoRollNotes(const std::vector<PianoRollView::PreviewSelection>& notes, int tick) const;
  void stopNotePreview() const;
  void previewActiveNote(int trackIndex, int key) const;
  void onPlaybackPositionChanged(int current, int max, PositionChangeOrigin origin);
  void onPlayerStatusChanged(bool playing);
  void resetHexViewFont();
  void increaseHexViewFont();
  void decreaseHexViewFont();
};
