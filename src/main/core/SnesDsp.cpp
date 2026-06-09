/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace vgmtrans::core {

namespace {

constexpr double kSampleRate = 32000.0;
constexpr std::array<unsigned, 32> kCounterRates{
    0x7800, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64,     48,   40,   32,   24,   20,  16,  12,  10,  8,   6,   5,   4,   3,   2,  1};

struct GainEnvelope {
  s16 envelope = 0;
  double seconds = 0.0;
};

struct SnesEnvelopeSeconds {
  double attack = 0.0;
  double decay = 0.0;
  double sustainLevel = 0.0;
  double sustain = 0.0;
  double release = 0.0;
};

[[nodiscard]] double linearAmpDecayTimeToLinDbDecayTime(double secondsToFullAttenuation) {
  if (secondsToFullAttenuation <= 0.0) {
    return 0.0;
  }

  constexpr double targetDbLeastSquares = 70.0;
  constexpr double targetDbInitialSlope = 140.0;
  constexpr double ln10 = 2.302585092994046;
  constexpr double kneeSeconds = 0.12;
  constexpr double kneeShape = 2.0;

  const double shortScale = targetDbInitialSlope / (20.0 / ln10);
  const double longScale = targetDbLeastSquares * ln10 / 45.0;
  const double x = secondsToFullAttenuation / kneeSeconds;
  const double weight = 1.0 / (1.0 + std::pow(x, kneeShape));

  return secondsToFullAttenuation * (weight * shortScale + (1.0 - weight) * longScale);
}

[[nodiscard]] double ampToDb(double amp) {
  constexpr double maxAttenuationDb = 100.0;
  if (amp == 0.0) {
    return maxAttenuationDb;
  }
  return std::min(-20.0 * std::log10(amp), maxAttenuationDb);
}

[[nodiscard]] u32 microsFromSeconds(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) {
    return kEnvelopeInfinite;
  }

  constexpr double microsPerSecond = 1000000.0;
  const double micros = seconds * microsPerSecond;
  if (micros >= static_cast<double>(std::numeric_limits<u32>::max())) {
    return std::numeric_limits<u32>::max();
  }
  return static_cast<u32>(std::lround(std::max(0.0, micros)));
}

[[nodiscard]] std::optional<double> preciseSeconds(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) {
    return std::nullopt;
  }
  return std::max(0.0, seconds);
}

[[nodiscard]] u32 permilleFromLevel(double level) {
  return static_cast<u32>(std::lround(std::clamp(level, 0.0, 1.0) * 1000.0));
}

[[nodiscard]] GainEnvelope emulateGainEnvelope(u8 gain, s16 envelopeFrom, s16 envelopeTo) {
  if (envelopeFrom < 0 || envelopeFrom > 0x7ff || envelopeTo < 0 || envelopeTo > 0x7ff) {
    return {};
  }

  const u8 mode = gain >> 5;
  u8 rate = gain & 0x1f;
  u32 ticks = 0;
  u32 exponentialTicks = 0;
  s16 exponentialFinalEnvelope = envelopeTo;

  s16 envelope = envelopeFrom;
  if (mode < 4) {
    envelope = static_cast<s16>(gain * 0x10);
    rate = 31;
  } else if (mode == 4) {
    while (envelope > envelopeTo) {
      envelope = static_cast<s16>(std::max(0, envelope - 0x20));
      ++ticks;
    }
  } else if (mode == 5) {
    while (envelope > envelopeTo) {
      const s16 previous = envelope;
      --envelope;
      envelope -= envelope >> 8;
      ++ticks;

      if (envelope <= 255 && previous > 255) {
        exponentialFinalEnvelope = envelope;
      }
      if (envelope > 255) {
        ++exponentialTicks;
      }
    }
  } else {
    s16 previous = envelope >= 0x20 ? static_cast<s16>(envelope - 0x20) : 0;
    while (envelope < envelopeTo) {
      envelope += 0x20;
      if (mode > 6 && static_cast<unsigned>(previous) >= 0x600) {
        envelope += 0x8 - 0x20;
      }
      previous = envelope;
      envelope = static_cast<s16>(std::min<int>(envelope, 0x7ff));
      ++ticks;
    }
  }

  double seconds = 0.0;
  if (mode < 4) {
    seconds = 0.0;
  } else if (mode == 4) {
    const u32 fullSamples = (0x800 / 0x20) * kCounterRates[rate];
    seconds = linearAmpDecayTimeToLinDbDecayTime(fullSamples / kSampleRate);
  } else if (mode == 5) {
    if (ticks == 0) {
      seconds = 0.0;
    } else if (envelopeFrom > 255) {
      const double dbAtStart = ampToDb(envelopeFrom / 2047.0);
      const double dbAtExpFinal = ampToDb(exponentialFinalEnvelope / 2047.0);
      const double timeAtExpFinal = (exponentialTicks * kCounterRates[rate]) / kSampleRate;
      seconds = timeAtExpFinal * (100.0 / (dbAtExpFinal - dbAtStart));
    } else if (envelope == 0 && ticks == 1) {
      seconds = kCounterRates[rate] / kSampleRate;
    } else {
      s16 finalEnvelope = envelope;
      u32 totalTicks = ticks;
      if (finalEnvelope == 0) {
        ++finalEnvelope;
        --totalTicks;
      }

      const double dbAtStart = ampToDb(envelopeFrom / 2047.0);
      const double dbAtFinal = ampToDb(finalEnvelope / 2047.0);
      const double timeAtFinal = (totalTicks * kCounterRates[rate]) / kSampleRate;
      seconds = timeAtFinal * (100.0 / (dbAtFinal - dbAtStart));
    }
  } else {
    const u32 fullSamples = (0x800 / 0x20) * kCounterRates[rate];
    seconds = fullSamples / kSampleRate;
  }

  return GainEnvelope{
      .envelope = envelope,
      .seconds = seconds,
  };
}

