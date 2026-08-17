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

  explicit VGMTreeItem(vgmtrans::core::SourceInspectionItem item,
                       vgmtrans::core::SourceRange range = {})
      : QTreeWidgetItem(ItemType), item_(item), range_(range) {}

  [[nodiscard]] vgmtrans::core::SourceInspectionItem sourceItem() const noexcept { return item_; }
  [[nodiscard]] vgmtrans::core::SourceRange range() const noexcept { return range_; }

private:
  vgmtrans::core::SourceInspectionItem item_;
  vgmtrans::core::SourceRange range_;
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

int itemWidthForSizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) {
  const auto* tree = qobject_cast<const QTreeView*>(option.widget);
  if (tree == nullptr) {
    if (option.rect.width() > 0) {
      return option.rect.width();
    }
    return option.widget == nullptr ? 1 : option.widget->width();
  }

  int depth = tree->rootIsDecorated() ? 1 : 0;
  for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) {
    ++depth;
  }
  const int indentedWidth =
      std::max(1, tree->viewport()->width() - (depth * tree->indentation()));
  return option.rect.width() > 0 ? std::min(option.rect.width(), indentedWidth) : indentedWidth;
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
  const int itemWidth = itemWidthForSizeHint(styleOption, index);
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

VGMFileTreeView::VGMFileTreeView(std::shared_ptr<const vgmtrans::core::SourceInspection> inspection,
                                 const vgmtrans::core::Asset& asset, QWidget* parent)
    : QTreeWidget(parent), inspection_(std::move(inspection)) {
  Q_ASSERT(inspection_);
  setHeaderLabel("File structure");
  showDetails_ = Settings::the()->VGMFileTreeView.showDetails();
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  horizontalScrollBar()->setEnabled(false);
  setHeader(new VGMFileTreeHeaderView(Qt::Horizontal, this, showDetails_));
  setItemDelegate(new VGMTreeDisplayItem(this));
  playbackBrush_ = QBrush(palette().color(QPalette::Accent));

  for (const auto root : inspection_->roots()) {
    appendItem(invisibleRootItem(), vgmtrans::core::SourceInspectionItem::forAnnotation(root));
  }

  const auto addGroup = [&](const auto& objects, vgmtrans::core::ObjectKind kind, const QString& groupName,
                            const QString& itemName, const QString& groupIcon, const QString& itemIcon,
                            auto rangeOf) {
    if (objects.empty()) {
      return;
    }

    vgmtrans::core::SourceRange span;
    for (const auto& object : objects) {
      const auto range = rangeOf(object);
      if (!range.valid() || (span.valid() && range.source != span.source)) {
        continue;
      }
      if (!span.valid()) {
        span = range;
      } else {
        const u64 begin = std::min(span.offset, range.offset);
        const u64 end = std::max(span.endOffset(), range.endOffset());
        span.offset = begin;
        span.size = end - begin;
      }
    }

    const auto sourceItemForObject = [&](u32 index) {
      const auto range = rangeOf(objects[index]);
      const auto& annotations = inspection_->annotations();
      const auto source = std::ranges::find_if(annotations, [&](const auto& annotation) {
        return annotation.owner && annotation.owner->kind == kind &&
               annotation.owner->asset == inspection_->asset() && annotation.owner->index0 == index &&
               annotation.range == range;
      });
      return source == annotations.end() ? vgmtrans::core::SourceInspectionItem{}
                                         : vgmtrans::core::SourceInspectionItem::forAnnotation(source->id);
    };

    auto* group = new VGMTreeItem({}, span);
    setItemText(group);
    group->setText(0, groupName);
    group->setIcon(0, QIcon(groupIcon));
    group->setExpanded(true);

    for (u32 index = 0; index < objects.size(); ++index) {
      const auto& object = objects[index];
      const auto sourceItem = sourceItemForObject(index);

      auto* item = sourceItem.valid() ? treeItem(sourceItem) : nullptr;
      if (item == nullptr) {
        item = new QTreeWidgetItem(group);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
      } else {
        if (auto* parent = item->parent()) {
          parent->removeChild(item);
        } else {
          takeTopLevelItem(indexOfTopLevelItem(item));
        }
        group->addChild(item);
      }
      item->setText(0, object.name.empty() ? QStringLiteral("%1 %2").arg(itemName).arg(index)
                                           : QString::fromStdString(object.name));
      item->setIcon(0, QIcon(itemIcon));
    }

    const auto offset = rangeForItem(group).offset;
    int position = 0;
    while (position < topLevelItemCount() && rangeForItem(topLevelItem(position)).offset <= offset) {
      ++position;
    }
    insertTopLevelItem(position, group);
  };

  if (const auto* bank = std::get_if<vgmtrans::core::SoundBankAsset>(&asset)) {
    addGroup(bank->instruments, vgmtrans::core::ObjectKind::Instrument, QStringLiteral("Instruments"),
             QStringLiteral("Instrument"), QStringLiteral(":/icons/instrument-set.svg"),
             QStringLiteral(":/icons/instr.svg"), [](const auto& instrument) { return instrument.range; });
    addGroup(bank->localSamples.samples, vgmtrans::core::ObjectKind::Sample, QStringLiteral("Samples"),
             QStringLiteral("Sample"), QStringLiteral(":/icons/sample-collection.svg"),
             QStringLiteral(":/icons/sample.svg"), [](const auto& sample) { return sample.encodedData; });
  } else if (const auto* pool = std::get_if<vgmtrans::core::SamplePoolAsset>(&asset)) {
    addGroup(pool->pool.samples, vgmtrans::core::ObjectKind::Sample, QStringLiteral("Samples"),
             QStringLiteral("Sample"), QStringLiteral(":/icons/sample-collection.svg"),
             QStringLiteral(":/icons/sample.svg"), [](const auto& sample) { return sample.encodedData; });
  }

  connect(Settings::the(), &Settings::vgmFileTreeShowDetailsChanged, this, &VGMFileTreeView::onShowDetailsChanged);
}

