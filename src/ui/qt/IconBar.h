/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QToolBar>

class QPushButton;
class QLabel;
class MarqueeLabel;
class SeekBar;
enum class PositionChangeOrigin;

class IconBar final : public QWidget {
  Q_OBJECT
public:
  explicit IconBar(QWidget *parent = nullptr);

  void showPlayInfo();

signals:
  void playToggle();
  void stopPressed();
  void seekingTo(int position, PositionChangeOrigin origin);
  void createPressed();

private slots:
  void playerStatusChanged(bool playing);
  void playbackRangeUpdate(int cur, int max, PositionChangeOrigin origin);

private:
  void setupControls();

  QPushButton *m_create{};
  QPushButton *m_play{};
  QPushButton *m_stop{};
  SeekBar *m_slider{};
  MarqueeLabel *m_title;
  bool m_skipNextPlaybackSliderUpdate = false;
  inline static QIcon s_playicon;
  inline static QIcon s_pauseicon;
};
