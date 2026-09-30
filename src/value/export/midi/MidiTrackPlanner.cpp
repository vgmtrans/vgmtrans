/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/MidiTrackPlanner.h"

#include "value/export/PerformancePitchBendContext.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vgmtrans::core {

namespace {

using detail::MidiNoteBoundary;
using detail::MidiInstrumentEvent;
using detail::MidiTrackEvent;

// Private planning state. Source properties remain borrowed; key, gate and
// continuation decisions are completed here before creating output events.
enum class NoteAction { Attack, Continue, Expired };

struct NoteDraft {
  const NotePerformanceEvent* source;
  PerformanceEventHeader header;
  u64 endTick;
  double key;
  double bendBaseKey;
  bool extendsPrevious;
  bool restartsEnvelope;
  NoteAction action = NoteAction::Attack;
  size_t outputIndex = 0;  // Preserve the boundary's position among coincident source controls.

  explicit NoteDraft(const NotePerformanceEvent& note)
      : source(&note), header(note.header), endTick(addTicks(note.header.tick, note.durationTicks)),
        key(note.key), bendBaseKey(note.key), extendsPrevious(note.extendsPrevious),
        restartsEnvelope(note.restartsEnvelope) {}
};

struct NoteSpan {
  const NotePerformanceEvent& source;
  u64 endTick = 0;  // Source gate including ties, independent of MIDI overlap and hardware deadlines.
  // Native portamento replaces the source boundaries with these segments.
  // Otherwise the source boundaries retain their individual LFO decisions.
  bool usesPortamento = false;
  std::vector<NoteDraft> segments;
};

// Source pitch at a given command: the persistent primary wheel and the
// source/instrument ranges that interpret it. The immutable track owns the wheel.
class SourcePitchTimeline {
 public:
  SourcePitchTimeline(const std::vector<PerformanceEvent>& events, const ResolvedPerformance& performance)
      : initial_{.context = PerformancePitchBendContext{performance}} {
    Point current = initial_;
    for (const auto& event : events) {
      current.order = performanceEventHeader(event).order();
      bool changed = current.context.apply(event, performance);
      if (const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
          bend && bend->layer == kPrimaryPitchBendLayer &&
          (!current.primary || current.primary->header.order() < bend->header.order())) {
        current.primary = bend;
        changed = true;
      }
      if (changed) {
        points_.push_back(current);
      }
    }
  }

  [[nodiscard]] double semitones(const PitchBendPerformanceEvent& bend, const PerformanceEventHeader& at) const {
    return stateAt(at).context.semitones(bend);
  }

  [[nodiscard]] std::optional<double> primaryBendAt(const PerformanceEventHeader& at) const {
    const auto& state = stateAt(at);
    return state.primary ? std::optional{state.context.semitones(*state.primary)} : std::nullopt;
  }

 private:
  struct Point {
    std::pair<u64, u64> order;
    PerformancePitchBendContext context;
    const PitchBendPerformanceEvent* primary = nullptr;
  };

  [[nodiscard]] const Point& stateAt(const PerformanceEventHeader& at) const {
    const auto found = std::ranges::upper_bound(points_, at.order(), {}, &Point::order);
    return found == points_.begin() ? initial_ : *std::prev(found);
  }

