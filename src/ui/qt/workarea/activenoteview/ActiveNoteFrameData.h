/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QColor>
#include <QSize>

#include <bitset>
#include <vector>

namespace ActiveNoteFrame {

struct Data {
  QSize viewportSize;
  int scrollY = 0;
  int trackCount = 0;
  int rowHeight = 0;
  int leftGutter = 0;
  int topPadding = 0;
  int bottomPadding = 0;
  int rightPadding = 0;
  QColor backgroundColor;
  QColor separatorColor;
  QColor whiteKeyColor;
  QColor blackKeyColor;
  bool playbackActive = false;
  std::vector<QColor> trackColors;
  std::vector<std::bitset<128>> activeKeysByTrack;
};

}  // namespace ActiveNoteFrame
