/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/SequenceModulationProfile.h"

#include "value/synth/SynthModel.h"
#include "value/synth/SynthMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vgmtrans::core {

namespace {

struct ObservedRange {
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();

  void observe(double value, bool includeZero = false) {
    if (!std::isfinite(value) || value < 0.0 || (!includeZero && value == 0.0)) {
      return;
    }
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
  }

  [[nodiscard]] bool observed() const noexcept {
    return std::isfinite(minimum) && std::isfinite(maximum);
  }

  [[nodiscard]] std::optional<ModulationRange> range(bool requirePositiveMaximum = false) const {
    if (!observed() || (requirePositiveMaximum && maximum <= 0.0)) {
      return std::nullopt;
    }
    return ModulationRange{.minimum = minimum, .maximum = maximum};
  }
};

struct LfoObservation {
  double maxDepth = 0.0;
  ObservedRange rate;
  ObservedRange delay;
  std::optional<LfoWaveform> waveform;
  bool hasConflictingWaveforms = false;
  TremoloGainMode gainMode = TremoloGainMode::BipolarAroundNominal;

  void observeWaveform(std::optional<LfoWaveform> observed) {
    if (!observed || hasConflictingWaveforms) {
      return;
    }
    if (waveform && waveform != observed) {
      waveform.reset();
      hasConflictingWaveforms = true;
    } else {
      waveform = observed;
    }
  }
};

[[nodiscard]] double normalizedLinear(double value, double maximum) noexcept {
  if (!std::isfinite(value) || !std::isfinite(maximum) || value <= 0.0 || maximum <= 0.0) {
    return 0.0;
  }
  return std::clamp(value / maximum, 0.0, 1.0);
}

template <class Convert>
[[nodiscard]] double normalizedSynthRange(double value, const ModulationRange& range, Convert convert) noexcept {
  if (!std::isfinite(value) || value < 0.0 || range.minimum < 0.0 || range.maximum <= range.minimum) {
    return 0.0;
  }
  const s32 minimum = convert(range.minimum);
  const s32 maximum = convert(range.maximum);
  if (maximum <= minimum) {
    return 0.0;
  }
  const s32 current = convert(std::clamp(value, range.minimum, range.maximum));
  return std::clamp(static_cast<double>(current - minimum) / static_cast<double>(maximum - minimum), 0.0, 1.0);
}

[[nodiscard]] double normalizedHertz(double hertz, const ModulationRange& range) noexcept {
  return normalizedSynthRange(hertz, range, [](double value) { return synthAmountFromHertz(value); });
}

[[nodiscard]] double normalizedSeconds(double seconds, const ModulationRange& range) noexcept {
  return normalizedSynthRange(seconds, range, [](double value) {
    return synthAmountFromSeconds(synthSecondsRangeMinimum(value));
  });
}

[[nodiscard]] u8 midi7(double normalized) noexcept {
  return static_cast<u8>(
      std::clamp<s32>(static_cast<s32>(std::lround(std::clamp(normalized, 0.0, 1.0) * 127.0)), 0, 127));
}

void observeModulation(const ModulationPerformanceEvent& event, LfoObservation& vibrato, LfoObservation& tremolo,
                       LfoObservation& pan) {
  switch (event.target) {
    case ModulationPerformanceTarget::VibratoDepth:
      vibrato.observeWaveform(event.waveform);
      if (event.pitchDepthSemitones) {
        vibrato.maxDepth = std::max(vibrato.maxDepth, std::abs(*event.pitchDepthSemitones) * 100.0);
      }
      break;
    case ModulationPerformanceTarget::VibratoRate:
      vibrato.observeWaveform(event.waveform);
      if (event.frequencyHz) {
        vibrato.rate.observe(*event.frequencyHz);
      }
      break;
    case ModulationPerformanceTarget::TremoloDepth:
      tremolo.observeWaveform(event.waveform);
      if (event.volumeDepthDecibels) {
        tremolo.maxDepth = std::max(tremolo.maxDepth, std::abs(*event.volumeDepthDecibels));
        if (std::abs(*event.volumeDepthDecibels) > 0.0) {
          tremolo.gainMode = event.tremoloGainMode;
        }
      } else if (event.volumeDepthLinearGain) {
        const double depth = std::clamp(std::abs(*event.volumeDepthLinearGain), 0.0, 1.0 - 1e-9);
        tremolo.maxDepth = std::max(tremolo.maxDepth, -20.0 * std::log10(1.0 - depth));
        if (depth > 0.0) {
          tremolo.gainMode = TremoloGainMode::BipolarAroundNominal;
        }
      }
      break;
    case ModulationPerformanceTarget::TremoloRate:
      tremolo.observeWaveform(event.waveform);
      if (event.frequencyHz) {
        tremolo.rate.observe(*event.frequencyHz);
      }
      break;
    case ModulationPerformanceTarget::PanDepth:
      if (event.panDepth) {
        pan.maxDepth = std::max(pan.maxDepth, std::abs(*event.panDepth));
      }
      break;
    case ModulationPerformanceTarget::PanRate:
      if (event.frequencyHz) {
        pan.rate.observe(*event.frequencyHz);
      }
      break;
  }
}

[[nodiscard]] const ModulationRange* rateRange(const ModulationPerformanceEvent& event,
                                               const SequenceModulationProfile& profile) noexcept {
  switch (event.target) {
    case ModulationPerformanceTarget::VibratoRate:
      return profile.instruments.vibrato ? &profile.instruments.vibrato->rateHertz : nullptr;
    case ModulationPerformanceTarget::TremoloRate:
      return profile.instruments.tremolo ? &profile.instruments.tremolo->rateHertz : nullptr;
    case ModulationPerformanceTarget::PanRate:
      return profile.panRateHertz ? &*profile.panRateHertz : nullptr;
    case ModulationPerformanceTarget::VibratoDepth:
    case ModulationPerformanceTarget::TremoloDepth:
    case ModulationPerformanceTarget::PanDepth:
      return nullptr;
  }
  return nullptr;
}

}  // namespace

