/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileTreeView.h"

#include "CapsuleText.h"
#include "ColorHelpers.h"
#include "hexview/HexViewInput.h"
#include "Metrics.h"
#include "services/Settings.h"
#include "value/model/SourceInspection.h"
#include "workarea/SourceInspectorPresentation.h"

#include <QApplication>
#include <QCheckBox>
#include <QFontMetrics>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>

#include <algorithm>
#include <utility>

namespace {

enum ItemDataRole {
  DescriptionRole = Qt::UserRole,
  RangeRole,
  ShowDetailsRole,
};

constexpr int treeTextMargin = 4;

class VGMFileTreeHeaderView final : public QHeaderView {
public:
  VGMFileTreeHeaderView(Qt::Orientation orientation, QWidget* parent, bool showDetails);

private:
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void onShowDetailsChanged(bool showDetails) const;
  void toggleShowDetails() const;

  QCheckBox* detailsCheckBox_{};
};

class VGMTreeItem final : public QTreeWidgetItem {
public:
  static constexpr auto ItemType = QTreeWidgetItem::UserType + 1;

  explicit VGMTreeItem(vgmtrans::core::SourceAnnotationId annotation)
      : QTreeWidgetItem(ItemType), annotation_(annotation) {}

  [[nodiscard]] vgmtrans::core::SourceAnnotationId annotation() const noexcept { return annotation_; }

private:
  vgmtrans::core::SourceAnnotationId annotation_;
};

class VGMTreeDisplayItem final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

VGMFileTreeHeaderView::VGMFileTreeHeaderView(Qt::Orientation orientation, QWidget* parent, bool showDetails)
    : QHeaderView(orientation, parent) {
  setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  setSectionResizeMode(QHeaderView::Fixed);

  detailsCheckBox_ = new QCheckBox("Show Details", this);
  detailsCheckBox_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  detailsCheckBox_->setStyleSheet(
      QString("QCheckBox::indicator { width: %1px; height: %1px; }").arg(Size::HeaderCheckbox));
  detailsCheckBox_->setChecked(showDetails);
  detailsCheckBox_->show();

  connect(detailsCheckBox_, &QCheckBox::clicked, this, &VGMFileTreeHeaderView::toggleShowDetails);
  connect(Settings::the(), &Settings::vgmFileTreeShowDetailsChanged, this,
          &VGMFileTreeHeaderView::onShowDetailsChanged);
}

void VGMFileTreeHeaderView::showEvent(QShowEvent* event) {
  QFont headerFont = font();
  headerFont.setPointSize(headerFont.pointSize() - 1);
#ifdef Q_OS_MAC
  setStyleSheet("QHeaderView::section { border-top: 0px solid white; margin-left: 4px; "
                "padding-bottom: -1px; padding-top: -3px; }");
  resizeSection(0, width());
#endif
  detailsCheckBox_->setFont(headerFont);
  QHeaderView::showEvent(event);
}

void VGMFileTreeHeaderView::resizeEvent(QResizeEvent* event) {
  QHeaderView::resizeEvent(event);
  resizeSection(0, width());
  detailsCheckBox_->move(width() - detailsCheckBox_->width() - 10, (height() - detailsCheckBox_->height()) / 2);
}

void VGMFileTreeHeaderView::onShowDetailsChanged(bool showDetails) const {
  detailsCheckBox_->setChecked(showDetails);
}

void VGMFileTreeHeaderView::toggleShowDetails() const {
  Settings::the()->VGMFileTreeView.setShowDetails(detailsCheckBox_->isChecked());
}

QColor selectedTreeTextColor(const QStyleOptionViewItem& option) {
  const QPalette::ColorGroup colorGroup = !option.state.testFlag(QStyle::State_Enabled) ? QPalette::Disabled
                                          : option.state.testFlag(QStyle::State_Active) ? QPalette::Normal
                                                                                        : QPalette::Inactive;
  const QColor selectionColor = itemSelectionFillColor(option.palette, colorGroup);
  return contrastingTextColor(selectionColor, option.palette.color(colorGroup, QPalette::Window), option.palette,
                              colorGroup);
}

}  // namespace

