/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/AkaoSnes/AkaoSnes.h"
#include "value/synth/SnesDsp.h"

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao_snes {

[[nodiscard]] constexpr u8 akaoSnesV1Gain(u8 parameter) {
  return static_cast<u8>(0xa0 | (parameter & 0x1f));
}

[[nodiscard]] constexpr u8 akaoSnesV1DurationRate(u8 parameter) {
  if (parameter > 100) {
    return 0;
  }
  return parameter == 100 ? parameter : static_cast<u8>((static_cast<u16>(parameter) << 8) / 100);
}

[[nodiscard]] inline AkaoSnesV1VolumeEnvelopes captureAkaoSnesV1VolumeEnvelopes(core::ByteReader reader,
                                                                                u32 tableAddress) {
  AkaoSnesV1VolumeEnvelopes result;
  for (size_t index = 0; index < kAkaoSnesV1VolumeEnvelopeCount; ++index) {
    const u32 pointerAddress = tableAddress + static_cast<u32>(index * 2);
    if (!reader.has(pointerAddress, 2)) {
      continue;
    }

    const u32 curveAddress = reader.le16(pointerAddress);
    std::vector<u8> curve;
    for (u32 offset = 0; offset < 0x100 && reader.has(curveAddress + offset, 1); ++offset) {
      const u8 value = reader.u8At(curveAddress + offset);
      if (value == 0) {
        result[index] = std::move(curve);
        break;
      }
      curve.push_back(value);
    }
  }
  return result;
}

// FF4 multiplies track volume by DC's per-note software table. Its terminator
// enables the exponential GAIN selected by DD; DE can replace that decay with
// B1 at a duration-relative point.
class AkaoSnesV1EnvelopeState {
public:
  explicit AkaoSnesV1EnvelopeState(const AkaoSnesV1VolumeEnvelopes* envelopes = nullptr) : envelopes(envelopes) {}

  void selectVolumeEnvelope(u8 index) { selectedVolumeEnvelope = index; }
  void selectGain(u8 parameter) { selectedGain = akaoSnesV1Gain(parameter); }
  void selectDurationRate(u8 parameter) { selectedDurationRate = akaoSnesV1DurationRate(parameter); }

  void beginNote(u16 duration) {
    elapsedSeconds = 0.0;
    gainStartSeconds.reset();
    gainStartEnvelope = kDspEnvelopeMaximum;
    activeGain = selectedGain;
    releaseTicks = static_cast<u16>((duration * selectedDurationRate) >> 8);

    activeCurve = selectedVolumeEnvelope ? volumeEnvelope(*selectedVolumeEnvelope) : std::nullopt;
    volumeMultiplier = envelopes == nullptr ? 0xff : (!activeCurve || activeCurve->empty() ? 0 : activeCurve->front());
  }

  [[nodiscard]] double level() const {
    return (static_cast<double>(volumeMultiplier) / 255.0) *
           (static_cast<double>(gainEnvelopeAt(elapsedSeconds)) / kDspEnvelopeMaximum);
  }

  [[nodiscard]] bool advance(double seconds) {
    const u8 previousMultiplier = volumeMultiplier;
    const s16 previousEnvelope = gainEnvelopeAt(elapsedSeconds);
    elapsedSeconds += seconds;

    if (activeCurve) {
      const size_t index = static_cast<size_t>(elapsedSeconds / kVolumeEnvelopeStepSeconds);
      if (index < activeCurve->size()) {
        volumeMultiplier = (*activeCurve)[index];
      } else {
        if (!activeCurve->empty()) {
          volumeMultiplier = activeCurve->back();
        }
        if (!gainStartSeconds) {
          startGain(activeGain, activeCurve->size() * kVolumeEnvelopeStepSeconds);
        }
        activeCurve.reset();
      }
    }

    // DE's countdown advances on music ticks. If DC already selected GAIN
    // during the elapsed interval, DE continues from that ENVX value with B1.
    if (releaseTicks != 0 && --releaseTicks == 0) {
      startGain(0xb1, elapsedSeconds);
    }

    return volumeMultiplier != previousMultiplier || gainEnvelopeAt(elapsedSeconds) != previousEnvelope;
  }

private:
  [[nodiscard]] std::optional<std::span<const u8>> volumeEnvelope(u8 index) const {
    if (envelopes == nullptr || index >= envelopes->size()) {
      return std::nullopt;
    }
    const auto& curve = (*envelopes)[index];
    return curve ? std::optional{std::span<const u8>{*curve}} : std::nullopt;
  }

  [[nodiscard]] s16 gainEnvelopeAt(double seconds) const {
    return gainStartSeconds ? core::snesDspGainEnvelopeValue(activeGain, gainStartEnvelope, seconds - *gainStartSeconds)
                            : kDspEnvelopeMaximum;
  }

  void startGain(u8 gain, double seconds) {
    gainStartEnvelope = gainEnvelopeAt(seconds);
    gainStartSeconds = seconds;
    activeGain = gain;
  }

  static constexpr s16 kDspEnvelopeMaximum = 0x7ff;
  static constexpr double kVolumeEnvelopeStepSeconds =
      2.0 / akaoSnesFrameRateHz(akaoSnesTimer0Frequency(AKAOSNES_V1, AKAOSNES_V1_FF4));

  std::optional<u8> selectedVolumeEnvelope;
  const AkaoSnesV1VolumeEnvelopes* envelopes;
  u8 selectedGain = 0;
  u8 selectedDurationRate = 0;

  u8 activeGain = 0;
  u8 volumeMultiplier = 0xff;
  u16 releaseTicks = 0;
  std::optional<std::span<const u8>> activeCurve;
  double elapsedSeconds = 0.0;
  std::optional<double> gainStartSeconds;
  s16 gainStartEnvelope = kDspEnvelopeMaximum;
};

}  // namespace vgmtrans::formats::akao_snes
