/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"

#include <algorithm>

namespace vgmtrans::formats::sculpt_soft_snes {

void CurvePlayer::start(const Curve& data, u16 duration) {
  *this = CurvePlayer{
      .curve = &data,
      .value = data.points.front(),
      .hold = static_cast<u16>(std::max(0, int(duration) - data.releaseLead * data.speed)),
      .countdown = static_cast<u8>(data.speed + 1),
      .active = true,
  };
}

void CurvePlayer::tick() {
  if (!active) {
    return;
  }
  const auto& c = *curve;
  const u8 previous = countdown;
  countdown = countdown <= 1 ? c.speed : static_cast<u8>(countdown - 1);
  if (previous < 2) {
    const auto count = c.pointCount == 0 ? c.points.size() : c.pointCount;
    if (index + 1u >= count) {
      if (index != c.loopEnd) {
        active = false;
        return;
      }
      index = c.loopStart;
    } else {
      ++index;
      if (hold != 0) {
        hold = static_cast<u16>(std::max(0, int(hold) - c.speed));
        if (hold == 0 && c.loopEnd != 0xff) {
          index = c.loopEnd;
        } else if (index > c.loopEnd) {
          index = c.loopStart;
        }
      }
    }
  }
  if (!c.interpolate || c.speed < 2 || previous == 2) {
    value = c.points[index];
  } else {
    if (previous < 3) {
      // The SPC divides the unsigned distance, then restores its sign.
      increment = static_cast<s16>((int(c.points[index]) - int(value)) / c.speed);
    }
    value = static_cast<u16>(value + increment);
  }
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
