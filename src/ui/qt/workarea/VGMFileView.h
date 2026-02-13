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

class QButtonGroup;
class QStackedWidget;
class QToolButton;
class QWidget;
class SnappingSplitter;
class VGMFile;
class VGMSeq;
class SeqTrack;
class MidiTrack;
class HexView;
class VGMFileTreeView;
class ActiveNoteView;
class PianoRollView;
class VGMItem;
class SeqEvent;
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

private:
  struct PanelUi {
    QWidget* container = nullptr;
    QToolButton* singlePaneToggle = nullptr;
    QButtonGroup* viewButtons = nullptr;
    QStackedWidget* stack = nullptr;
    HexView* hexView = nullptr;
    VGMFileTreeView* treeView = nullptr;
    ActiveNoteView* activeNoteView = nullptr;
    PianoRollView* pianoRollView = nullptr;
    PanelViewKind currentKind = PanelViewKind::Hex;
  };

  static constexpr int treeViewMinimumWidth = 220;

  void focusInEvent(QFocusEvent* event) override;
  void closeEvent(QCloseEvent* closeEvent) override;

  PanelUi createPanel(PanelSide side, bool isSeqFile);
  void setPanelView(PanelSide side, PanelViewKind viewKind);
  void setSinglePaneMode(bool singlePane);

  void resetSnapRanges() const;
  [[nodiscard]] int hexViewFullWidth() const;
  [[nodiscard]] int hexViewWidthSansAscii() const;
  [[nodiscard]] int hexViewWidthSansAsciiAndAddress() const;
  void updateHexViewFont(qreal sizeIncrement) const;
  void applyHexViewFont(QFont font) const;

  void clearPlaybackVisuals();
  void ensureTrackIndexMap(VGMSeq* seq);
  [[nodiscard]] int trackIndexForEvent(const SeqEvent* event) const;
  [[nodiscard]] int effectiveTrackCountForSeq(VGMSeq* seq) const;
  static int noteKeyForEvent(const SeqEvent* event);
  bool prepareSeqEventForPlayback(SeqEvent* event, uint32_t& tick) const;

  PanelUi& panel(PanelSide side) { return m_panels[static_cast<size_t>(side)]; }
  const PanelUi& panel(PanelSide side) const { return m_panels[static_cast<size_t>(side)]; }

  VGMFile* m_vgmfile{};
  SnappingSplitter* m_splitter = nullptr;
  std::array<PanelUi, 2> m_panels{};
  int m_defaultSplitterHandleWidth = 0;
  QList<int> m_lastSplitSizes;
  QFont m_defaultHexFont;

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
  void onSelectionChange(VGMItem* item) const;
  void onSelectionSetChange(const std::vector<VGMItem*>& items, VGMItem* primaryItem) const;
  void seekToEvent(VGMItem* item) const;
  void previewModifierNoteForEvent(VGMItem* item, bool includeActiveNotesAtTick) const;
  void stopModifierNotePreview() const;
  void onPlaybackPositionChanged(int current, int max, PositionChangeOrigin origin);
  void onPlayerStatusChanged(bool playing);
  void resetHexViewFont();
  void increaseHexViewFont();
  void decreaseHexViewFont();
};
