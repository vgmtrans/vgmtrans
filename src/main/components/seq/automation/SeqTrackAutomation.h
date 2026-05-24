/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "SeqTrack.h"
#include "SeqMidiAutomation.h"
#include <utility>

// SeqTrack helper methods that connect automation objects to this track's MIDI writers.

// Advance pitch automation by one tick and emit MIDI bend if the bend changed.
template <typename PitchType>
SeqMotionTick<PitchType> SeqTrack::advancePitchBendAutomation(
    SeqPitchBendAutomation<PitchType>& automation) {
  return automation.tickBend([this](int16_t bend) { addPitchBendNoItem(bend); });
}

// Start a pitch slide and emit any bend range or initial bend update it requires.
template <typename PitchType>
bool SeqTrack::beginPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation,
                                        const SeqMotionPlan<PitchType>& motion,
                                        uint16_t rangeCents, uint16_t defaultRangeCents,
                                        bool applyInitialBend) {
  return automation.beginSlide(
      motion,
      rangeCents,
      defaultRangeCents,
      applyInitialBend,
      [this](uint16_t newRangeCents) { addPitchBendRangeNoItem(newRangeCents); },
      [this](int16_t bend) { addPitchBendNoItem(bend); });
}

// Set pitch bend range and emit any bend update required by the new range.
template <typename PitchType>
bool SeqTrack::setPitchBendAutomationRange(SeqPitchBendAutomation<PitchType>& automation, uint16_t cents) {
  return automation.setRange(cents,
                             [this](uint16_t newRangeCents) {
                               addPitchBendRangeNoItem(newRangeCents);
                             },
                             [this](int16_t bend) { addPitchBendNoItem(bend); });
}

// Emit a raw MIDI bend if it differs from the automation's cached bend.
template <typename PitchType>
bool SeqTrack::setPitchBendAutomationBend(SeqPitchBendAutomation<PitchType>& automation, int16_t bend) {
  return automation.setBend(bend, [this](int16_t newBend) { addPitchBendNoItem(newBend); });
}

// Emit bend for the automation's current source-space pitch if it changed.
template <typename PitchType>
bool SeqTrack::applyPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation) {
  return automation.applyCurrentBend([this](int16_t bend) { addPitchBendNoItem(bend); });
}

// Restore the default bend range and emit centered bend if either value changed.
template <typename PitchType>
void SeqTrack::resetPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation, uint16_t defaultRangeCents) {
  automation.resetRangeAndBend(defaultRangeCents,
                               [this](uint16_t newRangeCents) {
                                 addPitchBendRangeNoItem(newRangeCents);
                               },
                               [this](int16_t bend) { addPitchBendNoItem(bend); });
}

// Non-template helper implementations are defined in SeqTrack.cpp

// Advance an LFO fade by one tick and emit converted vibrato depth if it changed.
template <typename ConvertDepth>
SeqMotionTick<int32_t> SeqTrack::advanceVibratoDepthFade(SeqSynthLfoAutomation& automation,
                                                         uint8_t fractionalBits,
                                                         ConvertDepth&& convertDepth) {
  return automation.tickFadeToDepth(fractionalBits,
                                    std::forward<ConvertDepth>(convertDepth),
                                    [this](uint8_t depth) {
                                      addVibratoDepthNoItem(depth);
                                    });
}

template <typename ConvertDepth>
SeqMotionTick<int32_t> SeqTrack::advanceTremoloDepthFade(SeqSynthLfoAutomation& automation,
                                                         uint8_t fractionalBits,
                                                         ConvertDepth&& convertDepth) {
  return automation.tickFadeToDepth(fractionalBits,
                                    std::forward<ConvertDepth>(convertDepth),
                                    [this](uint8_t depth) {
                                      addTremoloDepthNoItem(depth);
                                    });
}
