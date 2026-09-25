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

void LateCurvePlayer::start(const Curve& data, u8 scale, bool pitch) {
  const unsigned product = data.speed * scale;
  const unsigned speed = scale == 0 ? data.speed : std::clamp(product >> 5, 1u, 255u);
  const u8 fraction = scale == 0 || product < 32 ? 0 : product >= 8192 ? 1 : product & 31;
  *this = LateCurvePlayer{
      .curve = &data,
      .value = data.points.front(),
      .accumulator = static_cast<u16>(data.points.front() * (pitch ? 1 : 64)),
      .interval = static_cast<u8>(speed),
      .countdown = static_cast<u8>(std::min(255u, speed + 1)),
      .step = static_cast<u8>(255 - fraction),
      .precision = static_cast<u8>(pitch ? 1 : 64),
      .active = true,
  };
}

void LateCurvePlayer::tick(bool released) {
  if (!active) {
    return;
  }
  const auto& c = *curve;
  const unsigned sum = phase + step;
  phase = static_cast<u8>(sum);
  const bool advance = sum > 255;
  const u8 previous = countdown;
  if (advance) {
    countdown = countdown <= 1 ? interval : static_cast<u8>(countdown - 1);
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
        if (held) {
          if (released) {
            held = false;
            if (c.loopEnd < count) {
              index = c.loopEnd;
            }
          } else if (index > c.loopEnd) {
            index = c.loopStart;
          }
        }
      }
    }
  }
  const u16 target = static_cast<u16>(c.points[index] * precision);
  if (!advance || !c.interpolate || interval < 2 || previous == 2) {
    accumulator = target;
  } else {
    if (previous < 3) {
      increment = static_cast<s16>((int(target) - int(accumulator)) / interval);
    }
    accumulator = static_cast<u16>(accumulator + increment);
  }
  value = accumulator / precision;
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
