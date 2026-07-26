/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/PitchTransitionMidiLowering.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace vgmtrans::core {

namespace {

struct PortamentoSegment {
  PerformanceEventHeader header;
  u64 startTick = 0;
  u64 endTick = 0;
  double key = 0.0;
  // Pitch bend is channel-relative to the MIDI note that actually started the
  // voice. A segment entered without a new attack inherits that note's key.
  double bendBaseKey = 0.0;
  bool extendsPrevious = false;
};

struct NoteSpan {
  NotePerformanceEvent source;
  u64 endTick = 0;
  // A boundary pitch bend can carry the preceding MIDI voice into this
  // logical note instead of retriggering its attack.
  bool continuesPreviousVoice = false;
  double bendBaseKey = 0.0;
  // Empty unless native portamento must replace this source note.
  std::vector<PortamentoSegment> portamentoSegments;
};

[[nodiscard]] u64 noteEnd(const NotePerformanceEvent& note) {
  return note.header.tick > std::numeric_limits<u64>::max() - note.durationTicks
             ? std::numeric_limits<u64>::max()
             : note.header.tick + note.durationTicks;
}

[[nodiscard]] NoteSpan* findNote(std::vector<NoteSpan>& notes, PerformanceNoteId id) {
  const auto found = std::ranges::find_if(notes, [id](const NoteSpan& note) { return note.source.note == id; });
  return found == notes.end() ? nullptr : &*found;
}

[[nodiscard]] const NoteSpan* findNote(const std::vector<NoteSpan>& notes, PerformanceNoteId id) {
  const auto found = std::ranges::find_if(notes, [id](const NoteSpan& note) { return note.source.note == id; });
  return found == notes.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<NoteSpan> collectNotes(const PerformanceTrack& track) {
  std::vector<NoteSpan> notes;
  for (const auto& event : track.events) {
    const auto* source = std::get_if<NotePerformanceEvent>(&event);
    if (source == nullptr || !source->note.valid()) {
      continue;
    }
    if (auto* note = findNote(notes, source->note)) {
      note->endTick = std::max(note->endTick, noteEnd(*source));
    } else {
      notes.push_back(NoteSpan{
          .source = *source,
          .endTick = noteEnd(*source),
          .bendBaseKey = source->key,
      });
    }
  }
  std::ranges::stable_sort(notes, [](const NoteSpan& lhs, const NoteSpan& rhs) {
    return std::tie(lhs.source.header.tick, lhs.source.header.sequence) <
           std::tie(rhs.source.header.tick, rhs.source.header.sequence);
  });
  return notes;
}

[[nodiscard]] double bendBaseKeyAt(const NoteSpan& note, u64 tick) {
  double key = note.bendBaseKey;
  for (const auto& segment : note.portamentoSegments) {
    if (segment.startTick > tick) {
      break;
    }
    key = segment.bendBaseKey;
  }
  return key;
}

[[nodiscard]] std::optional<double> pitchBendAt(const std::vector<PerformanceEvent>& events, u64 beginTick,
                                                u64 endTick) {
  const PitchBendPerformanceEvent* latest = nullptr;
  for (const auto& event : events) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    if (bend == nullptr || bend->header.tick < beginTick || bend->header.tick > endTick) {
      continue;
    }
    if (latest == nullptr ||
        std::tie(latest->header.tick, latest->header.sequence) < std::tie(bend->header.tick, bend->header.sequence)) {
      latest = bend;
    }
  }
  return latest == nullptr ? std::nullopt : std::optional{latest->semitones};
}

[[nodiscard]] std::optional<double> establishedPitchBend(const std::vector<PerformanceEvent>& events,
                                                         const NoteSpan& note, const PitchTransitionIntent& transition,
                                                         u64 startTick) {
  const auto bend = pitchBendAt(events, note.source.header.tick, startTick);
  // Pitch bends may already have passed through the source's finite bend
  // register, so allow a small quantization difference from the exact key.
  if (!bend || std::abs(bendBaseKeyAt(note, startTick) + *bend - transition.startKey) >= 0.02) {
    return std::nullopt;
  }
  return bend;
}

[[nodiscard]] PerformanceEventHeader atTick(const PerformanceEventHeader& origin, u64 tick, u64& nextSequence) {
  auto header = origin;
  header.tick = tick;
  header.sequence = nextSequence++;
  return header;
}

[[nodiscard]] PerformanceEventHeader atAutomationTick(const PerformanceAutomation& automation, u64 tick,
                                                      u64& nextSequence) {
  auto header = atTick(automation.header, tick, nextSequence);
  header.automation = automation.id;
  return header;
}

void addWarning(PerformanceSequence& performance, const PerformanceAutomation& automation, std::string message) {
  performance.diagnostics.push_back(Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .annotation = automation.header.sourceAnnotation.valid()
                        ? std::optional<SourceAnnotationId>{automation.header.sourceAnnotation}
                        : std::nullopt,
  });
}

[[nodiscard]] double physicalDurationMilliseconds(const PitchTransitionIntent& transition,
                                                  const PerformanceTempoMap& tempos, u64 startTick) {
  return std::visit(
      [&](const auto& physical) {
        using Physical = std::decay_t<decltype(physical)>;
        if constexpr (std::is_same_v<Physical, TempoRelativePitchSlideTiming>) {
          return tempos.durationMilliseconds(startTick, transition.timing.timelineTicks);
        } else if constexpr (std::is_same_v<Physical, FixedDurationPitchSlideTiming>) {
          return std::max(0.0, physical.milliseconds);
        } else {
          if (physical.semitonesPerSecond <= 0.0) {
            return 0.0;
          }
          return std::abs(transition.targetKey - transition.startKey) / physical.semitonesPerSecond * 1000.0;
        }
      },
      transition.timing.physical);
}

[[nodiscard]] bool affectsNote(const PerformanceAutomation& automation, const PitchTransitionIntent& transition,
                               const NoteSpan& anchor, const NoteSpan& note) {
  if (note.source.note == anchor.source.note) {
    return true;
  }
  if (!transition.continuesAcrossNotes || note.source.lane != transition.lane ||
      note.source.header.tick < anchor.source.header.tick) {
    return false;
  }
  return note.endTick > automation.realization.startTick && note.source.header.tick <= automation.realization.endTick;
}

[[nodiscard]] u16 pitchRangeCents(const PerformanceAutomation& automation, const PitchTransitionIntent& transition,
                                  const NoteSpan& note) {
  const u64 beginTick = std::max(note.source.header.tick, automation.realization.startTick);
  const u64 endTick = std::min(note.endTick, automation.realization.endTick);
  if (endTick < beginTick) {
    return 200;
  }

  double maximum = 0.0;
  for (u64 tick = beginTick;; ++tick) {
    const u64 elapsed = tick - automation.realization.startTick;
    maximum = std::max(
        maximum, std::abs(pitchTransitionValueAt(
                              transition, static_cast<u32>(std::min<u64>(elapsed, std::numeric_limits<u32>::max()))) -
                          bendBaseKeyAt(note, tick)));
    if (tick == endTick) {
      break;
    }
  }

  const double cents = std::ceil(maximum * 100.0);
  // MIDI's RPN stores 0-127 semitones plus 0-99 fine cents.
  return static_cast<u16>(std::clamp<double>(std::max(200.0, cents), 200.0, 12'799.0));
}

[[nodiscard]] bool appendPitchBends(std::vector<PerformanceEvent>& events, const PerformanceAutomation& automation,
                                    const PitchTransitionIntent& transition, const NoteSpan& note, u64& nextSequence) {
  const u64 startTick = std::max(note.source.header.tick, automation.realization.startTick);
  const u64 endTick = std::min(note.endTick, automation.realization.endTick);
  if (startTick >= note.endTick || endTick < startTick) {
    return false;
  }

  // A delayed slide may begin away from the note's nominal key.
  const double noteBaseKey = bendBaseKeyAt(note, note.source.header.tick);
  if (startTick > note.source.header.tick && std::abs(transition.startKey - noteBaseKey) > 0.000001 &&
      !establishedPitchBend(events, note, transition, startTick)) {
    events.emplace_back(PitchBendPerformanceEvent{
        .header = atAutomationTick(automation, note.source.header.tick, nextSequence),
        .semitones = transition.startKey - noteBaseKey,
    });
  }

  for (u64 tick = startTick;; ++tick) {
    const u64 elapsed = tick - automation.realization.startTick;
    events.emplace_back(PitchBendPerformanceEvent{
        .header = atAutomationTick(automation, tick, nextSequence),
        .semitones = pitchTransitionValueAt(transition,
                                            static_cast<u32>(std::min<u64>(elapsed, std::numeric_limits<u32>::max()))) -
                     bendBaseKeyAt(note, tick),
    });
    if (tick == endTick) {
      break;
    }
  }
  return true;
}

[[nodiscard]] bool transitionOwnsAttackAtEnd(const PerformanceAutomation& automation,
                                             const PitchTransitionIntent& transition, const NoteSpan& anchor,
                                             const NotePerformanceEvent& attack, const std::vector<NoteSpan>& notes) {
  if (automation.realization.endReason != PerformanceAutomationEndReason::Completed ||
      !transition.continuesAcrossNotes || attack.header.tick != automation.realization.endTick) {
    return false;
  }
  const auto* note = findNote(notes, attack.note);
  return note != nullptr && affectsNote(automation, transition, anchor, *note);
}

[[nodiscard]] std::optional<u64> nextIndependentAttack(const std::vector<PerformanceEvent>& events,
                                                       const std::vector<NoteSpan>& notes,
                                                       const PerformanceAutomation& automation,
                                                       const PitchTransitionIntent& transition,
                                                       const NoteSpan& anchor) {
  std::optional<u64> next;
  for (const auto& event : events) {
    const auto* note = std::get_if<NotePerformanceEvent>(&event);
    if (note == nullptr || note->extendsPrevious || note->header.tick < automation.realization.endTick ||
        transitionOwnsAttackAtEnd(automation, transition, anchor, *note, notes)) {
      continue;
    }
    next = next ? std::min(*next, note->header.tick) : note->header.tick;
  }
  return next;
}

[[nodiscard]] bool pitchBendTakenOver(const std::vector<PerformanceEvent>& events,
                                      const PerformanceAutomation& automation, u64 resetTick) {
  return std::ranges::any_of(events, [&](const PerformanceEvent& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    return bend != nullptr && bend->header.tick >= automation.realization.endTick && bend->header.tick <= resetTick &&
           (!bend->header.automation || *bend->header.automation != automation.id);
  });
}

void lowerPitchBends(PerformanceSequence& performance, std::vector<PerformanceEvent>& events,
                     const std::vector<NoteSpan>& notes, const std::vector<const PerformanceAutomation*>& transitions,
                     u64& nextSequence) {
  // Bend range is channel state, so choose one range that covers every linked
  // transition affecting a note.
  for (const auto& note : notes) {
    const PerformanceAutomation* origin = nullptr;
    u16 rangeCents = 200;
    for (const auto* automation : transitions) {
      const auto& transition = *pitchTransitionIntent(*automation);
      const auto* anchor = findNote(notes, transition.note);
      if (anchor == nullptr || !affectsNote(*automation, transition, *anchor, note)) {
        continue;
      }
      origin = origin == nullptr ? automation : origin;
      rangeCents = std::max(rangeCents, pitchRangeCents(*automation, transition, note));
    }
    if (origin != nullptr) {
      events.emplace_back(PitchBendRangePerformanceEvent{
          .header = atTick(origin->header, note.source.header.tick, nextSequence),
          .cents = rangeCents,
      });
    }
  }

  // Emit every transition first so a later pitch-bend writer can take
  // ownership without an intervening reset.
  std::vector<const PerformanceAutomation*> renderedTransitions;
  for (const auto* automation : transitions) {
    const auto& transition = *pitchTransitionIntent(*automation);
    const auto* anchor = findNote(notes, transition.note);
    if (anchor == nullptr) {
      addWarning(performance, *automation, "Pitch transition did not reference a rendered note");
      continue;
    }

    bool rendered = false;
    for (const auto& note : notes) {
      if (affectsNote(*automation, transition, *anchor, note) &&
          appendPitchBends(events, *automation, transition, note, nextSequence)) {
        rendered = true;
      }
    }
    if (rendered) {
      renderedTransitions.push_back(automation);
    }
  }

  // Retain the terminal bend through note-off and the synth's release phase.
  // Recenter only for the next attack that would otherwise inherit it.
  std::vector<std::pair<const PerformanceAutomation*, u64>> resets;
  for (const auto* automation : renderedTransitions) {
    const auto& transition = *pitchTransitionIntent(*automation);
    const auto* anchor = findNote(notes, transition.note);
    const auto resetTick = nextIndependentAttack(events, notes, *automation, transition, *anchor);
    if (!resetTick || pitchBendTakenOver(events, *automation, *resetTick)) {
      continue;
    }
    resets.emplace_back(automation, *resetTick);
  }
  for (const auto& [automation, resetTick] : resets) {
    events.emplace_back(PitchBendPerformanceEvent{
        .header = atAutomationTick(*automation, resetTick, nextSequence),
        .semitones = 0.0,
    });
  }
}

void beginPortamentoRewrite(NoteSpan& note) {
  if (!note.portamentoSegments.empty()) {
    return;
  }
  note.portamentoSegments.push_back(PortamentoSegment{
      .header = note.source.header,
      .startTick = note.source.header.tick,
      .endTick = note.endTick,
      .key = note.source.key,
      .bendBaseKey = note.bendBaseKey,
  });
}

void splitForPortamento(NoteSpan& note, const PerformanceAutomation& automation,
                        const PitchTransitionIntent& transition, bool sourceEstablishesStart) {
  const u64 startTick = automation.realization.startTick;
  if (startTick <= note.source.header.tick) {
    note.portamentoSegments.front().key = transition.targetKey;
    note.portamentoSegments.front().bendBaseKey = transition.targetKey;
    note.portamentoSegments.front().extendsPrevious = false;
    return;
  }

  const u64 clampedStart = std::min(startTick, note.endTick);
  std::vector<PortamentoSegment> segments;
  segments.reserve(note.portamentoSegments.size() + 1);
  for (auto segment : note.portamentoSegments) {
    if (segment.startTick >= clampedStart) {
      continue;
    }
    if (segment.endTick > clampedStart) {
      const u32 overlap = transition.nativePortamento.overlapTicks;
      segment.endTick = std::min(note.endTick, clampedStart > std::numeric_limits<u64>::max() - overlap
                                                   ? std::numeric_limits<u64>::max()
                                                   : clampedStart + overlap);
      if (!sourceEstablishesStart) {
        segment.key = transition.startKey;
        segment.bendBaseKey = transition.startKey;
      }
    }
    if (segment.endTick > segment.startTick) {
      segments.push_back(std::move(segment));
    }
  }
  if (clampedStart < note.endTick) {
    segments.push_back(PortamentoSegment{
        .header = automation.header,
        .startTick = clampedStart,
        .endTick = note.endTick,
        .key = transition.targetKey,
        .bendBaseKey = transition.targetKey,
    });
  }
  note.portamentoSegments = std::move(segments);
}

void linkPitchBendVoices(std::vector<NoteSpan>& notes, const std::vector<const PerformanceAutomation*>& transitions) {
  for (const auto* automation : transitions) {
    const auto& transition = *pitchTransitionIntent(*automation);
    if (!transition.previousNote) {
      continue;
    }

    auto* note = findNote(notes, transition.note);
    auto* previous = findNote(notes, *transition.previousNote);
    if (note == nullptr || previous == nullptr || note == previous ||
        automation->realization.startTick > note->source.header.tick ||
        std::tie(previous->source.header.tick, previous->source.header.sequence) >=
            std::tie(note->source.header.tick, note->source.header.sequence) ||
        previous->source.lane != note->source.lane) {
      continue;
    }

    const double baseKey = bendBaseKeyAt(*previous, automation->realization.startTick);
    note->continuesPreviousVoice = true;
    note->bendBaseKey = baseKey;
    if (!note->portamentoSegments.empty()) {
      auto& first = note->portamentoSegments.front();
      first.bendBaseKey = baseKey;
      first.extendsPrevious = true;
    }
  }
}

void lowerPortamento(PerformanceSequence& performance, std::vector<PerformanceEvent>& events,
                     const std::vector<PerformanceEvent>& sourceEvents, std::vector<NoteSpan>& notes,
                     const std::vector<const PerformanceAutomation*>& transitions, const PerformanceTempoMap& tempos,
                     u64& nextSequence) {
  for (const auto* automation : transitions) {
    const auto& transition = *pitchTransitionIntent(*automation);
    auto* note = findNote(notes, transition.note);
    if (note == nullptr) {
      addWarning(performance, *automation, "Pitch transition did not reference a rendered note");
      continue;
    }
    if (std::holds_alternative<SampledAutomationCurve>(transition.curve)) {
      addWarning(performance, *automation,
                 "Native MIDI portamento cannot preserve the transition's exact sampled pitch curve");
    }

    const u64 startTick = automation->realization.startTick;
    if (startTick >= note->endTick) {
      continue;
    }
    const auto sourceBend = establishedPitchBend(sourceEvents, *note, transition, startTick);
    beginPortamentoRewrite(*note);
    if (startTick <= note->source.header.tick && transition.previousNote) {
      if (auto* previous = findNote(notes, *transition.previousNote)) {
        beginPortamentoRewrite(*previous);
        auto& segment = previous->portamentoSegments.back();
        const u32 overlap = transition.nativePortamento.overlapTicks;
        const u64 overlapEnd = startTick > std::numeric_limits<u64>::max() - overlap ? std::numeric_limits<u64>::max()
                                                                                     : startTick + overlap;
        segment.endTick = std::max(segment.endTick, overlapEnd);
      }
    }
    splitForPortamento(*note, *automation, transition, sourceBend.has_value());

    if (sourceBend && std::abs(*sourceBend) > 0.000001) {
      events.emplace_back(PitchBendPerformanceEvent{
          .header = atTick(automation->header, startTick, nextSequence),
          .semitones = 0.0,
      });
    }

    if (transition.nativePortamento.useCurrentTiming) {
      events.emplace_back(PortamentoControlPerformanceEvent{
          .header = atTick(automation->header, startTick, nextSequence),
          .previousKey = transition.startKey,
      });
    } else {
      events.emplace_back(PortamentoPerformanceEvent{
          .header = atTick(automation->header, startTick, nextSequence),
          .timeMilliseconds = physicalDurationMilliseconds(transition, tempos, startTick),
          .previousKey = transition.startKey,
      });
    }
    if (transition.nativePortamento.restoreTimeMilliseconds) {
      events.emplace_back(PortamentoPerformanceEvent{
          .header = atTick(automation->header, note->endTick, nextSequence),
          .timeMilliseconds = *transition.nativePortamento.restoreTimeMilliseconds,
      });
    }
  }
}

void appendSourceEvents(std::vector<PerformanceEvent>& events, const PerformanceTrack& track,
                        const std::vector<NoteSpan>& notes, bool renderPortamentoSettings, u64& nextSequence) {
  for (const auto& event : track.events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event); note != nullptr && note->note.valid()) {
      const auto* span = findNote(notes, note->note);
      if (span != nullptr && !span->portamentoSegments.empty()) {
        continue;
      }
      if (span != nullptr && span->continuesPreviousVoice) {
        auto continued = *note;
        continued.extendsPrevious = true;
        events.emplace_back(std::move(continued));
        continue;
      }
    }
    if (const auto* settings = std::get_if<PitchTransitionSettingsPerformanceEvent>(&event)) {
      if (renderPortamentoSettings) {
        events.emplace_back(PortamentoPerformanceEvent{
            .header = settings->header,
            .timeMilliseconds = settings->timeMilliseconds,
        });
      }
    } else {
      events.push_back(event);
    }
  }

  for (const auto& note : notes) {
    for (const auto& segment : note.portamentoSegments) {
      if (segment.endTick <= segment.startTick) {
        continue;
      }
      auto header = segment.header;
      header.tick = segment.startTick;
      header.sequence = nextSequence++;
      events.emplace_back(NotePerformanceEvent{
          .header = header,
          .key = segment.key,
          .linearVelocity = note.source.linearVelocity,
          .durationTicks =
              static_cast<u32>(std::min<u64>(segment.endTick - segment.startTick, std::numeric_limits<u32>::max())),
          .extendsPrevious = segment.extendsPrevious,
          .restartsLfoPhase = note.source.restartsLfoPhase,
          .note = note.source.note,
          .lane = note.source.lane,
      });
    }
  }
}

[[nodiscard]] PitchTransitionRenderingHint effectiveRendering(const PerformanceSequence& performance,
                                                              const MidiExportOptions& options,
                                                              const PitchTransitionIntent& transition) {
  if (options.pitchTransitions == MidiPitchTransitionRendering::Portamento) {
    return PitchTransitionRenderingHint::Portamento;
  }
  if (options.pitchTransitions == MidiPitchTransitionRendering::PitchBend) {
    return PitchTransitionRenderingHint::PitchBend;
  }
  return transition.preferredRendering.value_or(performance.preferredPitchTransitionRendering);
}

}  // namespace

