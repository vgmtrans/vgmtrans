/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>

class QToolButton;
class QEvent;
class QResizeEvent;
class SeekBar;
enum class PositionChangeOrigin;

class PlaybackControls final : public QWidget {
  Q_OBJECT
public:
  explicit PlaybackControls(QWidget *parent = nullptr);

  void showPlayInfo();

signals:
  void playToggle();
  void stopPressed();
  void seekingTo(int position, PositionChangeOrigin origin);

protected:
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void playerStatusChanged(bool playing);
  void playbackRangeUpdate(int cur, int max, PositionChangeOrigin origin);

private:
  void setupControls();
  void updateSeekBarVisibility();

  QToolButton *m_play{};
  QToolButton *m_stop{};
  SeekBar *m_slider{};
  bool m_hasSelectedCollection = false;
  bool m_skipNextPlaybackSliderUpdate = false;
};
