/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QIcon>
#include <VGMFile.h>
#include <VGMItem.h>

class QUrl;

const QIcon &iconForFile(VGMFileVariant file);
const QIcon &iconForItemType(VGMItem::Type type);

QColor colorForItemType(VGMItem::Type type);
QColor textColorForItemType(VGMItem::Type type);

QString getFullDescriptionForTooltip(VGMItem* item);

void qtOpenUrl(const QUrl& url);

template<typename ... Base>
struct Visitor : Base ... {
  using Base::operator()...;
};

template<typename ... T> Visitor(T...) -> Visitor<T...>;
