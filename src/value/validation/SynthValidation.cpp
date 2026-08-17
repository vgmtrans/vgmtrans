/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/SynthValidation.h"

#include "value/synth/SynthModel.h"

#include <cmath>
#include <set>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::optional<SourceRange> validRange(SourceRange range) {
  return range.valid() ? std::optional<SourceRange>{range} : std::nullopt;
}

void validateEnvelope(ValidationReport& report, const Envelope& envelope, SourceRange range) {
  // Positive infinity explicitly represents a stage with no finite duration.
  const auto checkSeconds = [&](const std::optional<double> value, std::string_view code, std::string_view name) {
    if (value && (std::isnan(*value) || *value < 0.0)) {
      report.error(std::string(code), "Synth envelope " + std::string(name) + " time was negative or NaN",
                   validRange(range));
    }
  };

  checkSeconds(envelope.attackSeconds, "synth.envelope.attack", "attack");
  checkSeconds(envelope.holdSeconds, "synth.envelope.hold", "hold");
  checkSeconds(envelope.decaySeconds, "synth.envelope.decay", "decay");
  checkSeconds(envelope.secondDecaySeconds, "synth.envelope.second-decay", "second decay");
  checkSeconds(envelope.releaseSeconds, "synth.envelope.release", "release");

  if (envelope.sustainAmplitude && (!std::isfinite(*envelope.sustainAmplitude) || *envelope.sustainAmplitude < 0.0 ||
                                    *envelope.sustainAmplitude > 1.0)) {
    report.error("synth.envelope.sustain", "Synth envelope sustain amplitude was outside the 0.0 to 1.0 range",
                 validRange(range));
  }
}

void validateYm2151Voice(ValidationReport& report, const Ym2151Voice& voice, SourceRange range) {
  const auto error = [&](std::string_view field) {
    report.error("synth.ym2151." + std::string(field), "YM2151 voice " + std::string(field) + " was out of range",
                 validRange(range));
  };
  if (voice.algorithm > 7) {
    error("algorithm");
  }
  if (voice.feedback > 7) {
    error("feedback");
  }
  if (voice.operatorMask > 0x0f) {
    error("operator-mask");
  }
  if (voice.amplitudeModulationSensitivity > 3) {
    error("amplitude-modulation-sensitivity");
  }
  if (voice.pitchModulationSensitivity > 7) {
    error("pitch-modulation-sensitivity");
  }
  if (voice.noiseFrequency > 31) {
    error("noise-frequency");
  }
  for (const auto& op : voice.operators) {
    if (op.attackRate > 31 || op.firstDecayRate > 31 || op.secondDecayRate > 31 || op.releaseRate > 15 ||
        op.sustainLevel > 15 || op.totalLevel > 127 || op.keyScale > 3 || op.multiplier > 15 || op.detune1 > 7 ||
        op.detune2 > 3) {
      error("operator");
      break;
    }
  }
}

void validateSamples(ValidationReport& report, const SamplePool& pool) {
  for (const auto& sample : pool.samples) {
    if (sample.channels == 0) {
      report.error("synth.sample.channels", "Synth sample had zero channels", validRange(sample.encodedData));
    }
    if (!std::isfinite(sample.attenuationDb)) {
      report.error("synth.sample.attenuation", "Synth sample attenuation was not finite",
                   validRange(sample.encodedData));
    }
  }
}

}  // namespace

ValidationReport validateSoundBank(const SoundBankAsset& soundBank) {
  ValidationReport report;
  std::set<std::pair<std::string, u32>> identities;

  for (const auto& instrument : soundBank.instruments) {
    if (instrument.identity) {
      if (!instrument.identity->valid()) {
        report.error("synth.instrument.identity", "Synth instrument had an empty identity domain",
                     validRange(instrument.range));
      } else if (!identities.emplace(instrument.identity->domain, instrument.identity->key).second) {
        report.error("synth.instrument.duplicate-identity", "Synth instrument identity was duplicated in its set",
                     validRange(instrument.range));
      }
    }
    if (!std::isfinite(instrument.reverb) || instrument.reverb < 0.0) {
      report.error("synth.instrument.reverb", "Synth instrument reverb send was not finite and nonnegative",
                   validRange(instrument.range));
    }
    if (instrument.synthVoice) {
      std::visit(
          [&](const auto& voice) {
            using Voice = std::decay_t<decltype(voice)>;
            if constexpr (std::is_same_v<Voice, Ym2151Voice>) {
              validateYm2151Voice(report, voice, instrument.range);
            }
          },
          *instrument.synthVoice);
    }

    for (const auto& region : instrument.regions) {
      // Region pan is intentionally unipolar in the synth model. Performance
      // pan uses a different bipolar range and is validated elsewhere.
      if (!std::isfinite(region.pan) || region.pan < 0.0 || region.pan > 1.0) {
        report.error("synth.region.pan", "Synth region pan was outside the 0.0 to 1.0 range", validRange(region.range));
      }
      if (!std::isfinite(region.attenuationDb)) {
        report.error("synth.region.attenuation", "Synth region attenuation was not finite", validRange(region.range));
      }
      if (!std::isfinite(region.unityKey)) {
        report.error("synth.region.unity-key", "Synth region unity key was not finite", validRange(region.range));
      }
      validateEnvelope(report, region.envelope, region.range);
    }
  }
  validateSamples(report, soundBank.localSamples);

  return report;
}

ValidationReport validateSamplePool(const SamplePoolAsset& samplePool) {
  ValidationReport report;

  validateSamples(report, samplePool.pool);

  return report;
}

}  // namespace vgmtrans::core
