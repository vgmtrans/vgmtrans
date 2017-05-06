#ifndef VGMTRANS_TOOLBAR_H
#define VGMTRANS_TOOLBAR_H

#include <QAction>
#include <QToolBar>

class ToolBar final : public QToolBar
{
 Q_OBJECT

 public:
  explicit ToolBar(QWidget *parent = nullptr);

// public slots:
//  void EmulationStarted();
//  void EmulationPaused();
//  void EmulationStopped();

 signals:
  void OpenPressed();
  void PlayPressed();
  void PausePressed();
  void StopPressed();
//  void FullScreenPressed();
//  void ScreenShotPressed();

  void PathsPressed();
  void SettingsPressed();

 private:
  void MakeActions();
  void UpdateIcons();

  QAction* openAction;
  QAction* playAction;
  QAction* pauseAction;
  QAction* stopAction;
};

#endif //VGMTRANS_TOOLBAR_H
