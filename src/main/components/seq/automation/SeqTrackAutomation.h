/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Types.h"

#include "SeqTrack.h"
#include "SeqMidiAutomation.h"
#include <utility>

// SeqTrack helper methods that connect automation objects to this track's MIDI writers.

// Advance pitch automation by one tick and emit MIDI bend if the bend changed.
template <typename PitchType>
SeqMotionTick<PitchType> SeqTrack::advancePitchBendAutomation(
    SeqPitchBendAutomation<PitchType>& automation) {
  return automation.tickBend([this](s16 bend) { addPitchBendNoItem(bend); });
}

// Start a pitch slide and emit any bend range or initial bend update it requires.
template <typename PitchType>
bool SeqTrack::beginPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation,
                                        const SeqMotionPlan<PitchType>& motion,
                                        u16 rangeCents, u16 defaultRangeCents,
                                        bool applyInitialBend) {
  return automation.beginSlide(
      motion,
      rangeCents,
      defaultRangeCents,
      applyInitialBend,
      [this](u16 newRangeCents) { addPitchBendRangeNoItem(newRangeCents); },
      [this](s16 bend) { addPitchBendNoItem(bend); });
}

// Set pitch bend range and emit any bend update required by the new range.
template <typename PitchType>
bool SeqTrack::setPitchBendAutomationRange(SeqPitchBendAutomation<PitchType>& automation, u16 cents) {
  return automation.setRange(cents,
                             [this](u16 newRangeCents) {
                               addPitchBendRangeNoItem(newRangeCents);
                             },
                             [this](s16 bend) { addPitchBendNoItem(bend); });
}

// Emit a raw MIDI bend if it differs from the automation's cached bend.
template <typename PitchType>
bool SeqTrack::setPitchBendAutomationBend(SeqPitchBendAutomation<PitchType>& automation, s16 bend) {
  return automation.setBend(bend, [this](s16 newBend) { addPitchBendNoItem(newBend); });
}

// Emit bend for the automation's current source-space pitch if it changed.
template <typename PitchType>
bool SeqTrack::applyPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation) {
  return automation.applyCurrentBend([this](s16 bend) { addPitchBendNoItem(bend); });
}

// Restore the default bend range and emit centered bend if either value changed.
template <typename PitchType>
void SeqTrack::resetPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation, u16 defaultRangeCents) {
  automation.resetRangeAndBend(defaultRangeCents,
                               [this](u16 newRangeCents) {
                                 addPitchBendRangeNoItem(newRangeCents);
                               },
                               [this](s16 bend) { addPitchBendNoItem(bend); });
}

// Non-template helper implementations are defined in SeqTrack.cpp

// Advance an LFO fade by one tick and emit converted vibrato depth if it changed.
template <typename ConvertDepth>
SeqMotionTick<s32> SeqTrack::advanceVibratoDepthFade(SeqSynthLfoAutomation& automation,
                                                         u8 fractionalBits,
                                                         ConvertDepth&& convertDepth) {
  return automation.tickFadeToDepth(fractionalBits,
                                    std::forward<ConvertDepth>(convertDepth),
                                    [this](u8 depth) {
                                      addVibratoDepthNoItem(depth);
                                    });
}

template <typename ConvertDepth>
SeqMotionTick<s32> SeqTrack::advanceTremoloDepthFade(SeqSynthLfoAutomation& automation,
                                                         u8 fractionalBits,
                                                         ConvertDepth&& convertDepth) {
  return automation.tickFadeToDepth(fractionalBits,
                                    std::forward<ConvertDepth>(convertDepth),
                                    [this](u8 depth) {
                                      addTremoloDepthNoItem(depth);
                                    });
}
