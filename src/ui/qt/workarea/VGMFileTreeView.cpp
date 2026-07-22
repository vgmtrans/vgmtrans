/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileTreeView.h"

#include "ColorHelpers.h"
#include "hexview/HexViewInput.h"
#include "Metrics.h"
#include "models/SourceInspectorModel.h"
#include "services/Settings.h"
#include "workarea/SourceInspectorPresentation.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QPainter>
#include <QScrollBar>
#include <QTextDocument>

VMGFileTreeHeaderView::VMGFileTreeHeaderView(Qt::Orientation orientation, QWidget* parent, bool showDetails)
    : QHeaderView(orientation, parent) {
  setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  setSectionResizeMode(QHeaderView::Fixed);

  detailsCheckBox = new QCheckBox("Show Details", this);
  detailsCheckBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  detailsCheckBox->setStyleSheet(
      QString("QCheckBox::indicator { width: %1px; height: %1px; }").arg(Size::HeaderCheckbox));
  detailsCheckBox->setChecked(showDetails);
  detailsCheckBox->show();

  connect(detailsCheckBox, &QCheckBox::clicked, this, &VMGFileTreeHeaderView::toggleShowDetails);
  connect(Settings::the(), &Settings::vgmFileTreeShowDetailsChanged, this,
          &VMGFileTreeHeaderView::onShowDetailsChanged);
}

void VMGFileTreeHeaderView::showEvent(QShowEvent* event) {
  QFont headerFont = font();
  headerFont.setPointSize(headerFont.pointSize() - 1);
#ifdef Q_OS_MAC
  setStyleSheet("QHeaderView::section { border-top: 0px solid white; margin-left: 4px; "
                "padding-bottom: -1px; padding-top: -3px; }");
  resizeSection(0, width());
#endif
  detailsCheckBox->setFont(headerFont);
  QHeaderView::showEvent(event);
}

void VMGFileTreeHeaderView::resizeEvent(QResizeEvent* event) {
  QHeaderView::resizeEvent(event);
  resizeSection(0, width());
  detailsCheckBox->move(width() - detailsCheckBox->width() - 10, (height() - detailsCheckBox->height()) / 2);
}

void VMGFileTreeHeaderView::onShowDetailsChanged(bool showDetails) const {
  detailsCheckBox->setChecked(showDetails);
}

void VMGFileTreeHeaderView::toggleShowDetails() const {
  Settings::the()->VGMFileTreeView.setShowDetails(detailsCheckBox->isChecked());
}

namespace {

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
  QStyle* style = paintOption.widget ? paintOption.widget->style() : QApplication::style();

  QTextDocument document;
  document.setHtml(paintOption.text);
  QAbstractTextDocumentLayout::PaintContext textContext;
  textContext.palette = paintOption.palette;
  if (paintOption.state.testFlag(QStyle::State_Selected)) {
    const QColor textColor = selectedTreeTextColor(paintOption);
    textContext.palette.setColor(QPalette::Text, textColor);
    textContext.palette.setColor(QPalette::WindowText, textColor);
    textContext.palette.setColor(QPalette::ButtonText, textColor);
    textContext.palette.setColor(QPalette::HighlightedText, textColor);
  }

  paintOption.text.clear();
  style->drawControl(QStyle::CE_ItemViewItem, &paintOption, painter, paintOption.widget);
  const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &paintOption);
  painter->save();
  painter->translate(textRect.topLeft());
  textContext.clip = textRect.translated(-textRect.topLeft());
  document.documentLayout()->draw(painter, textContext);
  painter->restore();
}

QSize VGMTreeDisplayItem::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
  QStyleOptionViewItem styleOption = option;
  initStyleOption(&styleOption, index);
  QTextDocument document;
  document.setHtml(styleOption.text);
  document.setTextWidth(styleOption.rect.width());
  return QSize(document.idealWidth(), document.size().height());
}

VGMFileTreeView::VGMFileTreeView(const vgmtrans::ui::SourceInspectorModel& model, QWidget* parent)
    : QTreeWidget(parent), model_(model) {
  setHeaderLabel("File structure");
  showDetails_ = Settings::the()->VGMFileTreeView.showDetails();
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  horizontalScrollBar()->setEnabled(false);
  setHeader(new VMGFileTreeHeaderView(Qt::Horizontal, this, showDetails_));
  setItemDelegate(new VGMTreeDisplayItem(this));
  playbackBrush_ = QBrush(palette().color(QPalette::Accent));

  appendChildren(invisibleRootItem(), model_.roots());
  connect(Settings::the(), &Settings::vgmFileTreeShowDetailsChanged, this, &VGMFileTreeView::onShowDetailsChanged);
}

void VGMFileTreeView::appendChildren(QTreeWidgetItem* parent,
                                     std::span<const vgmtrans::core::SourceAnnotationId> children) {
  for (const auto id : children) {
    const auto* annotation = model_.annotation(id);
    if (annotation == nullptr) {
      continue;
    }
    auto* item = new VGMTreeItem(id, annotation->range.offset);
    setItemText(item);
    item->setData(0, Qt::UserRole, id.value);
    parent->addChild(item);
    items_.emplace(id.value, item);
    appendChildren(item, model_.children(id));
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

void VGMFileTreeView::setItemText(VGMTreeItem* item) const {
  const auto* annotation = model_.annotation(item->annotation());
  if (annotation == nullptr) {
    return;
  }
  item->setText(0, SourceInspectorPresentation::treeText(*annotation, showDetails_));
  item->setIcon(0, SourceInspectorPresentation::icon(*annotation));
  item->setToolTip(0, SourceInspectorPresentation::description(*annotation));
}

void VGMFileTreeView::onShowDetailsChanged(bool show) {
  showDetails_ = show;
  model()->blockSignals(true);
  updateItemTextRecursively(invisibleRootItem());
  model()->blockSignals(false);
  doItemsLayout();
  scrollToItem(currentItem());
}

void VGMFileTreeView::updateItemTextRecursively(QTreeWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  if (item->type() == VGMTreeItem::ItemType) {
    setItemText(static_cast<VGMTreeItem*>(item));
  }
  for (int index = 0; index < item->childCount(); ++index) {
    updateItemTextRecursively(item->child(index));
  }
}
