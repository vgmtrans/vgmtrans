/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>
#include "Metrics.h"

class QToolButton;

class TitleBar : public QWidget {
  Q_OBJECT

public:
  enum Button {
    NoButtons = 0x0,
    HideButton = 0x1,
    NewButton = 0x2,
  };
  Q_DECLARE_FLAGS(Buttons, Button)

  explicit TitleBar(const QString& title, Buttons buttons = NoButtons, QWidget *parent = nullptr);
  ~TitleBar() override = default;

  void addLeadingWidget(QWidget *widget);

  QSize sizeHint() const override {
    return QSize(200, Size::VTab);
  }
  QSize minimumSizeHint() const override {
    return QSize(0, Size::VTab);
  }

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

signals:
  void hideRequested();
  void addRequested();
  void appearanceChanged();

private:
  void updateButtonStyles();
  void updateButtonsVisible();

  QWidget *m_leadingContainer{};
  class QHBoxLayout *m_leadingLayout{};
  QWidget *m_buttonContainer{};
  class QGraphicsOpacityEffect *m_buttonOpacity{};
  class QPropertyAnimation *m_buttonFade{};
  class QToolButton *m_newButton{};
  class QToolButton *m_hideButton{};
  bool m_buttonsVisible{};
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TitleBar::Buttons)