PerformanceSequence lowerMidiPerformanceAutomation(const PerformanceSequence& performance,
                                                   const MidiExportOptions& options) {
  return lowerMidiPerformanceAutomation(performance, options, PerformanceTempoMap{performance});
}

PerformanceSequence lowerMidiPerformanceAutomation(const PerformanceSequence& performance,
                                                   const MidiExportOptions& options,
                                                   const PerformanceTempoMap& tempos) {
  PerformanceSequence lowered = performance;

  for (auto& track : lowered.tracks) {
    u64 nextSequence = 0;
    std::vector<const PerformanceAutomation*> portamentoTransitions;
    std::vector<const PerformanceAutomation*> pitchBendTransitions;
    for (const auto& event : track.events) {
      nextSequence = std::max(nextSequence, performanceEventHeader(event).sequence + 1);
    }
    for (const auto& automation : track.automations) {
      nextSequence = std::max(nextSequence, automation.header.sequence + 1);
      if (const auto* transition = pitchTransitionIntent(automation)) {
        if (automation.realization.endReason != PerformanceAutomationEndReason::Completed &&
            automation.realization.endTick <= automation.realization.startTick) {
          continue;
        }
        auto& transitions =
            effectiveRendering(performance, options, *transition) == PitchTransitionRenderingHint::Portamento
                ? portamentoTransitions
                : pitchBendTransitions;
        transitions.push_back(&automation);
      }
    }
    const auto sortTransitions = [](auto& transitions) {
      std::ranges::stable_sort(transitions, [](const auto* lhs, const auto* rhs) {
        return std::tie(lhs->realization.startTick, lhs->header.sequence) <
               std::tie(rhs->realization.startTick, rhs->header.sequence);
      });
    };
    sortTransitions(portamentoTransitions);
    sortTransitions(pitchBendTransitions);

    auto notes = collectNotes(track);
    std::vector<PerformanceEvent> events;
    events.reserve(track.events.size() + (portamentoTransitions.size() + pitchBendTransitions.size()) * 4);
    lowerPortamento(lowered, events, track.events, notes, portamentoTransitions, tempos, nextSequence);
    linkPitchBendVoices(notes, pitchBendTransitions);
    const bool renderPortamentoSettings =
        options.pitchTransitions == MidiPitchTransitionRendering::Portamento ||
        (options.pitchTransitions == MidiPitchTransitionRendering::PreserveFormat &&
         (performance.preferredPitchTransitionRendering == PitchTransitionRenderingHint::Portamento ||
          !portamentoTransitions.empty()));
    appendSourceEvents(events, track, notes, renderPortamentoSettings, nextSequence);
    lowerPitchBends(lowered, events, notes, pitchBendTransitions, nextSequence);
    std::ranges::stable_sort(events, [](const PerformanceEvent& lhs, const PerformanceEvent& rhs) {
      const auto& left = performanceEventHeader(lhs);
      const auto& right = performanceEventHeader(rhs);
      return std::tie(left.tick, left.sequence) < std::tie(right.tick, right.sequence);
    });
    track.events = std::move(events);
    std::erase_if(track.automations,
                  [](const PerformanceAutomation& automation) { return pitchTransitionIntent(automation) != nullptr; });
  }
  return lowered;
}

}  // namespace vgmtrans::core
