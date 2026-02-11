/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>

#include <array>
#include <cstdint>
#include <vector>

class VGMFile;

namespace HexViewFrame {

struct SelectionRange {
  uint32_t offset = 0;
  uint32_t length = 0;
};

struct Style {
  QColor bg;
  QColor fg;
};

struct GlyphAtlasView {
  const QImage* image = nullptr;
  const std::array<QRectF, 128>* uvTable = nullptr;
  uint64_t version = 0;
};

struct Data {
  VGMFile* vgmfile = nullptr;
  QSize viewportSize;
  int totalLines = 0;
  int scrollY = 0;
  int lineHeight = 0;
  int charWidth = 0;
  int charHalfWidth = 0;
  int hexStartX = 0;
  bool shouldDrawOffset = true;
  bool shouldDrawAscii = true;
  bool addressAsHex = true;

  qreal overlayOpacity = 0.0;
  qreal shadowBlur = 0.0;
  qreal shadowStrength = 0.0;
  QPointF shadowOffset{0.0, 0.0};
  float shadowEdgeCurve = 1.0f;

  QColor windowColor;
  QColor windowTextColor;
  const std::vector<uint16_t>* styleIds = nullptr;
  std::vector<Style> styles;
  std::vector<SelectionRange> selections;
  std::vector<SelectionRange> fadeSelections;
  GlyphAtlasView glyphAtlas;
};

}  // namespace HexViewFrame
