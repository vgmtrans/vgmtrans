/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

QString cssColor(const QColor &color);
QColor blendColors(const QColor &foreground, const QColor &background, qreal foregroundWeight);
QColor itemSelectionFillColor(const QPalette &palette,
                              QPalette::ColorGroup colorGroup = QPalette::Active);
QColor contrastingTextColor(const QColor &foreground, const QColor &background,
                            const QPalette &palette,
                            QPalette::ColorGroup colorGroup = QPalette::Active);
bool isDarkPalette(const QPalette &palette);
