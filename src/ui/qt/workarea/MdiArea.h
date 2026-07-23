/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "PlaybackPosition.h"
#include "util/CapsuleText.h"
#include "value/base/CoreTypes.h"
#include "value/sequence/PerformanceModel.h"

#include <span>
#include <unordered_map>
#include <vector>

#include <QMdiArea>
#include <QMdiSubWindow>
#include <QIcon>
#include <QPointer>

class QEvent;
class QPaintEvent;
class VGMFileView;

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
  void setPlaybackSequence(vgmtrans::core::AssetId sequence,
                           std::span<const vgmtrans::core::SourcePlaybackSpan> timeline);
  void clearPlayback();

public slots:
  void increaseActiveHexFont();
  void decreaseActiveHexFont();
  void resetActiveHexFont();
  void setSeekModifierActive(bool active);
  void setPlaybackPosition(int current, int maximum, PositionChangeOrigin origin);

signals:
  void assetSelected(vgmtrans::core::AssetId asset, QWidget* caller);
  void hexViewAvailableChanged(bool available);
  void inspectorStatusChanged(const QString& name, const CapsuleText& description,
                              const QIcon& icon, int offset, int size);
  void playbackSeekRequested(int position, PositionChangeOrigin origin);

protected:
  void changeEvent(QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  struct InspectorWindow {
    QPointer<QMdiSubWindow> window;
    QPointer<QWidget> content;
  };

  MdiArea(QWidget *parent = nullptr);
  void updateBackgroundColor();
  void onSubWindowActivated(QMdiSubWindow *window);
  [[nodiscard]] VGMFileView* activeFileView() const;
  static void ensureMaximizedSubWindow(QMdiSubWindow *window);
  vgmtrans::ui::WorkspaceController* m_workspace{};
  bool m_seekModifierActive = false;
  int m_playbackPosition = 0;
  int m_playbackMaximum = 1;
  vgmtrans::core::AssetId m_playbackSequence;
  std::vector<vgmtrans::core::SourcePlaybackSpan> m_playbackSpans;
  std::unordered_map<u32, InspectorWindow> assetToWindowMap;
  std::unordered_map<QMdiSubWindow *, u32> windowToAssetMap;
};
