/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QIcon>
#include <VGMFile.h>
#include <VGMItem.h>

const QIcon &iconForItemType(VGMItem::Icon type);
const QIcon &iconForFileType(FileType filetype);

QColor colorForEventColor(VGMItem::EventColor eventColor);
QColor textColorForEventColor(VGMItem::EventColor eventColor);
