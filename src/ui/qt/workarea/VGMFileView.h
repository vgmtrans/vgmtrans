/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "SeqEventTimeIndex.h"

#include <memory>
#include <unordered_map>
#include <vector>

#include <QColor>
#include <QFont>
#include <QMdiSubWindow>

class MidiTrack;
class SnappingSplitter;
class VGMFile;
class VGMSeq;
class HexView;
class VGMFileTreeView;
class VGMItem;
class SeqEvent;
class SeqTrack;
struct SeqTimedEvent;
enum class PositionChangeOrigin;

class VGMFileView final : public QMdiSubWindow {
  Q_OBJECT

public:
  explicit VGMFileView(VGMFile *vgmFile);

private:
  static constexpr int treeViewMinimumWidth = 220;

  void resetSnapRanges() const;
  void focusInEvent(QFocusEvent* event) override;
  void closeEvent(QCloseEvent *closeEvent) override;
  int hexViewFullWidth() const;
  int hexViewWidthSansAscii() const;
  int hexViewWidthSansAsciiAndAddress() const;
  void updateHexViewFont(qreal sizeIncrement) const;
  void applyHexViewFont(QFont font) const;
  void ensureTrackIndexMap(VGMSeq* seq);
  int trackIndexForEvent(const SeqEvent* event) const;

  VGMFileTreeView* m_treeview{};
  VGMFile* m_vgmfile{};
  HexView* m_hexview{};
  SnappingSplitter* m_splitter;
  QFont m_defaultHexFont;
  std::vector<const SeqTimedEvent*> m_playbackTimedEvents;
  std::vector<const VGMItem*> m_playbackItems;
  std::vector<QColor> m_playbackItemColors;
  std::vector<const VGMItem*> m_lastPlaybackItems;
  std::vector<QColor> m_lastPlaybackItemColors;
  int m_lastPlaybackPosition = 0;
  const SeqEventTimeIndex* m_playbackTimeline = nullptr;
  std::unique_ptr<SeqEventTimeIndex::Cursor> m_playbackCursor;
  VGMSeq* m_trackIndexSeq = nullptr;
  std::unordered_map<const SeqTrack*, int> m_trackIndexByPtr;
  std::unordered_map<const MidiTrack*, int> m_trackIndexByMidiPtr;

public slots:
  void onSelectionChange(VGMItem* item) const;
  void seekToEvent(VGMItem* item) const;
  void onPlaybackPositionChanged(int current, int max, PositionChangeOrigin origin);
  void onPlayerStatusChanged(bool playing);
  void resetHexViewFont();
  void increaseHexViewFont();
  void decreaseHexViewFont();
};
