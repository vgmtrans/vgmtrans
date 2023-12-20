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

QScrollArea* getContainingScrollArea(QWidget* widget);
void applyEffectToPixmap(QPixmap &src, QPixmap &tgt, QGraphicsEffect *effect, int extent = 0);

std::string OpenSaveFileDialog(const std::wstring& suggested_filename, const std::wstring& extension);
std::string OpenSaveDirDialog();