SequenceModulationProfile analyzeSequenceModulation(const PerformanceSequence& sequence) {
  LfoObservation vibrato;
  LfoObservation tremolo;
  LfoObservation pan;

  for (const auto& track : sequence.tracks) {
    if (!track.hasPhysicalModulation) {
      continue;
    }
    for (const auto& event : track.events) {
      if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
        observeModulation(*modulation, vibrato, tremolo, pan);
      } else if (const auto* vibratoDelay = std::get_if<VibratoDelayPerformanceEvent>(&event);
                 vibratoDelay != nullptr && vibratoDelay->milliseconds) {
        vibrato.delay.observe(*vibratoDelay->milliseconds / 1000.0, true);
      } else if (const auto* tremoloDelay = std::get_if<TremoloDelayPerformanceEvent>(&event);
                 tremoloDelay != nullptr && tremoloDelay->milliseconds) {
        tremolo.delay.observe(*tremoloDelay->milliseconds / 1000.0, true);
      }
    }
  }

  SequenceModulationProfile profile{
      .maxPanDepth = pan.maxDepth,
      .panRateHertz = pan.rate.range(),
  };
  if (vibrato.maxDepth > 0.0 && vibrato.rate.observed()) {
    profile.instruments.vibrato = VibratoSpec{
        .maxDepthCents = vibrato.maxDepth,
        .rateHertz = *vibrato.rate.range(),
        .waveform = vibrato.waveform,
        .delaySeconds = vibrato.delay.range(true),
    };
  }
  if (tremolo.maxDepth > 0.0 && tremolo.rate.observed()) {
    profile.instruments.tremolo = TremoloSpec{
        .maxDepthDb = tremolo.maxDepth,
        .rateHertz = *tremolo.rate.range(),
        .waveform = tremolo.waveform,
        .gainMode = tremolo.gainMode,
        .delaySeconds = tremolo.delay.range(true),
    };
  }
  return profile;
}

double modulationControllerAmount(const ModulationPerformanceEvent& event,
                                  const SequenceModulationProfile* profile) noexcept {
  if (profile == nullptr) {
    return std::clamp(event.amount, 0.0, 1.0);
  }

  switch (event.target) {
    case ModulationPerformanceTarget::VibratoDepth:
      if (event.pitchDepthSemitones && profile->instruments.vibrato) {
        return normalizedLinear(std::abs(*event.pitchDepthSemitones) * 100.0,
                                profile->instruments.vibrato->maxDepthCents);
      }
      break;
    case ModulationPerformanceTarget::TremoloDepth:
      if (event.volumeDepthDecibels && profile->instruments.tremolo) {
        return normalizedLinear(std::abs(*event.volumeDepthDecibels), profile->instruments.tremolo->maxDepthDb);
      }
      if (event.volumeDepthLinearGain && profile->instruments.tremolo) {
        const double depth = std::clamp(std::abs(*event.volumeDepthLinearGain), 0.0, 1.0 - 1e-9);
        return normalizedLinear(-20.0 * std::log10(1.0 - depth), profile->instruments.tremolo->maxDepthDb);
      }
      break;
    case ModulationPerformanceTarget::PanDepth:
      if (event.panDepth && profile->maxPanDepth > 0.0) {
        return normalizedLinear(std::abs(*event.panDepth), profile->maxPanDepth);
      }
      break;
    case ModulationPerformanceTarget::VibratoRate:
    case ModulationPerformanceTarget::TremoloRate:
    case ModulationPerformanceTarget::PanRate:
      if (event.frequencyHz) {
        if (const auto* range = rateRange(event, *profile)) {
          return normalizedHertz(*event.frequencyHz, *range);
        }
      }
      break;
  }
  return std::clamp(event.amount, 0.0, 1.0);
}

u8 vibratoDelayControllerValue(const VibratoDelayPerformanceEvent& event,
                               const SequenceModulationProfile* profile) noexcept {
  if (profile == nullptr || !event.milliseconds || !profile->instruments.vibrato ||
      !profile->instruments.vibrato->delaySeconds) {
    return event.midiValue;
  }
  return midi7(normalizedSeconds(*event.milliseconds / 1000.0, *profile->instruments.vibrato->delaySeconds));
}

u8 tremoloDelayControllerValue(const TremoloDelayPerformanceEvent& event,
                               const SequenceModulationProfile* profile) noexcept {
  if (profile == nullptr || !event.milliseconds || !profile->instruments.tremolo ||
      !profile->instruments.tremolo->delaySeconds) {
    return event.midiValue;
  }
  return midi7(normalizedSeconds(*event.milliseconds / 1000.0, *profile->instruments.tremolo->delaySeconds));
}

void applySequenceModulation(InstrumentSetAsset& instrumentSet, const SequenceModulationProfile& profile) {
  for (auto& instrument : instrumentSet.instruments) {
    if (profile.instruments.vibrato) {
      instrument.modulation.vibrato = profile.instruments.vibrato;
    }
    if (profile.instruments.tremolo) {
      instrument.modulation.tremolo = profile.instruments.tremolo;
    }
  }
}

}  // namespace vgmtrans::core
