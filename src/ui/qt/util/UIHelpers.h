/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <filesystem>
#include <string>

class QScrollArea;
class QWidget;
class QPixmap;
class QGraphicsEffect;
class VGMItem;

QScrollArea* getContainingScrollArea(const QWidget* widget);
void applyEffectToPixmap(QPixmap& src, QPixmap& tgt, QGraphicsEffect* effect, int extent = 0);

std::filesystem::path openSaveDirDialog();
std::filesystem::path openSaveFileDialog(const std::filesystem::path& suggested_filename, const std::string& extension);