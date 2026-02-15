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
  float dpr = 1.0f;
  int trackCount = 0;
  QColor backgroundColor;
  QColor separatorColor;
  QColor whiteKeyColor;
  QColor blackKeyColor;
  bool playbackActive = false;
  std::vector<QColor> trackColors;
  std::vector<std::bitset<128>> activeKeysByTrack;
  std::vector<std::bitset<128>> previewKeysByTrack;
};

}  // namespace ActiveNoteFrame
