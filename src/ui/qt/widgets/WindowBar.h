/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <QList>
#include <QPointer>
#include <QString>
#include <QToolButton>
#include <QWidget>

class QAbstractButton;
class QAction;

class WindowBar final : public QWidget {
  Q_OBJECT

public:
  struct ToggleButtonSpec {
    QAction *action{};
    QString iconPath;
  };

  explicit WindowBar(QWidget *parent = nullptr);

  QWidget *centerWidget() const {
    return m_centerWidget == m_centerPlaceholder ? nullptr : m_centerWidget;
  }
  void setCenterWidget(QWidget *widget);
  QWidget *menuBarWidget() const {
    return m_menuBarWidget == m_menuBarPlaceholder ? nullptr : m_menuBarWidget;
  }
  void setMenuBarWidget(QWidget *widget);
  void setDockToggleButtons(const QList<ToggleButtonSpec> &buttons);
  QWidget *dockControls() const { return m_dockControls; }
  QAbstractButton *windowIconButton() const { return m_windowIconButton; }
  QWidget *systemButtonArea() const { return m_systemButtonArea; }
  QAbstractButton *minimizeButton() const { return m_minimizeButton; }
  QAbstractButton *maximizeButton() const { return m_maximizeButton; }
  QAbstractButton *closeButton() const { return m_closeButton; }

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  void applyWindowButtonStyle(QToolButton *button, bool closeButton = false, bool iconButton = false) const;
  void attachToTopLevelWindow();
  QToolButton *createWindowButton(const QString& toolTip);
  void refreshDockToggleButtons();
  void syncWindowButtons();
  void updateResponsiveLayout();

  struct DockToggleButton {
    QToolButton *button{};
    QString iconPath;
  };

  class QHBoxLayout *m_layout{};
  QWidget *m_menuBarPlaceholder{};
  QWidget *m_menuBarWidget{};
  QWidget *m_centerPlaceholder{};
  QWidget *m_centerWidget{};
  QWidget *m_leftCenterSpacer{};
  QWidget *m_dockControlsSpacer{};
  QWidget *m_dockControls{};
  QToolButton *m_windowIconButton{};
  QWidget *m_rightControls{};
  QWidget *m_systemButtonArea{};
  QToolButton *m_minimizeButton{};
  QToolButton *m_maximizeButton{};
  QToolButton *m_closeButton{};
  QPointer<QWidget> m_trackedWindow;
  QList<DockToggleButton> m_dockToggleButtons;
};
