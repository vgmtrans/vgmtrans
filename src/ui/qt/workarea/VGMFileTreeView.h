/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QBrush>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItem>

class QCheckBox;
namespace vgmtrans::ui {
class SourceInspectorModel;
}

class VMGFileTreeHeaderView final : public QHeaderView {
  Q_OBJECT

public:
  VMGFileTreeHeaderView(Qt::Orientation orientation, QWidget* parent = nullptr, bool showDetails = false);

private:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void onShowDetailsChanged(bool showDetails) const;
  void toggleShowDetails() const;

  QCheckBox* detailsCheckBox{};
};

class VGMTreeItem final : public QTreeWidgetItem {
public:
  static constexpr auto ItemType = QTreeWidgetItem::UserType + 1;

  VGMTreeItem(vgmtrans::core::SourceAnnotationId annotation, u64 offset)
      : QTreeWidgetItem(ItemType), annotation_(annotation), offset_(offset) {}

  [[nodiscard]] vgmtrans::core::SourceAnnotationId annotation() const noexcept { return annotation_; }
  [[nodiscard]] u64 itemOffset() const noexcept { return offset_; }

private:
  vgmtrans::core::SourceAnnotationId annotation_;
  u64 offset_ = 0;
};

class VGMTreeDisplayItem final : public QStyledItemDelegate {
  Q_OBJECT
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

class VGMFileTreeView final : public QTreeWidget {
  Q_OBJECT
public:
  explicit VGMFileTreeView(const vgmtrans::ui::SourceInspectorModel& model, QWidget* parent = nullptr);

  [[nodiscard]] QTreeWidgetItem* treeItem(vgmtrans::core::SourceAnnotationId annotation) const;
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
  void appendChildren(QTreeWidgetItem* parent, std::span<const vgmtrans::core::SourceAnnotationId> children);
  void setItemText(VGMTreeItem* item) const;
  void onShowDetailsChanged(bool show);
  void updateItemTextRecursively(QTreeWidgetItem* item);
  void seekToTreeItem(QTreeWidgetItem* item, bool allowRepeat = false);

  const vgmtrans::ui::SourceInspectorModel& model_;
  bool showDetails_ = false;
  std::unordered_map<u32, QTreeWidgetItem*> items_;
  QTreeWidgetItem* lastSeekItem_{};
  std::unordered_set<QTreeWidgetItem*> playbackTreeItems_;
  QBrush playbackBrush_;
};
