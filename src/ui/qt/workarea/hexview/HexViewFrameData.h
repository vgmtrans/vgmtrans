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
#include <span>
#include <vector>

class VGMFile;

namespace HexViewFrame {

struct SelectionRange {
  uint32_t offset = 0;
  uint32_t length = 0;
};

struct PlaybackSelection {
  uint32_t offset = 0;
  uint32_t length = 0;
  QColor glowColor;
};

struct FadePlaybackSelection {
  PlaybackSelection range;
  int64_t startMs = 0;
  float alpha = 0.0f;
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
  qreal dpr = 1.0;
  int totalLines = 0;
  int scrollY = 0;
  int lineHeight = 0;
  int charWidth = 0;
  bool shouldDrawOffset = true;
  bool shouldDrawAscii = true;
  bool addressAsHex = true;
  bool seekModifierActive = false;

  qreal overlayOpacity = 0.0;
  qreal shadowBlur = 0.0;
  qreal shadowStrength = 0.0;
  QPointF shadowOffset{0.0, 0.0};
  float playbackGlowStrength = 1.0f;
  float playbackGlowRadius = 0.5f;
  float shadowEdgeCurve = 1.0f;
  float playbackGlowEdgeCurve = 1.0f;

  QColor windowColor;
  QColor windowTextColor;
  std::span<const uint16_t> styleIds;
  std::span<const Style> styles;
  std::span<const SelectionRange> selections;
  std::span<const SelectionRange> fadeSelections;
  std::span<const PlaybackSelection> playbackSelections;
  std::span<const FadePlaybackSelection> fadePlaybackSelections;
  GlyphAtlasView glyphAtlas;
};

}  // namespace HexViewFrame