void VGMTreeDisplayItem::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
  QStyleOptionViewItem paintOption = option;
  initStyleOption(&paintOption, index);
  const bool showDetails = index.data(ShowDetailsRole).toBool();
  QStyle* style = paintOption.widget ? paintOption.widget->style() : QApplication::style();
  const QString label = paintOption.text;
  paintOption.text.clear();
  style->drawControl(QStyle::CE_ItemViewItem, &paintOption, painter, paintOption.widget);
  const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &paintOption);
  const QRect contentRect = textRect.adjusted(treeTextMargin, treeTextMargin,
                                              -treeTextMargin, -treeTextMargin);

  const QColor textColor = paintOption.state.testFlag(QStyle::State_Selected)
                               ? selectedTreeTextColor(paintOption)
                               : paintOption.palette.color(QPalette::Text);
  QFont labelFont = paintOption.font;
  labelFont.setBold(showDetails);
  const QFontMetrics labelMetrics(labelFont);

  int y = contentRect.top();
  painter->save();
  painter->setClipRect(textRect);
  painter->setPen(textColor);
  painter->setFont(labelFont);
  painter->drawText(QRect(contentRect.left(), y, contentRect.width(), labelMetrics.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, label);
  if (!showDetails) {
    painter->restore();
    return;
  }

  const QFontMetrics detailMetrics(paintOption.font);
  const CapsuleText description = index.data(DescriptionRole).value<CapsuleText>();
  const int descriptionHeight = CapsuleTextLayout::heightForWidth(
      description, paintOption.font, contentRect.width(), true);
  y += labelMetrics.height();
  if (descriptionHeight > 0) {
    painter->setFont(paintOption.font);
    CapsuleTextLayout::paint(
        *painter, QRect(contentRect.left(), y + 1, contentRect.width(), descriptionHeight),
        description, paintOption.palette, textColor, true);
    y += descriptionHeight + 2;
  }
  painter->setFont(paintOption.font);
  painter->setPen(textColor);
  painter->drawText(QRect(contentRect.left(), y, contentRect.width(), detailMetrics.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, index.data(RangeRole).toString());
  painter->restore();
}

QSize VGMTreeDisplayItem::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  QStyleOptionViewItem styleOption = option;
  initStyleOption(&styleOption, index);
  const QSize baseSize = QStyledItemDelegate::sizeHint(styleOption, index);
  const bool showDetails = index.data(ShowDetailsRole).toBool();
  QStyle* style = styleOption.widget ? styleOption.widget->style() : QApplication::style();
  int itemWidth = styleOption.rect.width();
  if (itemWidth <= 0 && styleOption.widget != nullptr) {
    itemWidth = styleOption.widget->width();
  }
  styleOption.rect = QRect(0, 0, std::max(1, itemWidth), 1000);
  styleOption.text.clear();
  const int textWidth =
      std::max(1, style->subElementRect(QStyle::SE_ItemViewItemText, &styleOption).width());
  const int contentWidth = std::max(1, textWidth - (treeTextMargin * 2));

  QFont labelFont = styleOption.font;
  labelFont.setBold(showDetails);
  const int labelHeight = QFontMetrics(labelFont).height();
  if (!showDetails) {
    return QSize(baseSize.width(),
                 std::max(baseSize.height(), labelHeight + (treeTextMargin * 2)));
  }

  const QFontMetrics detailMetrics(styleOption.font);
  const int descriptionHeight = CapsuleTextLayout::heightForWidth(
      index.data(DescriptionRole).value<CapsuleText>(), styleOption.font, contentWidth, true);
  const int spacing = descriptionHeight > 0 ? 2 : 0;
  return QSize(baseSize.width(),
               std::max(baseSize.height(),
                        labelHeight + descriptionHeight + spacing + detailMetrics.height() +
                            (treeTextMargin * 2)));
}

VGMFileTreeView::VGMFileTreeView(std::shared_ptr<const vgmtrans::core::SourceInspection> inspection, QWidget* parent)
    : QTreeWidget(parent), inspection_(std::move(inspection)) {
  Q_ASSERT(inspection_);
  setHeaderLabel("File structure");
  showDetails_ = Settings::the()->VGMFileTreeView.showDetails();
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  horizontalScrollBar()->setEnabled(false);
  setHeader(new VGMFileTreeHeaderView(Qt::Horizontal, this, showDetails_));
  setItemDelegate(new VGMTreeDisplayItem(this));
  playbackBrush_ = QBrush(palette().color(QPalette::Accent));

  appendChildren(invisibleRootItem(), inspection_->roots());
  connect(Settings::the(), &Settings::vgmFileTreeShowDetailsChanged, this, &VGMFileTreeView::onShowDetailsChanged);
}

void VGMFileTreeView::appendChildren(QTreeWidgetItem* parent,
                                     std::span<const vgmtrans::core::SourceAnnotationId> children) {
  for (const auto id : children) {
    const auto* annotation = inspection_->annotation(id);
    if (annotation == nullptr) {
      continue;
    }
    auto* item = new VGMTreeItem(id);
    setItemText(item);
    parent->addChild(item);
    items_.emplace(id.value, item);
    const auto grandchildren = inspection_->children(id);
    appendChildren(item, grandchildren);
  }
}

QTreeWidgetItem* VGMFileTreeView::treeItem(vgmtrans::core::SourceAnnotationId annotation) const {
  const auto found = items_.find(annotation.value);
  return found == items_.end() ? nullptr : found->second;
}

vgmtrans::core::SourceAnnotationId VGMFileTreeView::annotationForItem(const QTreeWidgetItem* item) const {
  if (item == nullptr || item->type() != VGMTreeItem::ItemType) {
    return {};
  }
  return static_cast<const VGMTreeItem*>(item)->annotation();
}

