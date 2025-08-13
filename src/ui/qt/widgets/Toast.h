/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QWidget>
#include <QTimer>
#include "Root.h"

class QLabel;
class QPushButton;
class QPropertyAnimation;
class QGraphicsOpacityEffect;
class QFrame;

struct ToastTheme {
  QColor bg, text, border;
  const char* icon;    // qrc path
};

class Toast : public QWidget {
  Q_OBJECT
public:
  explicit Toast(QWidget *parent = nullptr);
  void showMessage(const QString& message, ToastType type, int duration_ms = 3000);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onFadeOutFinished();
  void onCloseClicked();

private:
  static ToastTheme themeFor(ToastType type);

  QFrame* m_bubble;
  QLabel* m_icon;
  QLabel* m_text;
  QPushButton* m_close;
  QTimer m_timer;
  QGraphicsOpacityEffect* m_opacity_effect;
  QPropertyAnimation* m_fade_in;
  QPropertyAnimation* m_fade_out;
  int m_duration_ms{};
};
