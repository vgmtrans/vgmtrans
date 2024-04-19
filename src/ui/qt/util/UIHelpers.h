/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <string>

class QScrollArea;
class QWidget;
class QPixmap;
class QGraphicsEffect;
class VGMItem;

QScrollArea* getContainingScrollArea(QWidget* widget);
void applyEffectToPixmap(QPixmap &src, QPixmap &tgt, QGraphicsEffect *effect, int extent = 0);

std::string OpenSaveFileDialog(const std::string& suggested_filename, const std::string& extension);
std::string OpenSaveDirDialog();
