/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QColor>
#include <QRectF>
#include <Qt>

#include <vector>

enum class RhiScrollButtonGlyph {
  Backward,
  Forward,
  Minus,
  Plus,
};

struct RhiScrollButtonSnapshot {
  QRectF rect;
  RhiScrollButtonGlyph glyph = RhiScrollButtonGlyph::Minus;
  bool hovered = false;
  bool pressed = false;
  bool visible = false;
};

struct RhiScrollBarSnapshot {
  bool visible = false;
  Qt::Orientation orientation = Qt::Horizontal;
  QRectF laneRect;
  QRectF thumbRect;
  bool thumbHovered = false;
  bool thumbPressed = false;
  std::vector<RhiScrollButtonSnapshot> arrowButtons;
};

struct RhiScrollChromeColors {
  QColor laneColor;
  QColor borderColor;
  QColor thumbColor;
  QColor thumbHoverColor;
  QColor thumbPressedColor;
  QColor buttonHoverColor;
  QColor buttonPressedColor;
  QColor glyphColor;
};

struct RhiScrollAreaChromeSnapshot {
  int rightMargin = 0;
  int bottomMargin = 0;
  QRectF cornerRect;
  RhiScrollChromeColors colors;
  RhiScrollBarSnapshot horizontal;
  RhiScrollBarSnapshot vertical;
  std::vector<RhiScrollButtonSnapshot> horizontalButtons;
  std::vector<RhiScrollButtonSnapshot> verticalButtons;
};
