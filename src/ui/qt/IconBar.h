/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QToolBar>

class QSlider;
class QPushButton;
class QLabel;
class MarqueeLabel;

class IconBar final : public QWidget {
  Q_OBJECT
public:
  explicit IconBar(QWidget *parent = nullptr);

  void showPlayInfo();

signals:
  void playToggle();
  void stopPressed();
  void seekingTo(int position);
  void createPressed();

private slots:
  void playerStatusChanged(bool playing) const;
  void playbackRangeUpdate(int cur, int max) const;

private:
  void setupControls();

  QPushButton *m_create{};
  QPushButton *m_play{};
  QPushButton *m_stop{};
  QSlider *m_slider{};
  MarqueeLabel *m_title;
  inline static QIcon s_playicon;
  inline static QIcon s_pauseicon;
};
