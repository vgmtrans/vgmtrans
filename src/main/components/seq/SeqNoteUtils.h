/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <cstdint>

#include "SeqEvent.h"
#include "VGMSeq.h"

namespace SeqNoteUtils {

inline int rawNoteKey(const SeqEvent* event) {
  if (!event) {
    return -1;
  }
  if (const auto* noteOn = dynamic_cast<const NoteOnSeqEvent*>(event)) {
    return noteOn->absKey;
  }
  if (const auto* durNote = dynamic_cast<const DurNoteSeqEvent*>(event)) {
    return durNote->absKey;
  }
  return -1;
}

inline int rawNoteVelocity(const SeqEvent* event) {
  if (!event) {
    return -1;
  }
  if (const auto* noteOn = dynamic_cast<const NoteOnSeqEvent*>(event)) {
    return noteOn->vel;
  }
  if (const auto* durNote = dynamic_cast<const DurNoteSeqEvent*>(event)) {
    return durNote->vel;
  }
  return -1;
}

inline int transposedNoteKeyAtTick(const VGMSeq* seq, const SeqEvent* event, uint32_t tick) {
  int noteKey = rawNoteKey(event);
  if (noteKey < 0) {
    return -1;
  }

  if (seq) {
    noteKey += seq->transposeTimeline().totalTransposeAtTick(event ? event->parentTrack : nullptr, tick);
  }
  if (noteKey < 0 || noteKey >= 128) {
    return -1;
  }
  return noteKey;
}

inline int transposedNoteKeyForTimedEvent(const VGMSeq* seq, const SeqTimedEvent* timedEvent) {
  if (!timedEvent || !timedEvent->event) {
    return -1;
  }

  int noteKey = rawNoteKey(timedEvent->event);
  if (noteKey < 0) {
    return -1;
  }

  if (seq) {
    noteKey += seq->transposeTimeline().totalTransposeForTimedEvent(timedEvent);
  }
  if (noteKey < 0 || noteKey >= 128) {
    return -1;
  }
  return noteKey;
}

}  // namespace SeqNoteUtils