void VGMFileTreeView::appendChildren(QTreeWidgetItem* parent,
                                     vgmtrans::core::SourceAnnotationId annotationId) {
  const auto* annotation = inspection_->annotation(annotationId);
  if (annotation == nullptr) {
    return;
  }

  std::vector<vgmtrans::core::SourceInspectionItem> children;
  const auto annotations = inspection_->children(annotationId);
  children.reserve(annotations.size() + (annotation->fieldsAsChildren ? annotation->fields.size() : 0));
  for (const auto child : annotations) {
    children.push_back(vgmtrans::core::SourceInspectionItem::forAnnotation(child));
  }
  if (annotation->fieldsAsChildren) {
    for (u32 fieldIndex = 0; fieldIndex < annotation->fields.size(); ++fieldIndex) {
      const auto item = vgmtrans::core::SourceInspectionItem::forField(annotationId, fieldIndex);
      if (inspection_->range(item).valid()) {
        children.push_back(item);
      }
    }
  }
  std::ranges::sort(children, [this](const auto lhs, const auto rhs) {
    const auto left = inspection_->range(lhs);
    const auto right = inspection_->range(rhs);
    if (left.offset != right.offset) {
      return left.offset < right.offset;
    }
    if (left.size != right.size) {
      return left.size > right.size;
    }
    if (lhs.isField() != rhs.isField()) {
      return !lhs.isField();
    }
    return itemKey(lhs) < itemKey(rhs);
  });
  for (const auto child : children) {
    appendItem(parent, child);
  }
}

