/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QIcon>
#include <VGMFile.h>
#include <VGMItem.h>

const QIcon &iconForFile(VGMFileVariant file);
const QIcon &iconForItemType(VGMItem::Icon type);

QColor colorForItemType(VGMItem::Type type);
QColor textColorForItemType(VGMItem::Type type);

QString getFullDescriptionForTooltip(VGMItem* item);

template<typename ... Base>
struct Visitor : Base ... {
  using Base::operator()...;
};

template<typename ... T> Visitor(T...) -> Visitor<T...>;
