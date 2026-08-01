/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <optional>
#include <utility>

namespace vgmtrans::core {

struct Envelope {
  // An absent value means the stage was not specified. Positive infinity means
  // the stage has no finite duration. Sustain is linear amplitude in [0, 1].
  std::optional<double> attackSeconds;
  std::optional<double> holdSeconds;
  std::optional<double> decaySeconds;
  // Full-scale decay time for the rate used after the sustain level is reached.
  // Some sound chips keep fading during a held note instead of sustaining.
  std::optional<double> secondDecaySeconds;
  std::optional<double> releaseSeconds;
  std::optional<double> sustainAmplitude;

  friend bool operator==(const Envelope&, const Envelope&) noexcept = default;
};

[[nodiscard]] inline bool hasExplicitEnvelope(const Envelope& envelope) {
  return envelope.attackSeconds.has_value() || envelope.holdSeconds.has_value() || envelope.decaySeconds.has_value() ||
         envelope.secondDecaySeconds.has_value() || envelope.releaseSeconds.has_value() ||
         envelope.sustainAmplitude.has_value();
}

// Fields are explicit because an absent optional can mean either "clear this
// stage" or simply "this command did not touch this stage".
enum class EnvelopeFields : u8 {
  None = 0,
  Attack = 1 << 0,
  Hold = 1 << 1,
  Decay = 1 << 2,
  SecondDecay = 1 << 3,
  Release = 1 << 4,
  Sustain = 1 << 5,
  All = 0x3f,
};

[[nodiscard]] constexpr EnvelopeFields operator|(EnvelopeFields left, EnvelopeFields right) noexcept {
  return static_cast<EnvelopeFields>(static_cast<u8>(left) | static_cast<u8>(right));
}

[[nodiscard]] constexpr EnvelopeFields operator&(EnvelopeFields left, EnvelopeFields right) noexcept {
  return static_cast<EnvelopeFields>(static_cast<u8>(left) & static_cast<u8>(right));
}

constexpr EnvelopeFields& operator|=(EnvelopeFields& left, EnvelopeFields right) noexcept {
  left = left | right;
  return left;
}

[[nodiscard]] constexpr bool hasEnvelopeField(EnvelopeFields fields, EnvelopeFields field) noexcept {
  return (fields & field) != EnvelopeFields::None;
}

struct EnvelopeUpdate {
  Envelope values;
  // setFields copies the corresponding optional from values. An absent value
  // explicitly clears that field. inheritFields restores the instrument value.
  // Fields in neither mask retain the previous dynamic override.
  EnvelopeFields setFields = EnvelopeFields::None;
  EnvelopeFields inheritFields = EnvelopeFields::None;

  [[nodiscard]] static EnvelopeUpdate replace(Envelope values) {
    return EnvelopeUpdate{
        .values = std::move(values),
        .setFields = EnvelopeFields::All,
    };
  }

  [[nodiscard]] static EnvelopeUpdate set(Envelope values, EnvelopeFields fields) {
    return EnvelopeUpdate{
        .values = std::move(values),
        .setFields = fields,
    };
  }

  [[nodiscard]] static EnvelopeUpdate restore(EnvelopeFields fields = EnvelopeFields::All) {
    return EnvelopeUpdate{
        .inheritFields = fields,
    };
  }
};

enum class VoiceEnvelopeScope : u8 {
  FutureAttacks,
  ActiveVoices,
  ActiveVoicesAndFutureAttacks,
};

}  // namespace vgmtrans::core
