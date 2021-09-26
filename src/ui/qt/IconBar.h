/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QToolBar>
#include <QIcon>

class QSlider;
class QAction;
class QLabel;

class IconBar final : public QToolBar {
    Q_OBJECT
public:
  explicit IconBar(QWidget *parent = nullptr);

signals:
  void playToggle();
  void stopPressed();
  void seekingTo(int position);

private slots:
  void playerStatusChanged(bool playing);
  void playbackRangeUpdate(int cur, int max);

private:
  void setupControls();

  QAction *m_open{};
  QAction *m_play{};
  QAction *m_stop{};
  QSlider *m_slider{};
  QLabel *m_title;
  inline static QIcon s_playicon;
  inline static QIcon s_pauseicon;
};
