/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QBrush>
#include <QTreeWidget>

namespace vgmtrans::core {
class SourceInspection;
}

class VGMFileTreeView final : public QTreeWidget {
  Q_OBJECT
public:
  explicit VGMFileTreeView(std::shared_ptr<const vgmtrans::core::SourceInspection> inspection,
                           QWidget* parent = nullptr);

  [[nodiscard]] vgmtrans::core::SourceAnnotationId annotationForItem(const QTreeWidgetItem* item) const;
  void setSelectedAnnotation(vgmtrans::core::SourceAnnotationId annotation);
  void updateStatusBar();
  void setPlaybackAnnotations(const std::vector<vgmtrans::core::SourceAnnotationId>& annotations);

signals:
  void statusAnnotationChanged(vgmtrans::core::SourceAnnotationId annotation);
  void seekToAnnotationRequested(vgmtrans::core::SourceAnnotationId annotation);

protected:
  void focusInEvent(QFocusEvent* event) override;
  void currentChanged(const QModelIndex& current, const QModelIndex& previous) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

private:
  [[nodiscard]] QTreeWidgetItem* treeItem(vgmtrans::core::SourceAnnotationId annotation) const;
  void appendChildren(QTreeWidgetItem* parent, std::span<const vgmtrans::core::SourceAnnotationId> children);
  void setItemText(QTreeWidgetItem* item) const;
  void onShowDetailsChanged(bool show);
  void updateItemTextRecursively(QTreeWidgetItem* item);
  void seekToTreeItem(QTreeWidgetItem* item, bool allowRepeat = false);

  std::shared_ptr<const vgmtrans::core::SourceInspection> inspection_;
  bool showDetails_ = false;
  std::unordered_map<u32, QTreeWidgetItem*> items_;
  QTreeWidgetItem* lastSeekItem_{};
  std::unordered_set<QTreeWidgetItem*> playbackTreeItems_;
  QBrush playbackBrush_;
};
