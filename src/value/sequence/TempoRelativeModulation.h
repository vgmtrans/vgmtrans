/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

namespace vgmtrans::core {

struct PerformanceSequence;

// Resolves sequence-clocked LFO rate and delay against the song-wide tempo
// timeline. Source cycles/ticks remain on the events for exact simulation;
// derived Hz/ms values and tempo-change events serve MIDI synth modulators.
void resolveTempoRelativeModulation(PerformanceSequence& performance);

}  // namespace vgmtrans::core
