/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

class QScrollArea;
class QWidget;
class QPixmap;
class QGraphicsEffect;

QScrollArea* getContainingScrollArea(QWidget* widget);
void applyEffectToPixmap(QPixmap &src, QPixmap &tgt, QGraphicsEffect *effect, int extent = 0);