[[nodiscard]] SnesEnvelopeSeconds convertSnesAdsr(u8 adsr1, u8 adsr2, u8 gain, u16 envelopeFrom) {
  const bool adsrEnabled = (adsr1 & 0x80) != 0;

  if (adsrEnabled) {
    const u8 attackRate = adsr1 & 0x0f;
    const u8 decayRate = (adsr1 & 0x70) >> 4;
    const u8 sustainLevel = (adsr2 & 0xe0) >> 5;
    const u8 sustainRate = adsr2 & 0x1f;

    double attackSeconds = 0.0;
    if (attackRate < 15) {
      attackSeconds = kCounterRates[attackRate * 2 + 1] * 64 / kSampleRate;
    } else {
      attackSeconds = 2 / kSampleRate;
    }

    s16 envelope = 0x7ff;
    s16 envelopeSustainStart = envelope;
    double decaySeconds = 0.0;
    if (sustainLevel != 7) {
      const u8 decayGainRate = 0x10 | (decayRate << 1);
      const auto decay =
          emulateGainEnvelope(0xa0 | decayGainRate, envelope, static_cast<s16>((sustainLevel << 8) | 0xff));
      envelopeSustainStart = decay.envelope;
      envelope = decay.envelope;
      decaySeconds = decay.seconds;
    }

    double sustainSeconds = -1.0;
    if (sustainRate != 0) {
      sustainSeconds = emulateGainEnvelope(0xa0 | sustainRate, envelope, 0).seconds;
    }

    const u32 releaseSamples = (envelopeSustainStart + 7) / 8;
    return SnesEnvelopeSeconds{
        .attack = attackSeconds,
        .decay = decaySeconds,
        .sustainLevel = (sustainLevel + 1) / 8.0,
        .sustain = sustainSeconds,
        .release = linearAmpDecayTimeToLinDbDecayTime(releaseSamples / kSampleRate),
    };
  }

  const u8 mode = gain >> 5;
  if (mode < 4) {
    const u32 releaseSamples = (envelopeFrom + 7) / 8;
    return SnesEnvelopeSeconds{
        .attack = 0.0,
        .decay = -1.0,
        .sustainLevel = (gain & 0x7f) / 128.0,
        .sustain = -1.0,
        .release = linearAmpDecayTimeToLinDbDecayTime(releaseSamples / kSampleRate),
    };
  }

  const s16 envelopeTo = mode >= 6 ? 0x7ff : 0;
  const auto gainEnvelope = emulateGainEnvelope(gain, static_cast<s16>(envelopeFrom), envelopeTo);
  if (mode >= 6) {
    const u32 releaseSamples = (envelopeTo + 7) / 8;
    return SnesEnvelopeSeconds{
        .attack = gainEnvelope.seconds,
        .decay = -1.0,
        .sustainLevel = 1.0,
        .sustain = -1.0,
        .release = linearAmpDecayTimeToLinDbDecayTime(releaseSamples / kSampleRate),
    };
  }

  const u32 releaseSamples = (envelopeFrom + 7) / 8;
  return SnesEnvelopeSeconds{
      .attack = 0.0,
      .decay = gainEnvelope.seconds,
      .sustainLevel = 0.0,
      .sustain = 0.0,
      .release = linearAmpDecayTimeToLinDbDecayTime(releaseSamples / kSampleRate),
  };
}

}  // namespace

Envelope snesDspEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  auto envelope = convertSnesAdsr(adsr1, adsr2, gain, 0x7ff);
  if ((adsr1 & 0x80) != 0) {
    const u8 sustainLevel = (adsr2 & 0xe0) >> 5;
    if (sustainLevel == 7) {
      envelope.decay = envelope.sustain;
      if (envelope.sustain != -1.0) {
        envelope.sustainLevel = 0.0;
      }
    } else if (envelope.sustain != -1.0) {
      const double dbAtSustainStart = ampToDb(envelope.sustainLevel);
      const double decayTimeRate = dbAtSustainStart / 100.0;
      envelope.decay = (envelope.decay * decayTimeRate) + (envelope.sustain * (1.0 - decayTimeRate));
      envelope.sustainLevel = 0.0;
    }
  }

  return Envelope{
      .attack = microsFromSeconds(envelope.attack),
      .decay = microsFromSeconds(envelope.decay),
      .sustain = permilleFromLevel(envelope.sustainLevel),
      .release = microsFromSeconds(envelope.release),
      .attackSeconds = preciseSeconds(envelope.attack),
      .holdSeconds = 0.0,
      .decaySeconds = preciseSeconds(envelope.decay),
      .releaseSeconds = preciseSeconds(envelope.release),
      .sustainAmplitude = std::clamp(envelope.sustainLevel, 0.0, 1.0),
  };
}

double snesDspGainEnvelopeSeconds(u8 gain, s16 envelopeFrom, s16 envelopeTo) {
  return emulateGainEnvelope(gain, envelopeFrom, envelopeTo).seconds;
}

u32 snesDspGainEnvelopeMicros(u8 gain, s16 envelopeFrom, s16 envelopeTo) {
  return microsFromSeconds(snesDspGainEnvelopeSeconds(gain, envelopeFrom, envelopeTo));
}

}  // namespace vgmtrans::core
