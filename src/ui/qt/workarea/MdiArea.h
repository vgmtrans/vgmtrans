/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <unordered_map>

#include <QMdiArea>
#include <QMdiSubWindow>

class QEvent;
class QPaintEvent;

namespace vgmtrans::ui {
class WorkspaceController;
}

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

  void setWorkspace(vgmtrans::ui::WorkspaceController* workspace);
  void newView(vgmtrans::core::AssetId asset);
  void workspaceChanged();
  void selectAsset(vgmtrans::core::AssetId asset, QWidget* caller);

signals:
  void assetSelected(vgmtrans::core::AssetId asset, QWidget* caller);

protected:
  void changeEvent(QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  MdiArea(QWidget *parent = nullptr);
  void updateBackgroundColor();
  void onSubWindowActivated(QMdiSubWindow *window);
  static void ensureMaximizedSubWindow(QMdiSubWindow *window);
  vgmtrans::ui::WorkspaceController* m_workspace{};
  std::unordered_map<u32, QMdiSubWindow *> assetToWindowMap;
  std::unordered_map<QMdiSubWindow *, u32> windowToAssetMap;
};