  Point initial_;
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

[[nodiscard]] NoteSpan* findNote(std::vector<NoteSpan>& notes, PerformanceNoteId id) {
  const auto found = std::ranges::find_if(notes, [id](const NoteSpan& note) { return note.source.note == id; });
  return found == notes.end() ? nullptr : &*found;
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

[[nodiscard]] std::vector<NoteSpan> collectNotes(const PerformanceTrack& track) {
  std::vector<NoteSpan> notes;
  for (const auto& event : track.events) {
    const auto* source = std::get_if<NotePerformanceEvent>(&event);
    if (source == nullptr || !source->note.valid()) {
      continue;
    }
    if (auto* note = findNote(notes, source->note)) {
      note->endTick = std::max(note->endTick, addTicks(source->header.tick, source->durationTicks));
      note->segments.front().endTick = note->endTick;
    } else {
      auto& span = notes.emplace_back(NoteSpan{
          .source = *source, .endTick = addTicks(source->header.tick, source->durationTicks),
          .segments = {NoteDraft{*source}}});
      span.segments.front().extendsPrevious = previousVoiceNote(notes, span) != nullptr;
    }
  }
  return notes;
}

[[nodiscard]] double bendBaseKeyAt(const NoteSpan& note, u64 tick) {
  double key = note.segments.front().bendBaseKey;
  for (const auto& segment : note.segments) {
    if (segment.header.tick > tick) {
      break;
    }
    key = segment.bendBaseKey;
  }
  return key;
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
                                const SourcePitchTimeline& sourcePitch, Emit emit,
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
              state.primaryOwner ? -sourcePitch.semitones(state.primary, write.bend.header) : 0.0;
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
                                    const SourcePitchTimeline& sourcePitch) {
  auto at = automation.header;
  at.tick = startTick;
  const auto state = resolvePitchBends(bends, heldTransitionLayer, sourcePitch, [](const auto&) {}, &at);
  const double bend = sourcePitch.semitones(state.primary, at) + state.heldSemitones;
  return state.hasPitch && std::abs(bendBaseKeyAt(note, startTick) + bend - transition.startKey) < 0.02;
}

[[nodiscard]] bool appendPitchBends(PitchBendWrites& bends, const PerformanceAutomation& automation,
                                    const PitchTransitionIntent& transition, const NoteSpan& note,
                                    bool held, bool retainReleaseTail, PitchBendLayerId heldTransitionLayer,
                                    const SourcePitchTimeline& sourcePitch) {
  const u64 startTick = std::max(note.source.header.tick, automation.realization.startTick);
  const u64 endTick =
      retainReleaseTail ? automation.realization.endTick : std::min(note.endTick, automation.realization.endTick);
  if (startTick >= note.endTick || endTick < startTick) {
    return false;
  }

  const double noteBaseKey = bendBaseKeyAt(note, note.source.header.tick);
  const bool needsInitialPitch = startTick > note.source.header.tick &&
                                 std::abs(transition.startKey - noteBaseKey) > 0.000001;
  // Ordinary curve samples already specify their pitch. History matters only
  // when inheriting a held pitch or deciding whether to establish an earlier start.
  const bool startPitchEstablished =
      (held || needsInitialPitch) &&
      pitchEstablished(bends, note, automation, transition, startTick, heldTransitionLayer, sourcePitch);
  const bool establishesHeldPitch = held && !startPitchEstablished;
  if (needsInitialPitch && !startPitchEstablished) {
    // Held transitions start on a note boundary; only absolute slides need this earlier write.
    bends.push_back(transitionPitchBend(automation, false, note.source.header.tick, transition.startKey - noteBaseKey));
  }

  const auto appendAt = [&](u64 tick) {
    const u64 elapsed = tick - automation.realization.startTick;
    const double transitionBaseKey = held && startPitchEstablished ? transition.startKey : bendBaseKeyAt(note, tick);
    const double key =
        pitchTransitionValueAt(transition, static_cast<u32>(std::min<u64>(elapsed, std::numeric_limits<u32>::max())));
    bends.push_back(transitionPitchBend(automation, held, tick, key - transitionBaseKey, false, establishesHeldPitch));
  };

  if (const auto* sampled = std::get_if<SampledAutomationCurve>(&transition.curve)) {
    std::vector<u64> sampleTicks{startTick, endTick};
    sampleTicks.reserve(sampled->samples.size() + note.segments.size() + 2);
    for (const AutomationSample& sample : sampled->samples) {
      const u64 tick = addTicks(automation.realization.startTick, sample.tickOffset);
      if (tick >= startTick && tick <= endTick) {
        sampleTicks.push_back(tick);
      }
    }
    for (const NoteDraft& segment : note.segments) {
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
    const std::vector<NoteDraft*>& physicalNotes, const PerformanceAutomation& automation,
    std::span<const NoteSpan* const> affectedNotes) {
  const bool continuesAtEnd = automation.realization.endReason == PerformanceAutomationEndReason::Completed &&
                              pitchTransitionIntent(automation)->continuesAcrossNotes;
  // Physical-note planning has already ordered this timeline.
  for (const auto* note : physicalNotes) {
    if (note->action != NoteAction::Attack || note->header.tick < automation.realization.endTick) {
      continue;
    }
    // A curve can finish on a new note that still belongs to that curve.
    // Reuse the affected-note set rather than reconstructing that relationship.
    if (continuesAtEnd && note->header.tick == automation.realization.endTick &&
        std::ranges::any_of(affectedNotes, [&](const auto* affected) { return affected->source.note == note->source->note; })) {
      continue;
    }
    return note->header.tick;
  }
  return std::nullopt;
}

[[nodiscard]] PitchBendWrites takeSourcePitchBends(std::vector<MidiTrackEvent>& events) {
  PitchBendWrites bends;
  std::erase_if(events, [&](const MidiTrackEvent& event) {
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

void lowerPitchBends(std::vector<Diagnostic>& diagnostics, std::vector<MidiTrackEvent>& events,
                     const std::vector<NoteDraft*>& physicalNotes, std::vector<NoteSpan>& notes, const std::vector<const PerformanceAutomation*>& transitions,
                     const SourcePitchTimeline& sourcePitch) {
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
                                   heldTransitionLayer, sourcePitch);
    }
    if (rendered) {
      // Retain the terminal bend through note-off and the synth's release
      // phase. Make its next-attack reset visible while lowering later
      // transitions so they inherit the chronological pitch state.
      if (const auto resetTick = nextIndependentAttack(physicalNotes, *automation, affectedNotes)) {
        bends.push_back(transitionPitchBend(*automation, held, *resetTick, 0.0, true));
      }
    }
  }

  resolvePitchBends(bends, heldTransitionLayer, sourcePitch,
                    [&](PitchBendPerformanceEvent bend) { events.emplace_back(std::move(bend)); });
}

void splitForPortamento(NoteSpan& note, const PerformanceAutomation& automation,
                        const PitchTransitionIntent& transition, bool held, bool sourceEstablishesStart) {
  const u64 startTick = automation.realization.startTick;
  if (startTick <= note.source.header.tick) {
    note.segments.front().key = transition.targetKey;
    note.segments.front().bendBaseKey = transition.targetKey;
    note.segments.front().extendsPrevious = false;
    if (held) {
      note.segments.front().restartsEnvelope = false;
    }
    return;
  }

  const u64 clampedStart = std::min(startTick, note.endTick);
  std::erase_if(note.segments, [=](const NoteDraft& segment) {
    return segment.header.tick >= clampedStart || segment.endTick <= segment.header.tick;
  });
  for (auto& segment : note.segments) {
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
    auto& segment = note.segments.emplace_back(NoteDraft{note.source});
    segment.header = automation.header;
    segment.header.tick = clampedStart;
    segment.endTick = note.endTick;
    segment.key = segment.bendBaseKey = transition.targetKey;
    segment.extendsPrevious = false;
    segment.restartsEnvelope = false;
  }
}

void lowerPortamento(std::vector<Diagnostic>& diagnostics, std::vector<MidiTrackEvent>& events,
                     std::vector<NoteSpan>& notes,
                     const std::vector<const PerformanceAutomation*>& transitions, const PerformanceTempoMap& tempos,
                     u64& nextSequence, const SourcePitchTimeline& sourcePitch) {
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
    auto at = automation->header;
    at.tick = startTick;
    const auto sourceBend = sourcePitch.primaryBendAt(at);
    // Allow the source's finite wheel register to quantize the declared start.
    const bool sourceEstablishesStart =
        sourceBend && std::abs(bendBaseKeyAt(*sourceNote, startTick) + *sourceBend - transition.startKey) < 0.02;
    note->usesPortamento = true;
    if (startTick <= note->source.header.tick && previous != nullptr) {
      previous->usesPortamento = true;
      auto& segment = previous->segments.back();
      const u32 overlap = transition.portamentoRendering.overlapTicks;
      const u64 overlapEnd = addTicks(startTick, overlap);
      segment.endTick = std::max(segment.endTick, overlapEnd);
    }
    splitForPortamento(*note, *automation, transition, previous != nullptr, sourceEstablishesStart);

    if (sourceEstablishesStart && std::abs(*sourceBend) > 0.000001) {
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

std::vector<NoteDraft*> appendSourceEvents(
    std::vector<MidiTrackEvent>& events, const std::vector<PerformanceEvent>& sourceEvents,
    std::vector<NoteSpan>& notes, bool renderPortamentoSettings, u64& nextSequence,
    const ResolvedPerformance& resolved, std::deque<NoteDraft>& ties) {
  std::vector<NoteDraft*> boundaries;
  const auto append = [&](NoteDraft& note) {
    note.outputIndex = events.size();
    events.emplace_back(MidiNoteBoundary{.header = note.header});
    boundaries.push_back(&note);
  };
  for (const auto& event : sourceEvents) {
    std::visit([&](const auto& source) {
      using Event = std::decay_t<decltype(source)>;
      if constexpr (std::is_same_v<Event, NotePerformanceEvent>) {
        auto* span = source.note.valid() ? findNote(notes, source.note) : nullptr;
        if (span && span->usesPortamento) {
          return;
        }
        auto& note = span && &source == &span->source ? span->segments.front() : ties.emplace_back(source);
        // The span's logical end includes ties; this boundary starts with its
        // own gate. Physical planning extends the attack actually sounding then.
        note.endTick = addTicks(source.header.tick, source.durationTicks);
        if (span) {
          note.extendsPrevious |= span->segments.front().extendsPrevious;
          note.bendBaseKey = span->segments.front().bendBaseKey;
        }
        append(note);
      } else if constexpr (std::is_same_v<Event, InstrumentPerformanceEvent>) {
        events.emplace_back(MidiInstrumentEvent{source.header, resolved.selectionFor(source), source.forceBankSelect});
      } else if constexpr (std::is_same_v<Event, EnvelopePerformanceEvent> ||
                           std::is_same_v<Event, TempoPerformanceEvent> ||
                           std::is_same_v<Event, TimeSignaturePerformanceEvent> ||
                           std::is_same_v<Event, GlobalTransposePerformanceEvent>) {
        events.emplace_back(detail::MidiTimingEvent{source.header});
      } else if constexpr (std::is_same_v<Event, PitchTransitionSettingsPerformanceEvent>) {
        if (renderPortamentoSettings) {
          events.emplace_back(PortamentoPerformanceEvent{
              .header = source.header, .timeMilliseconds = source.timeMilliseconds});
        }
      } else if constexpr (std::is_same_v<Event, PortamentoPerformanceEvent> ||
                           std::is_same_v<Event, PortamentoEnablePerformanceEvent>) {
        if (renderPortamentoSettings) {
          events.emplace_back(source);
        }
      } else {
        events.emplace_back(source);
      }
    }, event);
  }

  for (auto& note : notes) {
    if (!note.usesPortamento) {
      continue;
    }
    for (auto& segment : note.segments) {
      if (segment.endTick <= segment.header.tick) {
        continue;
      }
      segment.header.sequence = nextSequence++;
      append(segment);
    }
  }
  std::ranges::stable_sort(boundaries, {}, [](const NoteDraft* note) { return note->header.order(); });
  return boundaries;
}

// Resolve physical attacks before encoding. Later boundaries can extend an
// earlier attack, but retain their own LFO decisions and source annotations.
void planPhysicalNotes(const std::vector<NoteDraft*>& notes, const ResolvedPerformance& resolved) {
  std::unordered_map<PerformanceVoiceId, NoteDraft*> previous;
  for (auto* note : notes) {
    auto& attack = previous[note->source->voice];
    if (note->extendsPrevious && attack != nullptr) {
      note->bendBaseKey = attack->bendBaseKey;
    }
    if (const auto limit = resolved.voiceFor(*note->source).endLimit) {
      if ((note->extendsPrevious || !note->restartsEnvelope) && note->header.tick >= *limit) {
        note->action = NoteAction::Expired;
        continue;
      }
      note->endTick = std::max(note->header.tick, std::min(note->endTick, *limit));
    }
    if (note->extendsPrevious && attack != nullptr) {
      attack->endTick = std::max(attack->endTick, note->endTick);
      note->action = NoteAction::Continue;
    } else {
      note->bendBaseKey = note->key;
      attack = note;
    }
  }
}

[[nodiscard]] MidiNoteBoundary finishNote(const NoteDraft& note, const ResolvedPerformance& resolved,
                                         const MidiExportOptions& options) {
  const bool restart = !note.extendsPrevious && note.source->restartsLfoPhase;
  MidiNoteBoundary boundary{
      .header = note.header,
      .restartVibrato = note.source->restartsVibratoLfoPhase.value_or(restart),
      .restartTremolo = note.source->restartsTremoloLfoPhase.value_or(restart),
      .restartPan = restart,
      .expired = note.action == NoteAction::Expired,
  };
  if (!boundary.expired && !note.extendsPrevious) {
    boundary.instrument = resolved.selectionFor(*note.source);
  }
  if (note.action == NoteAction::Attack) {
    boundary.attack = detail::MidiAttack{
        note.key, note.source->linearVelocity,
        static_cast<u32>(std::min<u64>(note.endTick - note.header.tick, std::numeric_limits<u32>::max())),
        options.terminatePreviousVoice && note.restartsEnvelope};
  }
  return boundary;
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

std::vector<detail::MidiTrackEvent> detail::planMidiTrack(
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

  std::optional<SourcePitchTimeline> sourcePitch;
  if (!portamentoTransitions.empty() || !pitchBendTransitions.empty()) {
    sourcePitch.emplace(track.events, resolved);
  }

  std::vector<MidiTrackEvent> events;
  events.reserve(track.events.size() + (portamentoTransitions.size() + pitchBendTransitions.size()) * 4);
  if (!portamentoTransitions.empty()) {
    lowerPortamento(diagnostics, events, notes, portamentoTransitions, tempos, nextSequence, *sourcePitch);
  }
  const bool renderPortamentoSettings =
      !portamentoTransitions.empty() || options.pitchTransitions == MidiPitchTransitionRendering::Portamento ||
      (options.pitchTransitions == MidiPitchTransitionRendering::PreserveFormat &&
       performance.preferredPitchTransitionRendering == PitchTransitionRenderingHint::Portamento);
  // Tie boundaries have stable addresses; span segments are no longer split
  // after this point. Gate planning and pitch sampling share those same records.
  std::deque<NoteDraft> ties;
  const auto physicalNotes = appendSourceEvents(events, track.events, notes, renderPortamentoSettings,
                                                nextSequence, resolved, ties);
  planPhysicalNotes(physicalNotes, resolved);
  for (const auto* note : physicalNotes) {
    events[note->outputIndex] = finishNote(*note, resolved, options);
  }
  if (!pitchBendTransitions.empty()) {
    lowerPitchBends(diagnostics, events, physicalNotes, notes, pitchBendTransitions, *sourcePitch);
  }
  std::ranges::stable_sort(events, {},
                           [](const MidiTrackEvent& event) { return performanceEventHeader(event).order(); });
  return events;
}

}  // namespace vgmtrans::core
