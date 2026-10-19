/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "PlaybackPosition.h"

#include <QWidget>

class QToolButton;
class QEvent;
class QResizeEvent;
class SeekBar;

class PlaybackControls final : public QWidget {
  Q_OBJECT
public:
  explicit PlaybackControls(QWidget *parent = nullptr);

  void showPlayInfo();
  [[nodiscard]] bool hasPlayableTarget() const noexcept {
    return m_hasSelectedCollection || m_hasActiveCollection;
  }
  void setCollectionSelected(bool selected);
  void setPlaybackState(bool playing, bool hasActiveCollection);
  void setPlaybackPosition(int current, int maximum, PositionChangeOrigin origin);

signals:
  void playToggle();
  void stopPressed();
  void seekingTo(int position, PositionChangeOrigin origin);

protected:
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private slots:
  void playbackRangeUpdate(int current, int maximum, PositionChangeOrigin origin);

private:
  void setupControls();
  void playerStatusChanged(bool playing);
  void updateSeekBarVisibility();

  QToolButton *m_play{};
  QToolButton *m_stop{};
  SeekBar *m_slider{};
  bool m_hasSelectedCollection = false;
  bool m_hasActiveCollection = false;
  bool m_playing = false;
  bool m_skipNextPlaybackSliderUpdate = false;
};
