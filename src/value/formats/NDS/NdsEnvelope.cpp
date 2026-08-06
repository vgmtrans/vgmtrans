/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsEnvelope.h"

#include <array>
#include <cmath>
#include <limits>

namespace vgmtrans::formats::nds {

namespace {

constexpr double kEnvelopeIntervalSeconds = (2728.0 * 64.0) / 33513982.0;

constexpr std::array<s16, 128> kDecibelSquareTable = {
    -481, -480, -480, -480, -480, -480, -480, -480, -480, -460, -442, -425, -410, -396, -383, -371, -360, -349, -339,
    -330, -321, -313, -305, -297, -289, -282, -276, -269, -263, -257, -251, -245, -239, -234, -229, -224, -219, -214,
    -210, -205, -201, -196, -192, -188, -184, -180, -176, -173, -169, -165, -162, -158, -155, -152, -149, -145, -142,
    -139, -136, -133, -130, -127, -125, -122, -119, -116, -114, -111, -109, -106, -103, -101, -99,  -96,  -94,  -91,
    -89,  -87,  -85,  -82,  -80,  -78,  -76,  -74,  -72,  -70,  -68,  -66,  -64,  -62,  -60,  -58,  -56,  -54,  -52,
    -50,  -49,  -47,  -45,  -43,  -42,  -40,  -38,  -36,  -35,  -33,  -31,  -30,  -28,  -27,  -25,  -23,  -22,  -20,
    -19,  -17,  -16,  -14,  -13,  -11,  -10,  -8,   -7,   -6,   -4,   -3,   -1,   0};

constexpr std::array<u8, 19> kAttackTimeTable = {0x00, 0x01, 0x05, 0x0e, 0x1a, 0x26, 0x33, 0x3f, 0x49, 0x54,
                                                 0x5c, 0x64, 0x6d, 0x74, 0x7b, 0x7f, 0x84, 0x89, 0x8f};

// Converts an NDS decay or release byte into the rate used by the sound
// hardware. Callers validate the raw byte before reaching this helper.
[[nodiscard]] u16 fallingRate(u8 value) {
  if (value == 0x7f) {
    return 0xffff;
  }
  if (value == 0x7e) {
    return 0x3c00;
  }
  if (value < 0x32) {
    return static_cast<u16>(value * 2 + 1);
  }
  return static_cast<u16>(0x1e00 / (0x7e - value));
}

}  // namespace

std::optional<double> ndsAttackSeconds(u8 attack) {
  if (attack > 0x7f) {
    return std::nullopt;
  }

  u8 realAttack = 0xff - attack;
  if (attack >= 0x6d) {
    realAttack = kAttackTimeTable[0x7f - attack];
  }

  int count = 0;
  constexpr long attackThreshold = 0x16980 / 10;
  for (long value = 0x16980; value > attackThreshold; value = (value * realAttack) >> 8) {
    ++count;
  }
  return count * kEnvelopeIntervalSeconds;
}

std::optional<double> ndsDecaySeconds(u8 decay) {
  if (decay > 0x7f) {
    return std::nullopt;
  }
  if (decay == 0x7f) {
    return 0.001;
  }
  return (0x16980 / fallingRate(decay)) * kEnvelopeIntervalSeconds;
}

std::optional<double> ndsSustainAmplitude(u8 sustain) {
  if (sustain > 0x7f) {
    return std::nullopt;
  }
  if (sustain == 0x7f) {
    return 1.0;
  }
  if (sustain == 0) {
    return 0.0;
  }
  return std::pow(10.0, (kDecibelSquareTable[sustain] / 10.0) / 20.0);
}

std::optional<double> ndsReleaseSeconds(u8 release) {
  if (release > 0x7f) {
    return std::nullopt;
  }
  if (release == 0x7f) {
    return std::numeric_limits<double>::infinity();
  }
  return (0x16980 / fallingRate(release)) * kEnvelopeIntervalSeconds;
}

std::optional<core::Envelope> ndsEnvelope(u8 attack, u8 decay, u8 sustain, u8 release) {
  const auto attackSeconds = ndsAttackSeconds(attack);
  const auto decaySeconds = ndsDecaySeconds(decay);
  const auto sustainAmplitude = ndsSustainAmplitude(sustain);
  const auto releaseSeconds = ndsReleaseSeconds(release);
  if (!attackSeconds || !decaySeconds || !sustainAmplitude || !releaseSeconds) {
    return std::nullopt;
  }

  return core::Envelope{
      .attackSeconds = *attackSeconds,
      .decaySeconds = *decaySeconds,
      .releaseSeconds = *releaseSeconds,
      .sustainAmplitude = *sustainAmplitude,
  };
}

}  // namespace vgmtrans::formats::nds
