/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QIcon>
#include "VGMFile.h"

QIcon MakeIconFromPath(QString path, QColor color = QColor(0x547aa5));
QIcon iconForFileType(FileType filetype);
QColor colorForEventColor(uint8_t eventColor);
QColor textColorForEventColor(uint8_t eventColor);
