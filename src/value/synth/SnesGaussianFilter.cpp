/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SnesGaussianFilter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vgmtrans::core {

namespace {

// Minimum-phase FIR fitted to the coherent, phase-aligned mean of all 256
// S-DSP Gaussian interpolation phases. Its magnitude differs from that target
// by at most 0.23 dB while avoiding the pre-ringing of a linear-phase fit.
constexpr std::array<double, 31> kGaussianResponse = {
    0.53071395079826311,     0.41679409076156287,    0.049265161472004583,    0.0051300353439470781,
    -0.0032131516711479827,  0.0023961336006554229,  -0.0018340753790727442,  0.0014432113381850119,
    -0.0011637632177242585,  0.00095802980262378753, -0.00080248195443904892, 0.00068211732941549726,
    -0.00058709234505266103, 0.00051076050506215331, -0.00044851329455110556, 0.00039707782382184491,
    -0.00035407940628211481, 0.00031776176909220741, -0.00028680357437971184, 0.00026019531158914892,
    -0.00023715495219691591, 0.00021706905099094063, -0.00019945089671765824, 0.00018391030180238321,
    -0.00017013147543763356, 0.00015785659997757701, -0.00014687349032872012, 0.00013700621586620448,
    -0.00012810789875436336, 0.00012005512967081719, -0.00011274359844562693,
};

}  // namespace

void applySnesGaussianResponseFilter(DecodedSample& sample) {
  const size_t channels = std::max<size_t>(sample.channels, 1);
  const size_t frames = sample.pcm.size() / channels;
  if (frames == 0) {
    return;
  }

  const size_t loopBegin = std::min<size_t>(sample.loop.start, frames);
  const size_t loopLength =
      sample.loop.enabled && loopBegin < frames
          ? std::min<size_t>(sample.loop.length == 0 ? frames - loopBegin : sample.loop.length, frames - loopBegin)
          : 0;
  const size_t loopEnd = loopBegin + loopLength;
  const auto source = sample.pcm;

  for (size_t frame = 0; frame < frames; ++frame) {
    const bool inLoop = loopLength != 0 && frame >= loopBegin && frame < loopEnd;
    for (size_t channel = 0; channel < channels; ++channel) {
      double filtered = 0.0;
      for (size_t delay = 0; delay < kGaussianResponse.size(); ++delay) {
        size_t sourceFrame = frame >= delay ? frame - delay : 0;
        if (inLoop) {
          sourceFrame = loopBegin + (frame - loopBegin + loopLength - delay % loopLength) % loopLength;
        }
        filtered += kGaussianResponse[delay] * source[sourceFrame * channels + channel];
      }
      sample.pcm[frame * channels + channel] =
          static_cast<s16>(std::clamp(std::lround(filtered), static_cast<long>(std::numeric_limits<s16>::min()),
                                      static_cast<long>(std::numeric_limits<s16>::max())));
    }
  }
}

}  // namespace vgmtrans::core
