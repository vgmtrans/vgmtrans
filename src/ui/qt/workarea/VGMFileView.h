/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QFont>
#include <QMdiSubWindow>
#include <vector>

class SnappingSplitter;
class VGMFile;
class HexView;
class VGMFileTreeView;
class VGMItem;
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

  VGMFileTreeView* m_treeview{};
  VGMFile* m_vgmfile{};
  HexView* m_hexview{};
  SnappingSplitter* m_splitter;
  QFont m_defaultHexFont;
  std::vector<const SeqTimedEvent*> m_playbackTimedEvents;
  std::vector<const VGMItem*> m_playbackItems;
  std::vector<const VGMItem*> m_lastPlaybackItems;
  int m_lastPlaybackPosition = 0;

public slots:
  void onSelectionChange(VGMItem* item) const;
  void seekToEvent(VGMItem* item) const;
  void onPlaybackPositionChanged(int current, int max, PositionChangeOrigin origin);
  void onPlayerStatusChanged(bool playing);
  void resetHexViewFont();
  void increaseHexViewFont();
  void decreaseHexViewFont();
};
