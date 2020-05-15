/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QIcon>
#include <VGMFile.h>
#include <VGMItem.h>

const QIcon &iconForItemType(VGMItem::Icon type);

QColor colorForEventColor(uint8_t eventColor);
QColor textColorForEventColor(uint8_t eventColor);

template<typename ... Base>
struct Visitor : Base ... {
    using Base::operator()...;
};

template<typename ... T> Visitor(T...) -> Visitor<T...>;
