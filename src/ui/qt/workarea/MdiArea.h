/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <unordered_map>
#include <QMdiArea>
#include <QMdiSubWindow>

class VGMFile;

class MdiArea : public QMdiArea {
  Q_OBJECT
public:
  /*
   * This singleton is returned as a pointer instead of a reference
   * so that it can be disposed by Qt's destructor machinery
   */
  static auto the() {
    static MdiArea *area = new MdiArea();
    return area;
  }

  MdiArea(const MdiArea &) = delete;
  MdiArea &operator=(const MdiArea &) = delete;
  MdiArea(MdiArea &&) = delete;
  MdiArea &operator=(MdiArea &&) = delete;

  void newView(VGMFile *file);
  void removeView(VGMFile *file);
  void focusView(VGMFile *file, QWidget *caller);

signals:
  void vgmFileSelected(VGMFile *file);

public slots:
  void onSubWindowActivated(QMdiSubWindow *window);

private:
  MdiArea(QWidget *parent = nullptr);
  void ensureMaximizedSubWindow(QMdiSubWindow *window);
  std::unordered_map<VGMFile *, QMdiSubWindow *> fileToWindowMap;
  std::unordered_map<QMdiSubWindow *, VGMFile *> windowToFileMap;
};