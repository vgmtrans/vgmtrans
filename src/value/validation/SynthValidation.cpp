/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/validation/SynthValidation.h"

#include "value/synth/SynthModel.h"

#include <cmath>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::optional<SourceRange> validRange(SourceRange range) {
  return range.valid() ? std::optional<SourceRange>{range} : std::nullopt;
}

void validateEnvelope(ValidationReport& report, const Envelope& envelope, SourceRange range) {
  // Coarse integer envelopes use legacy sentinel values, but precise envelope
  // fields are real units and should never be negative or non-finite.
  const auto checkSeconds = [&](const std::optional<double> value, std::string_view code, std::string_view name) {
    if (value && (!std::isfinite(*value) || *value < 0.0)) {
      report.error(std::string(code), "Synth envelope " + std::string(name) + " time was not finite and nonnegative",
                   validRange(range));
    }
  };

  checkSeconds(envelope.attackSeconds, "synth.envelope.attack", "attack");
  checkSeconds(envelope.holdSeconds, "synth.envelope.hold", "hold");
  checkSeconds(envelope.decaySeconds, "synth.envelope.decay", "decay");
  checkSeconds(envelope.releaseSeconds, "synth.envelope.release", "release");

  if (envelope.sustainAmplitude && (!std::isfinite(*envelope.sustainAmplitude) || *envelope.sustainAmplitude < 0.0)) {
    report.error("synth.envelope.sustain", "Synth envelope sustain amplitude was not finite and nonnegative",
                 validRange(range));
  }
}

}  // namespace

ValidationReport validateInstrumentSet(const InstrumentSetAsset& instrumentSet) {
  ValidationReport report;

  for (const auto& instrument : instrumentSet.instruments) {
    if (!std::isfinite(instrument.reverb) || instrument.reverb < 0.0) {
      report.error("synth.instrument.reverb", "Synth instrument reverb send was not finite and nonnegative",
                   validRange(instrument.range));
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
      validateEnvelope(report, region.envelope, region.range);
    }
  }

  return report;
}

ValidationReport validateSampleCollection(const SampleCollectionAsset& sampleCollection) {
  ValidationReport report;

  for (const auto& sample : sampleCollection.samples.samples) {
    if (sample.channels == 0) {
      report.error("synth.sample.channels", "Synth sample had zero channels", validRange(sample.encodedData));
    }
    if (!std::isfinite(sample.attenuationDb)) {
      report.error("synth.sample.attenuation", "Synth sample attenuation was not finite",
                   validRange(sample.encodedData));
    }
  }

  return report;
}

}  // namespace vgmtrans::core
