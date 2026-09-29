/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/PitchTransitionMidiLowering.h"

#include "value/export/PerformancePitchBendContext.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vgmtrans::core {

namespace {

struct PortamentoSegment {
  PerformanceEventHeader header;
  u64 endTick = 0;
  double key = 0.0;
  // Pitch bend is channel-relative to the MIDI note that actually started the
  // voice. A segment entered without a new attack inherits that note's key.
  double bendBaseKey = 0.0;
  bool extendsPrevious = false;
  bool restartsEnvelope = true;
};

struct NoteSpan {
  const NotePerformanceEvent& source;
  u64 endTick = 0;
  // A boundary pitch bend can carry the preceding MIDI voice into this
  // logical note instead of retriggering its attack.
  bool extendsMidiNote = false;
  bool startsWithPortamento = false;
  double bendBaseKey = 0.0;
  // Empty unless MIDI portamento must replace this source note.
  std::vector<PortamentoSegment> portamentoSegments;
};

// A persistent normalized wheel is interpreted using the source and instrument
// ranges active at the moment its pitch is queried.
class PitchBendRangeTimeline {
 public:
  PitchBendRangeTimeline(const std::vector<PerformanceEvent>& events,
                         const ResolvedPerformance& performance)
      : initial_(performance) {
    PerformancePitchBendContext context = initial_;

    for (const auto& event : events) {
      if (context.apply(event, performance)) {
        points_.push_back(Point{.header = performanceEventHeader(event), .context = context});
      }
    }
  }

  [[nodiscard]] double semitones(const PitchBendPerformanceEvent& bend,
                                 const PerformanceEventHeader& at) const {
    const auto found =
        std::ranges::upper_bound(points_, at.order(), {}, [](const Point& point) { return point.header.order(); });
    const auto& context = found == points_.begin() ? initial_ : std::prev(found)->context;
    return context.semitones(bend);
  }

 private:
  struct Point {
    PerformanceEventHeader header;
    PerformancePitchBendContext context;
  };

  PerformancePitchBendContext initial_;
  std::vector<Point> points_;
};

enum class PitchBendWriteKind {
  Source,
  AbsoluteTransition,
  HeldTransition,
};

// Attack-free transitions use a separate layer so they can either inherit the
// held pitch or establish a declared start without discarding source bends.
struct PitchBendWrite {
  PitchBendPerformanceEvent bend;
  PitchBendWriteKind kind = PitchBendWriteKind::Source;
  bool reset = false;
  bool establishesHeldPitch = false;
};

// Source wheels stay normalized until queried; held transitions are already
// semitone offsets. Only transition-owned layers accept a scheduled reset.
struct PitchBendState {
  PitchBendPerformanceEvent primary;
  std::optional<PerformanceAutomationId> primaryOwner;
  double heldSemitones = 0.0;
  double heldBaseSemitones = 0.0;
  std::optional<PerformanceAutomationId> heldOwner;
  bool hasPitch = false;
};

using PitchBendWrites = std::vector<PitchBendWrite>;

[[nodiscard]] u64 noteEnd(const NotePerformanceEvent& note) {
  return addTicks(note.header.tick, note.durationTicks);
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
    if (source == nullptr || !source->note.valid()) continue;
    if (auto* note = findNote(notes, source->note)) {
      note->endTick = std::max(note->endTick, noteEnd(*source));
    } else {
      notes.push_back(NoteSpan{.source = *source, .endTick = noteEnd(*source), .bendBaseKey = source->key});
    }
  }
  return notes;
}

// Source voices are explicit. Only the preceding physical MIDI note needs to
// be recovered here, because portamento can change its bend reference key.
[[nodiscard]] NoteSpan* previousVoiceNote(std::vector<NoteSpan>& notes, const NoteSpan& note) {
  NoteSpan* previous = nullptr;
  for (auto& candidate : notes) {
    if (&candidate == &note) {
      break;
    }
    if (candidate.source.voice == note.source.voice) {
      previous = &candidate;
    }
  }
  return previous;
}

[[nodiscard]] double bendBaseKeyAt(const NoteSpan& note, u64 tick) {
  double key = note.bendBaseKey;
  for (const auto& segment : note.portamentoSegments) {
    if (segment.header.tick > tick) {
      break;
    }
    key = segment.bendBaseKey;
  }
  return key;
}

