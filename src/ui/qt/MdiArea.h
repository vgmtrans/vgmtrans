/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <unordered_map>

#include <QMdiArea>
#include <QMdiSubWindow>

#include "VGMFileView.h"
#include "VGMFileView.h"
#include "VGMFile.h"

class MdiArea : public QMdiArea {
  Q_OBJECT

public:
  MdiArea(const MdiArea&) = delete;
  MdiArea& operator=(const MdiArea&) = delete;
  MdiArea(MdiArea&&) = delete;
  MdiArea& operator=(MdiArea&&) = delete;

  static MdiArea& Instance();

  void NewView(VGMFile* file);
  void RemoveView(VGMFile* file);

private:
  std::unordered_map<VGMFile*, QMdiSubWindow*> registered_views_;
  explicit MdiArea(QWidget* parent = nullptr);
};
