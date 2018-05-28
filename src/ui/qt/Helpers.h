/*
* VGMTrans (c) 2018
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#ifndef VGMTRANS_HELPERS_H
#define VGMTRANS_HELPERS_H

#include <QIcon>
#include "VGMFile.h"

QIcon iconForFileType(FileType filetype);
QColor colorForEventColor(uint8_t eventColor);
QColor textColorForEventColor(uint8_t eventColor);

#endif //VGMTRANS_HELPERS_H
