/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

// Playback position updates can originate from several synchronized views. Keep
// the origin in the UI boundary so a future value-native player and inspector can
// reconnect without recreating the feedback-loop prevention logic.
enum class PositionChangeOrigin {
  Playback,
  SeekBar,
  HexView,
};
