/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QColor>
#include <QSize>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace PianoRollFrame {

static constexpr int kMidiKeyCount = 128;

struct Note {
  uint32_t startTick = 0;
  uint32_t duration = 0;
  uint8_t key = 0;
  int16_t trackIndex = -1;
};

struct TimeSignature {
  uint32_t tick = 0;
  uint8_t numerator = 4;
  uint8_t denominator = 4;
};

struct Data {
  QSize viewportSize;
  float dpr = 1.0f;

  int totalTicks = 1;
  int currentTick = 0;
  int trackCount = 0;
  int ppqn = 48;
  uint32_t maxNoteDurationTicks = 1;

  int scrollX = 0;
  int scrollY = 0;
  int keyboardWidth = 72;
  int topBarHeight = 22;
  float pixelsPerTick = 0.10f;
  float pixelsPerKey = 12.0f;
  float elapsedSeconds = 0.0f;

  bool playbackActive = false;

  QColor backgroundColor;
  QColor noteBackgroundColor;
  QColor keyboardBackgroundColor;
  QColor topBarBackgroundColor;
  QColor topBarProgressColor;
  QColor beatLineColor;
  QColor measureLineColor;
  QColor keySeparatorColor;
  QColor noteOutlineColor;
  QColor scanLineColor;
  QColor whiteKeyColor;
  QColor blackKeyColor;
  QColor whiteKeyRowColor;
  QColor blackKeyRowColor;
  QColor dividerColor;
  QColor selectedNoteFillColor;
  QColor selectedNoteOutlineColor;
  QColor selectionRectFillColor;
  QColor selectionRectOutlineColor;

  std::vector<QColor> trackColors;
  std::shared_ptr<const std::vector<Note>> notes;
  std::shared_ptr<const std::vector<Note>> activeNotes;
  std::shared_ptr<const std::vector<Note>> selectedNotes;
  std::shared_ptr<const std::vector<TimeSignature>> timeSignatures;
  std::array<int, kMidiKeyCount> activeKeyTrack{};

  bool selectionRectVisible = false;
  float selectionRectX = 0.0f;
  float selectionRectY = 0.0f;
  float selectionRectW = 0.0f;
  float selectionRectH = 0.0f;
};

}  // namespace PianoRollFrame
