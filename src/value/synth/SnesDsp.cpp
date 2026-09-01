/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SnesDsp.h"

#include "value/synth/SynthMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace vgmtrans::core {

namespace {

constexpr double kSampleRate = kSnesDspSampleRate;
constexpr std::array<unsigned, 32> kCounterRates{
    0x7800, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64,     48,   40,   32,   24,   20,  16,  12,  10,  8,   6,   5,   4,   3,   2,  1};

struct GainEnvelope {
  s16 envelope = 0;
  double seconds = 0.0;
  double physicalSeconds = 0.0;
};

struct SnesEnvelopeSeconds {
  double attack = 0.0;
  double decay = 0.0;
  double sustainLevel = 0.0;
  double sustain = 0.0;
  double release = 0.0;
};

[[nodiscard]] double ampToDb(double amp) {
  constexpr double maxAttenuationDb = 100.0;
  if (amp == 0.0) {
    return maxAttenuationDb;
  }
  return std::min(-20.0 * std::log10(amp), maxAttenuationDb);
}

[[nodiscard]] double envelopeSeconds(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) {
    return std::numeric_limits<double>::infinity();
  }
  return std::max(0.0, seconds);
}

[[nodiscard]] double adsrAttackSeconds(u8 rate) {
  return rate < 15 ? kCounterRates[rate * 2 + 1] * 64 / kSampleRate : 2 / kSampleRate;
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

  const bool stoppedCounter = mode >= 4 && ticks != 0 && rate == 0;
  const double physicalSeconds =
      stoppedCounter ? std::numeric_limits<double>::infinity() : (ticks * kCounterRates[rate]) / kSampleRate;
  double seconds = 0.0;
  if (stoppedCounter) {
    seconds = std::numeric_limits<double>::infinity();
  } else if (mode < 4) {
    seconds = 0.0;
  } else if (mode == 4) {
    const u32 fullSamples = (0x800 / 0x20) * kCounterRates[rate];
    seconds = linearAmplitudeFadeToDbEnvelopeSeconds(fullSamples / kSampleRate);
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
      .physicalSeconds = physicalSeconds,
  };
}

[[nodiscard]] SnesEnvelopeSeconds convertSnesAdsr(u8 adsr1, u8 adsr2, u8 gain, u16 envelopeFrom) {
  const bool adsrEnabled = (adsr1 & 0x80) != 0;

  if (adsrEnabled) {
    const u8 attackRate = adsr1 & 0x0f;
    const u8 decayRate = (adsr1 & 0x70) >> 4;
    const u8 sustainLevel = (adsr2 & 0xe0) >> 5;
    const u8 sustainRate = adsr2 & 0x1f;

    const double attackSeconds = adsrAttackSeconds(attackRate);

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
        .release = linearAmplitudeFadeToDbEnvelopeSeconds(releaseSamples / kSampleRate),
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
        .release = linearAmplitudeFadeToDbEnvelopeSeconds(releaseSamples / kSampleRate),
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
        .release = linearAmplitudeFadeToDbEnvelopeSeconds(releaseSamples / kSampleRate),
    };
  }

  const u32 releaseSamples = (envelopeFrom + 7) / 8;
  return SnesEnvelopeSeconds{
      .attack = 0.0,
      .decay = gainEnvelope.seconds,
      .sustainLevel = 0.0,
      .sustain = 0.0,
      .release = linearAmplitudeFadeToDbEnvelopeSeconds(releaseSamples / kSampleRate),
  };
}

}  // namespace

Envelope snesDspEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  const auto envelope = convertSnesAdsr(adsr1, adsr2, gain, 0x7ff);
  const bool adsrEnabled = (adsr1 & 0x80) != 0;

  return Envelope{
      .attackSeconds = envelopeSeconds(envelope.attack),
      .holdSeconds = 0.0,
      .decaySeconds = envelopeSeconds(envelope.decay),
      .secondDecaySeconds = adsrEnabled ? std::optional{envelopeSeconds(envelope.sustain)} : std::nullopt,
      .releaseSeconds = envelopeSeconds(envelope.release),
      .sustainAmplitude = std::clamp(envelope.sustainLevel, 0.0, 1.0),
  };
}

double snesDspAdsrAttackSeconds(u8 attackRate) {
  return adsrAttackSeconds(attackRate & 0x0f);
}

double snesDspAdsrDecaySeconds(u8 decayRate) {
  const u8 rate = static_cast<u8>(0x10 | ((decayRate & 0x07) << 1));
  return emulateGainEnvelope(static_cast<u8>(0xa0 | rate), 0x7ff, 0).seconds;
}

double snesDspAdsrSustainSeconds(u8 sustainRate) {
  const u8 rate = sustainRate & 0x1f;
  return rate == 0 ? std::numeric_limits<double>::infinity()
                   : emulateGainEnvelope(static_cast<u8>(0xa0 | rate), 0x7ff, 0).seconds;
}

double snesDspGainEnvelopeSeconds(u8 gain, s16 envelopeFrom, s16 envelopeTo) {
  return emulateGainEnvelope(gain, envelopeFrom, envelopeTo).seconds;
}

double snesDspGainPhysicalSeconds(u8 gain, s16 envelopeFrom, s16 envelopeTo) {
  return emulateGainEnvelope(gain, envelopeFrom, envelopeTo).physicalSeconds;
}

s16 snesDspGainEnvelopeValue(u8 gain, s16 envelopeFrom, double elapsedSeconds) {
  s16 envelope = std::clamp<s16>(envelopeFrom, 0, 0x7ff);
  const u8 mode = gain >> 5;
  if (mode < 4) {
    return static_cast<s16>(gain * 0x10);
  }
  if (elapsedSeconds <= 0.0) {
    return envelope;
  }
  if (!std::isfinite(elapsedSeconds)) {
    return mode < 6 ? 0 : 0x7ff;
  }

  const u8 rate = gain & 0x1f;
  if (rate == 0) {
    return envelope;
  }
  const auto updates =
      static_cast<u64>(std::floor(elapsedSeconds * kSampleRate / static_cast<double>(kCounterRates[rate])));
  for (u64 i = 0; i < updates; ++i) {
    if ((mode < 6 && envelope == 0) || (mode >= 6 && envelope == 0x7ff)) {
      break;
    }
    if (mode == 4) {
      envelope = static_cast<s16>(std::max(0, envelope - 0x20));
    } else if (mode == 5) {
      if (envelope != 0) {
        --envelope;
        envelope -= envelope >> 8;
      }
    } else {
      envelope = static_cast<s16>(std::min<int>(0x7ff, envelope + (mode == 7 && envelope > 0x600 ? 0x08 : 0x20)));
    }
  }
  return envelope;
}

u32 snesDspNoisePeriodSamples(u8 rate) {
  rate &= 0x1f;
  return rate == 0 ? 0 : kCounterRates[rate];
}

}  // namespace vgmtrans::core