void VGMFileTreeView::setSelectedAnnotation(vgmtrans::core::SourceAnnotationId annotation) {
  if (!annotation.valid()) {
    setCurrentItem(nullptr);
    clearSelection();
    return;
  }
  setCurrentItem(treeItem(annotation));
}

void VGMFileTreeView::focusInEvent(QFocusEvent*) {
  // Preserve the current selection when focus moves between the two inspector panes.
}

void VGMFileTreeView::currentChanged(const QModelIndex& current, const QModelIndex& previous) {
  QTreeView::currentChanged(current, previous);
  updateStatusBar();
  if (QApplication::keyboardModifiers().testFlag(HexViewInput::kModifier)) {
    seekToTreeItem(currentItem());
  }
}

void VGMFileTreeView::mousePressEvent(QMouseEvent* event) {
  QTreeWidgetItem* item = itemAt(event->pos());
  if (event->modifiers().testFlag(HexViewInput::kModifier)) {
    seekToTreeItem(item, true);
    return;
  }
  if (item && item->isSelected()) {
    clearSelection();
    setCurrentItem(nullptr);
  } else {
    QTreeWidget::mousePressEvent(event);
  }
}

void VGMFileTreeView::mouseDoubleClickEvent(QMouseEvent* event) {
  QTreeWidgetItem* item = itemAt(event->pos());
  if (item && item->childCount() > 0) {
    QTreeWidget::mouseDoubleClickEvent(event);
  } else {
    mousePressEvent(event);
  }
}

void VGMFileTreeView::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Left) {
    QTreeWidgetItem* current = currentItem();
    if (current && current->parent() && !isExpanded(indexFromItem(current))) {
      setCurrentItem(current->parent());
      return;
    }
  }
  QTreeWidget::keyPressEvent(event);
}

void VGMFileTreeView::mouseMoveEvent(QMouseEvent* event) {
  if ((event->buttons() & Qt::LeftButton) && event->modifiers().testFlag(HexViewInput::kModifier)) {
    seekToTreeItem(itemAt(event->pos()));
    return;
  }
  QTreeWidget::mouseMoveEvent(event);
}

void VGMFileTreeView::updateStatusBar() {
  emit statusAnnotationChanged(annotationForItem(currentItem()));
}

void VGMFileTreeView::setPlaybackAnnotations(const std::vector<vgmtrans::core::SourceAnnotationId>& annotations) {
  std::unordered_set<QTreeWidgetItem*> next;
  next.reserve(annotations.size());
  for (const auto id : annotations) {
    if (auto* item = treeItem(id)) {
      next.insert(item);
    }
  }
  for (auto* item : playbackTreeItems_) {
    if (item && !next.contains(item)) {
      item->setBackground(0, QBrush());
    }
  }
  for (auto* item : next) {
    if (item && !playbackTreeItems_.contains(item)) {
      item->setBackground(0, playbackBrush_);
    }
  }
  playbackTreeItems_.swap(next);
}

void VGMFileTreeView::seekToTreeItem(QTreeWidgetItem* item, bool allowRepeat) {
  if (item == nullptr || (!allowRepeat && item == lastSeekItem_)) {
    return;
  }
  const auto annotation = annotationForItem(item);
  if (!annotation.valid()) {
    return;
  }
  emit seekToAnnotationRequested(annotation);
  lastSeekItem_ = item;
}

void VGMFileTreeView::setItemText(QTreeWidgetItem* item) const {
  const auto* annotation = inspection_->annotation(annotationForItem(item));
  if (annotation == nullptr) {
    return;
  }
  const CapsuleText description = SourceInspectorPresentation::description(*annotation);
  item->setText(0, QString::fromStdString(annotation->label));
  item->setData(0, DescriptionRole, QVariant::fromValue(description));
  item->setData(0, RangeRole,
                QStringLiteral("Offset: 0x%1 | Length: 0x%2")
                    .arg(annotation->range.offset, 0, 16)
                    .arg(annotation->range.size, 0, 16));
  item->setData(0, ShowDetailsRole, showDetails_);
  item->setIcon(0, SourceInspectorPresentation::icon(*annotation));
  item->setToolTip(0, description.plainText());
}

void VGMFileTreeView::onShowDetailsChanged(bool show) {
  showDetails_ = show;
  {
    const QSignalBlocker blocker(model());
    updateItemTextRecursively(invisibleRootItem());
  }
  doItemsLayout();
  scrollToItem(currentItem());
}

void VGMFileTreeView::updateItemTextRecursively(QTreeWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  if (item->type() == VGMTreeItem::ItemType) {
    item->setData(0, ShowDetailsRole, showDetails_);
  }
  for (int index = 0; index < item->childCount(); ++index) {
    updateItemTextRecursively(item->child(index));
  }
}
