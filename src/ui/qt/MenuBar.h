#ifndef VGMTRANS_MENUBAR_H
#define VGMTRANS_MENUBAR_H

#include "MenuBar.h"

#include <QMenu>
#include <QMenuBar>

class MenuBar final : public QMenuBar
{
 Q_OBJECT

 public:
  explicit MenuBar(QWidget* parent = nullptr);

 signals:
  // File
  void Open();
  void Exit();

 private:
  void AddFileMenu();

  // File
  QAction* openAction;
  QAction* exitAction;

};

#endif //VGMTRANS_MENUBAR_H