[[nodiscard]] std::optional<double> primaryPitchBendAt(const std::vector<PerformanceEvent>& events,
                                                       const PerformanceEventHeader& at,
                                                       const PitchBendRangeTimeline& ranges) {
  const PitchBendPerformanceEvent* latest = nullptr;
  for (const auto& event : events) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    if (bend == nullptr || bend->layer != kPrimaryPitchBendLayer || bend->header.order() > at.order()) {
      continue;
    }
    if (latest == nullptr || latest->header.order() < bend->header.order()) {
      latest = bend;
    }
  }
  return latest == nullptr ? std::nullopt : std::optional{ranges.semitones(*latest, at)};
}

[[nodiscard]] std::optional<double> establishedPitchBend(const std::vector<PerformanceEvent>& events,
                                                         const NoteSpan& note,
                                                         const PerformanceAutomation& automation,
                                                         const PitchTransitionIntent& transition, u64 startTick,
                                                         const PitchBendRangeTimeline& ranges) {
  auto at = automation.header;
  at.tick = startTick;
  const auto bend = primaryPitchBendAt(events, at, ranges);
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

[[nodiscard]] PerformanceEventHeader atAutomationTick(const PerformanceAutomation& automation, u64 tick) {
  auto header = automation.header;
  header.tick = tick;
  header.automation = automation.id;
  return header;
}

void addWarning(std::vector<Diagnostic>& diagnostics, const PerformanceAutomation& automation, std::string message) {
  diagnostics.push_back(Diagnostic{
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

[[nodiscard]] PitchBendWrite transitionPitchBend(const PerformanceAutomation& automation,
                                                 bool held, u64 tick, double semitones,
                                                 bool reset = false, bool establishesHeldPitch = false) {
  return PitchBendWrite{
      .bend =
          PitchBendPerformanceEvent{
              .header = atAutomationTick(automation, tick),
              .semitones = semitones,
          },
      .kind = held ? PitchBendWriteKind::HeldTransition : PitchBendWriteKind::AbsoluteTransition,
      .reset = reset,
      .establishesHeldPitch = establishesHeldPitch,
  };
}

template <class Emit>
PitchBendState resolvePitchBends(PitchBendWrites& writes, PitchBendLayerId heldTransitionLayer,
                                const PitchBendRangeTimeline& ranges, Emit emit,
                                const PerformanceEventHeader* through = nullptr) {
  // A delayed slide can insert an earlier starting pitch. Sort the owned list
  // only when needed, preserving insertion order for coincident curve samples.
  const auto order = [](const PitchBendWrite& write) { return write.bend.header.order(); };
  if (!std::ranges::is_sorted(writes, {}, order)) {
    std::ranges::stable_sort(writes, {}, order);
  }
  PitchBendState state;
  for (const auto& write : writes) {
    if (through && write.bend.header.order() > through->order()) {
      break;
    }
    if (write.kind == PitchBendWriteKind::Source) {
      if (write.bend.layer == kPrimaryPitchBendLayer) {
        state.primary = write.bend;
        state.primaryOwner.reset();
        state.hasPitch = true;
      }
      emit(write.bend);
      continue;
    }

    const bool held = write.kind == PitchBendWriteKind::HeldTransition;
    auto& owner = held ? state.heldOwner : state.primaryOwner;
    if (write.reset) {
      if (owner != write.bend.header.automation) {
        continue;
      }
      owner.reset();
      if (held) {
        state.heldSemitones = 0.0;
      } else {
        state.primary = {};
      }
    } else if (held) {
      if (owner != write.bend.header.automation) {
        // Keep source bend beneath a held transition, but cancel an interrupted
        // absolute transition when the new transition declares its start pitch.
        state.heldBaseSemitones = state.heldSemitones;
        if (write.establishesHeldPitch) {
          state.heldBaseSemitones =
              state.primaryOwner ? -ranges.semitones(state.primary, write.bend.header) : 0.0;
        }
      }
      state.heldSemitones = state.heldBaseSemitones + write.bend.semitones;
      owner = write.bend.header.automation;
    } else {
      if (state.heldSemitones != 0.0) {
        auto reset = write.bend;
        reset.semitones = 0.0;
        reset.layer = heldTransitionLayer;
        reset.header.automation.reset();
        emit(std::move(reset));
      }
      state.heldSemitones = 0.0;
      state.heldOwner.reset();
      state.primary = write.bend;
      owner = write.bend.header.automation;
    }

    state.hasPitch = true;
    auto bend = write.bend;
    bend.semitones = held ? state.heldSemitones : state.primary.semitones;
    bend.layer = held ? heldTransitionLayer : kPrimaryPitchBendLayer;
    if (write.reset) {
      // A reset at the next attack is not part of the old transition's path.
      bend.header.automation.reset();
    }
    emit(std::move(bend));
  }
  return state;
}

[[nodiscard]] bool pitchEstablished(PitchBendWrites& bends, const NoteSpan& note,
                                    const PerformanceAutomation& automation, const PitchTransitionIntent& transition,
                                    u64 startTick, PitchBendLayerId heldTransitionLayer,
                                    const PitchBendRangeTimeline& ranges) {
  auto at = automation.header;
  at.tick = startTick;
  const auto state = resolvePitchBends(bends, heldTransitionLayer, ranges, [](const auto&) {}, &at);
  const double bend = ranges.semitones(state.primary, at) + state.heldSemitones;
  return state.hasPitch && std::abs(bendBaseKeyAt(note, startTick) + bend - transition.startKey) < 0.02;
}

[[nodiscard]] bool appendPitchBends(PitchBendWrites& bends, const PerformanceAutomation& automation,
                                    const PitchTransitionIntent& transition, const NoteSpan& note,
                                    bool held, bool retainReleaseTail, PitchBendLayerId heldTransitionLayer,
                                    const PitchBendRangeTimeline& ranges) {
  const u64 startTick = std::max(note.source.header.tick, automation.realization.startTick);
  const u64 endTick =
      retainReleaseTail ? automation.realization.endTick : std::min(note.endTick, automation.realization.endTick);
  if (startTick >= note.endTick || endTick < startTick) {
    return false;
  }

  // A delayed slide may begin away from the note's nominal key.
  const double noteBaseKey = bendBaseKeyAt(note, note.source.header.tick);
  const bool startPitchEstablished =
      pitchEstablished(bends, note, automation, transition, startTick, heldTransitionLayer, ranges);
  // A held source voice may either continue from its live bend or explicitly
  // reload the transition's declared start key at the note boundary.
  const bool establishesHeldPitch = held && !startPitchEstablished;
  if (startTick > note.source.header.tick && std::abs(transition.startKey - noteBaseKey) > 0.000001 &&
      !startPitchEstablished) {
    bends.push_back(transitionPitchBend(automation, held, note.source.header.tick,
                                        held ? 0.0 : transition.startKey - noteBaseKey));
  }

  const auto appendAt = [&](u64 tick) {
    const u64 elapsed = tick - automation.realization.startTick;
    const double transitionBaseKey = held && !establishesHeldPitch ? transition.startKey : bendBaseKeyAt(note, tick);
    const double key =
        pitchTransitionValueAt(transition, static_cast<u32>(std::min<u64>(elapsed, std::numeric_limits<u32>::max())));
    bends.push_back(transitionPitchBend(automation, held, tick, key - transitionBaseKey, false, establishesHeldPitch));
  };

  if (const auto* sampled = std::get_if<SampledAutomationCurve>(&transition.curve)) {
    std::vector<u64> sampleTicks{startTick, endTick};
    sampleTicks.reserve(sampled->samples.size() + note.portamentoSegments.size() + 2);
    for (const AutomationSample& sample : sampled->samples) {
      const u64 tick = addTicks(automation.realization.startTick, sample.tickOffset);
      if (tick >= startTick && tick <= endTick) {
        sampleTicks.push_back(tick);
      }
    }
    for (const PortamentoSegment& segment : note.portamentoSegments) {
      if (segment.header.tick >= startTick && segment.header.tick <= endTick) {
        sampleTicks.push_back(segment.header.tick);
      }
    }
    std::ranges::sort(sampleTicks);
    const auto uniqueEnd = std::ranges::unique(sampleTicks).begin();
    sampleTicks.erase(uniqueEnd, sampleTicks.end());
    for (const u64 tick : sampleTicks) {
      appendAt(tick);
    }
    return true;
  }

  for (u64 tick = startTick;; ++tick) {
    appendAt(tick);
    if (tick == endTick) {
      break;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u64> nextIndependentAttack(
    const std::vector<PerformanceEvent>& events, const PerformanceAutomation& automation,
    std::span<const NoteSpan* const> affectedNotes) {
  const bool continuesAtEnd = automation.realization.endReason == PerformanceAutomationEndReason::Completed &&
                              pitchTransitionIntent(automation)->continuesAcrossNotes;
  std::optional<u64> next;
  for (const auto& event : events) {
    const auto* note = std::get_if<NotePerformanceEvent>(&event);
    if (note == nullptr || note->extendsPrevious || note->header.tick < automation.realization.endTick) {
      continue;
    }
    // A curve can finish on a new note that still belongs to that curve.
    // Reuse the affected-note set rather than reconstructing that relationship.
    if (continuesAtEnd && note->header.tick == automation.realization.endTick &&
        std::ranges::any_of(affectedNotes, [&](const auto* affected) { return affected->source.note == note->note; })) {
      continue;
    }
    next = next ? std::min(*next, note->header.tick) : note->header.tick;
  }
  return next;
}

[[nodiscard]] PitchBendWrites takeSourcePitchBends(std::vector<PerformanceEvent>& events) {
  PitchBendWrites bends;
  std::erase_if(events, [&](const PerformanceEvent& event) {
    const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
    if (bend == nullptr) {
      return false;
    }
    bends.push_back(PitchBendWrite{.bend = *bend});
    return true;
  });
  return bends;
}

[[nodiscard]] PitchBendLayerId unusedPitchBendLayer(const PitchBendWrites& bends) {
  std::unordered_set<u32> used;
  for (const auto& write : bends) {
    if (write.bend.layer.valid()) {
      used.insert(write.bend.layer.value);
    }
  }
  for (u32 candidate = 1; candidate != invalidIdValue; ++candidate) {
    if (!used.contains(candidate)) {
      return PitchBendLayerId{candidate};
    }
  }
  throw std::overflow_error("Pitch bend layer space exhausted");
}

void lowerPitchBends(std::vector<Diagnostic>& diagnostics, std::vector<PerformanceEvent>& events,
                     std::vector<NoteSpan>& notes, const std::vector<const PerformanceAutomation*>& transitions,
                     const PitchBendRangeTimeline& ranges) {
  auto bends = takeSourcePitchBends(events);
  const PitchBendLayerId heldTransitionLayer = unusedPitchBendLayer(bends);
  for (const auto* automation : transitions) {
    const auto& transition = *pitchTransitionIntent(*automation);
    const auto* anchor = findNote(notes, transition.note);
    if (anchor == nullptr) {
      addWarning(diagnostics, *automation, "Pitch transition did not reference a rendered note");
      continue;
    }

    const bool held = automation->realization.startTick <= anchor->source.header.tick &&
                      previousVoiceNote(notes, *anchor) != nullptr;
    std::vector<const NoteSpan*> affectedNotes;
    for (const auto& note : notes) {
      if (affectsNote(*automation, transition, *anchor, note)) {
        affectedNotes.push_back(&note);
      }
    }
    bool rendered = false;
    for (const auto* note : affectedNotes) {
      rendered |= appendPitchBends(bends, *automation, transition, *note, held, note == affectedNotes.back(),
                                   heldTransitionLayer, ranges);
    }
    if (rendered) {
      // Retain the terminal bend through note-off and the synth's release
      // phase. Make its next-attack reset visible while lowering later
      // transitions so they inherit the chronological pitch state.
      if (const auto resetTick = nextIndependentAttack(events, *automation, affectedNotes)) {
        bends.push_back(transitionPitchBend(*automation, held, *resetTick, 0.0, true));
      }
    }
  }

  resolvePitchBends(bends, heldTransitionLayer, ranges,
                    [&](PitchBendPerformanceEvent bend) { events.emplace_back(std::move(bend)); });
}

void beginPortamentoRewrite(NoteSpan& note) {
  if (!note.portamentoSegments.empty()) {
    return;
  }
  note.portamentoSegments.push_back(PortamentoSegment{
      .header = note.source.header,
      .endTick = note.endTick,
      .key = note.source.key,
      .bendBaseKey = note.bendBaseKey,
      .restartsEnvelope = note.source.restartsEnvelope,
  });
}

void splitForPortamento(NoteSpan& note, const PerformanceAutomation& automation,
                        const PitchTransitionIntent& transition, bool held, bool sourceEstablishesStart) {
  const u64 startTick = automation.realization.startTick;
  if (startTick <= note.source.header.tick) {
    note.startsWithPortamento = true;
    note.portamentoSegments.front().key = transition.targetKey;
    note.portamentoSegments.front().bendBaseKey = transition.targetKey;
    note.portamentoSegments.front().extendsPrevious = false;
    if (held) {
      note.portamentoSegments.front().restartsEnvelope = false;
    }
    return;
  }

  const u64 clampedStart = std::min(startTick, note.endTick);
  std::erase_if(note.portamentoSegments, [=](const PortamentoSegment& segment) {
    return segment.header.tick >= clampedStart || segment.endTick <= segment.header.tick;
  });
  for (auto& segment : note.portamentoSegments) {
    if (segment.endTick > clampedStart) {
      const u32 overlap = transition.portamentoRendering.overlapTicks;
      segment.endTick = std::min(note.endTick, addTicks(clampedStart, overlap));
      if (!sourceEstablishesStart) {
        segment.key = transition.startKey;
        segment.bendBaseKey = transition.startKey;
      }
    }
  }
  if (clampedStart < note.endTick) {
    auto& segment = note.portamentoSegments.emplace_back(PortamentoSegment{
        .header = automation.header,
        .endTick = note.endTick,
        .key = transition.targetKey,
        .bendBaseKey = transition.targetKey,
        .restartsEnvelope = false,
    });
    segment.header.tick = clampedStart;
  }
}

void inheritMidiBendBases(std::vector<NoteSpan>& notes) {
  for (auto& note : notes) {
    auto* previous = previousVoiceNote(notes, note);
    if (previous == nullptr || note.startsWithPortamento) {
      continue;
    }
    const double baseKey = bendBaseKeyAt(*previous, note.source.header.tick);
    note.extendsMidiNote = true;
    note.bendBaseKey = baseKey;
    if (!note.portamentoSegments.empty()) {
      auto& first = note.portamentoSegments.front();
      first.bendBaseKey = baseKey;
      first.extendsPrevious = true;
    }
  }
}

void lowerPortamento(std::vector<Diagnostic>& diagnostics, std::vector<PerformanceEvent>& events,
                     const std::vector<PerformanceEvent>& sourceEvents, std::vector<NoteSpan>& notes,
                     const std::vector<const PerformanceAutomation*>& transitions, const PerformanceTempoMap& tempos,
                     u64& nextSequence, const PitchBendRangeTimeline& ranges) {
  for (const auto* automation : transitions) {
    const auto& transition = *pitchTransitionIntent(*automation);
    auto* note = findNote(notes, transition.note);
    if (note == nullptr) {
      addWarning(diagnostics, *automation, "Pitch transition did not reference a rendered note");
      continue;
    }
    if (std::holds_alternative<SampledAutomationCurve>(transition.curve)) {
      addWarning(diagnostics, *automation,
                 "Native MIDI portamento cannot preserve the transition's exact sampled pitch curve");
    }

    const u64 startTick = automation->realization.startTick;
    if (startTick >= note->endTick) {
      continue;
    }
    auto* previous = startTick <= note->source.header.tick ? previousVoiceNote(notes, *note) : nullptr;
    const NoteSpan* sourceNote = previous != nullptr ? previous : note;
    const auto sourceBend = establishedPitchBend(sourceEvents, *sourceNote, *automation, transition, startTick, ranges);
    beginPortamentoRewrite(*note);
    if (startTick <= note->source.header.tick && previous != nullptr) {
      beginPortamentoRewrite(*previous);
      auto& segment = previous->portamentoSegments.back();
      const u32 overlap = transition.portamentoRendering.overlapTicks;
      const u64 overlapEnd = addTicks(startTick, overlap);
      segment.endTick = std::max(segment.endTick, overlapEnd);
    }
    splitForPortamento(*note, *automation, transition, previous != nullptr, sourceBend.has_value());

    if (sourceBend && std::abs(*sourceBend) > 0.000001) {
      events.emplace_back(PitchBendPerformanceEvent{
          .header = atTick(automation->header, startTick, nextSequence),
          .semitones = 0.0,
      });
    }

    events.emplace_back(PortamentoPerformanceEvent{
        .header = atTick(automation->header, startTick, nextSequence),
        .timeMilliseconds = transition.portamentoRendering.useCurrentTiming
                                ? std::nullopt
                                : std::optional{physicalDurationMilliseconds(transition, tempos, startTick)},
        .previousKey = transition.startKey,
    });
    if (transition.portamentoRendering.restoreTimeMilliseconds) {
      events.emplace_back(PortamentoPerformanceEvent{
          .header = atTick(automation->header, note->endTick, nextSequence),
          .timeMilliseconds = *transition.portamentoRendering.restoreTimeMilliseconds,
      });
    }
  }
}

void appendSourceEvents(std::vector<PerformanceEvent>& events, const std::vector<PerformanceEvent>& sourceEvents,
                        const std::vector<NoteSpan>& notes, bool renderPortamentoSettings, u64& nextSequence) {
  for (const auto& event : sourceEvents) {
    const NoteSpan* span = nullptr;
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event); note != nullptr && note->note.valid()) {
      span = findNote(notes, note->note);
      if (span != nullptr && !span->portamentoSegments.empty()) {
        continue;
      }
    }
    const bool midiPortamentoEvent = std::holds_alternative<PortamentoPerformanceEvent>(event) ||
                                     std::holds_alternative<PortamentoEnablePerformanceEvent>(event);
    if (midiPortamentoEvent && !renderPortamentoSettings) {
      continue;
    }
    if (const auto* settings = std::get_if<PitchTransitionSettingsPerformanceEvent>(&event)) {
      if (renderPortamentoSettings) {
        events.emplace_back(PortamentoPerformanceEvent{
            .header = settings->header,
            .timeMilliseconds = settings->timeMilliseconds,
        });
      }
    } else {
      auto& copy = events.emplace_back(event);
      if (span != nullptr) {
        std::get<NotePerformanceEvent>(copy).extendsPrevious |= span->extendsMidiNote;
      }
    }
  }

  for (const auto& note : notes) {
    for (const auto& segment : note.portamentoSegments) {
      if (segment.endTick <= segment.header.tick) {
        continue;
      }
      auto event = note.source;
      event.header = segment.header;
      event.header.sequence = nextSequence++;
      event.key = segment.key;
      event.durationTicks =
          static_cast<u32>(std::min<u64>(segment.endTick - segment.header.tick, std::numeric_limits<u32>::max()));
      event.extendsPrevious = segment.extendsPrevious;
      event.restartsEnvelope = segment.restartsEnvelope;
      events.emplace_back(std::move(event));
    }
  }
}

[[nodiscard]] PitchTransitionRenderingHint effectiveRendering(const PerformanceSequence& performance,
                                                              const MidiExportOptions& options,
                                                              const PitchTransitionIntent& transition) {
  if (transition.portamentoRendering.required) {
    return PitchTransitionRenderingHint::Portamento;
  }
  if (options.pitchTransitions == MidiPitchTransitionRendering::Portamento) {
    return PitchTransitionRenderingHint::Portamento;
  }
  if (options.pitchTransitions == MidiPitchTransitionRendering::PitchBend) {
    return PitchTransitionRenderingHint::PitchBend;
  }
  return transition.preferredRendering.value_or(performance.preferredPitchTransitionRendering);
}

}  // namespace

std::vector<PerformanceEvent> detail::lowerMidiTrackEvents(
    const ResolvedPerformance& resolved, size_t trackIndex, const MidiExportOptions& options,
    const PerformanceTempoMap& tempos, std::vector<Diagnostic>& diagnostics) {
  const auto& performance = resolved.performance();
  const auto& track = performance.tracks.at(trackIndex);
  u64 nextSequence = 0;
  std::vector<const PerformanceAutomation*> portamentoTransitions;
  std::vector<const PerformanceAutomation*> pitchBendTransitions;
  const auto addTransition = [&](const PerformanceAutomation& automation) {
    const auto rendering = effectiveRendering(performance, options, *pitchTransitionIntent(automation));
    auto& transitions = rendering == PitchTransitionRenderingHint::Portamento ? portamentoTransitions
                                                                             : pitchBendTransitions;
    transitions.push_back(&automation);
  };
  for (const auto& event : track.events) {
    nextSequence = std::max(nextSequence, performanceEventHeader(event).sequence + 1);
  }
  for (const auto& automation : track.automations) {
    nextSequence = std::max(nextSequence, automation.header.sequence + 1);
    if (pitchTransitionIntent(automation)) {
      if (automation.realization.endReason != PerformanceAutomationEndReason::Completed &&
          automation.realization.endTick <= automation.realization.startTick) {
        continue;
      }
      addTransition(automation);
    }
  }
  const auto sortTransitions = [](auto& transitions) {
    std::ranges::stable_sort(transitions, [](const auto* lhs, const auto* rhs) {
      return std::tie(lhs->realization.startTick, lhs->header.sequence) <
             std::tie(rhs->realization.startTick, rhs->header.sequence);
    });
  };
  auto notes = collectNotes(track);
  // A changed key is already a source voice operation. Only MIDI needs an
  // instantaneous pitch transition when no explicit boundary slide replaces it.
  std::vector<PerformanceAutomation> keyChanges;
  keyChanges.reserve(notes.size());
  u32 nextAutomation = 0;
  for (const auto& motion : track.automations) {
    if (motion.id.valid()) {
      nextAutomation = std::max(nextAutomation, motion.id.value + 1);
    }
  }
  for (const auto& note : notes) {
    const auto* previous = previousVoiceNote(notes, note);
    if (previous == nullptr) {
      continue;
    }
    const auto atBoundary = [&](const PerformanceAutomation* motion) {
      return pitchTransitionIntent(*motion)->note == note.source.note &&
             motion->realization.startTick <= note.source.header.tick;
    };
    if (std::ranges::any_of(portamentoTransitions, atBoundary) ||
        std::ranges::any_of(pitchBendTransitions, atBoundary)) {
      continue;
    }
    keyChanges.push_back(PerformanceAutomation{
        .id = PerformanceAutomationId{nextAutomation++},
        .header = note.source.header,
        .intent = PitchTransitionIntent{
            .note = note.source.note,
            .lane = note.source.lane,
            .startKey = pitchTransitionKeyAt(track, previous->source.note, previous->source.lane, note.source.header.tick)
                            .value_or(previous->source.key),
            .targetKey = note.source.key,
        },
        .realization = {.startTick = note.source.header.tick, .endTick = note.source.header.tick},
    });
    addTransition(keyChanges.back());
  }
  sortTransitions(portamentoTransitions);
  sortTransitions(pitchBendTransitions);

  std::optional<PitchBendRangeTimeline> pitchBendRanges;
  if (!portamentoTransitions.empty() || !pitchBendTransitions.empty()) {
    pitchBendRanges.emplace(track.events, resolved);
  }

  std::vector<PerformanceEvent> events;
  events.reserve(track.events.size() + (portamentoTransitions.size() + pitchBendTransitions.size()) * 4);
  if (!portamentoTransitions.empty()) {
    lowerPortamento(diagnostics, events, track.events, notes, portamentoTransitions, tempos, nextSequence,
                    *pitchBendRanges);
  }
  inheritMidiBendBases(notes);
  const bool renderPortamentoSettings =
      !portamentoTransitions.empty() || options.pitchTransitions == MidiPitchTransitionRendering::Portamento ||
      (options.pitchTransitions == MidiPitchTransitionRendering::PreserveFormat &&
       performance.preferredPitchTransitionRendering == PitchTransitionRenderingHint::Portamento);
  appendSourceEvents(events, track.events, notes, renderPortamentoSettings, nextSequence);
  if (!pitchBendTransitions.empty()) {
    lowerPitchBends(diagnostics, events, notes, pitchBendTransitions, *pitchBendRanges);
  }
  std::ranges::stable_sort(events, {},
                           [](const PerformanceEvent& event) { return performanceEventHeader(event).order(); });
  return events;
}

}  // namespace vgmtrans::core
