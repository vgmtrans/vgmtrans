/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QStyledItemDelegate>

class FixedHeightListDelegate : public QStyledItemDelegate {
public:
  explicit FixedHeightListDelegate(int itemHeight, QObject* parent = nullptr)
      : QStyledItemDelegate(parent), m_itemHeight(itemHeight) {}

protected:
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    hint.setHeight(m_itemHeight);
    return hint;
  }

private:
  int m_itemHeight;
};