void VGMFileTreeView::appendItem(QTreeWidgetItem* parent, vgmtrans::core::SourceInspectionItem sourceItem) {
  if (inspection_->annotation(sourceItem) == nullptr ||
      (sourceItem.isField() && inspection_->field(sourceItem) == nullptr)) {
    return;
  }
  auto* item = new VGMTreeItem(sourceItem);
  setItemText(item);
  parent->addChild(item);
  items_.emplace(itemKey(sourceItem), item);
  if (!sourceItem.isField()) {
    appendChildren(item, sourceItem.annotation);
  }
}

u64 VGMFileTreeView::itemKey(vgmtrans::core::SourceInspectionItem item) {
  const u64 field = item.field ? static_cast<u64>(*item.field) + 1 : 0;
  return (static_cast<u64>(item.annotation.value) << 32) | field;
}

QTreeWidgetItem* VGMFileTreeView::treeItem(vgmtrans::core::SourceInspectionItem item) const {
  const auto found = items_.find(itemKey(item));
  return found == items_.end() ? nullptr : found->second;
}

vgmtrans::core::SourceInspectionItem VGMFileTreeView::sourceItemForItem(const QTreeWidgetItem* item) const {
  if (item == nullptr || item->type() != VGMTreeItem::ItemType) {
    return {};
  }
  return static_cast<const VGMTreeItem*>(item)->sourceItem();
}

vgmtrans::core::SourceRange VGMFileTreeView::rangeForItem(const QTreeWidgetItem* item) const {
  if (item == nullptr || item->type() != VGMTreeItem::ItemType) {
    return {};
  }
  const auto* treeItem = static_cast<const VGMTreeItem*>(item);
  return treeItem->range().valid() ? treeItem->range() : inspection_->range(treeItem->sourceItem());
}

vgmtrans::core::SourceAnnotationId VGMFileTreeView::annotationForItem(const QTreeWidgetItem* item) const {
  return sourceItemForItem(item).annotation;
}

void VGMFileTreeView::setSelectedItem(vgmtrans::core::SourceInspectionItem item) {
  if (!item.valid()) {
    setCurrentItem(nullptr);
    clearSelection();
    return;
  }
  setCurrentItem(treeItem(item));
}

void VGMFileTreeView::setSelectedAnnotation(vgmtrans::core::SourceAnnotationId annotation) {
  setSelectedItem(vgmtrans::core::SourceInspectionItem::forAnnotation(annotation));
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
  emit statusItemChanged(sourceItemForItem(currentItem()));
}

void VGMFileTreeView::setPlaybackAnnotations(const std::vector<vgmtrans::core::SourceAnnotationId>& annotations) {
  std::unordered_set<QTreeWidgetItem*> next;
  next.reserve(annotations.size());
  for (const auto id : annotations) {
    if (auto* item = treeItem(vgmtrans::core::SourceInspectionItem::forAnnotation(id))) {
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
  const auto sourceItem = sourceItemForItem(item);
  const auto* annotation = inspection_->annotation(sourceItem);
  const auto range = rangeForItem(item);
  item->setData(0, RangeRole,
                QStringLiteral("Offset: 0x%1 | Length: 0x%2")
                    .arg(range.offset, 0, 16)
                    .arg(range.size, 0, 16));
  item->setData(0, ShowDetailsRole, showDetails_);
  if (annotation == nullptr) {
    return;
  }
  CapsuleText description;
  if (const auto* field = inspection_->field(sourceItem)) {
    item->setText(0, SourceInspectorPresentation::fieldLabel(*field));
    description.prefix = SourceInspectorPresentation::fieldValue(*field);
    item->setIcon(0, SourceInspectorPresentation::fieldIcon());
    item->setToolTip(0, QStringLiteral("%1: %2").arg(item->text(0), description.prefix));
  } else {
    description = SourceInspectorPresentation::description(*annotation);
    item->setText(0, QString::fromStdString(annotation->label));
    item->setIcon(0, SourceInspectorPresentation::icon(*annotation));
    item->setToolTip(0, description.plainText());
  }
  item->setData(0, DescriptionRole, QVariant::fromValue(description));
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
