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
#include <utility>
#include <vector>

namespace vgmtrans::core {

namespace {

struct NoteSegment {
  PerformanceEventHeader header;
  PerformanceNoteId note;
  PerformanceLaneId lane;
  u64 startTick = 0;
  u64 endTick = 0;
  double key = 0.0;
  double linearVelocity = 1.0;
};

struct NoteChain {
  PerformanceNoteId note;
  PerformanceLaneId lane;
  PerformanceEventHeader header;
  u64 startTick = 0;
  u64 endTick = 0;
  double key = 0.0;
  double linearVelocity = 1.0;
  std::vector<NoteSegment> segments;
};

[[nodiscard]] u64 noteEnd(const NotePerformanceEvent& note) {
  return note.header.tick > std::numeric_limits<u64>::max() - note.durationTicks
             ? std::numeric_limits<u64>::max()
             : note.header.tick + note.durationTicks;
}

[[nodiscard]] PitchTransitionRenderingHint resolvedRendering(PitchTransitionRenderingHint hint,
                                                             const MidiExportOptions& options) {
  switch (options.pitchTransitions) {
    case MidiPitchTransitionRendering::PreserveFormat:
      return hint;
    case MidiPitchTransitionRendering::Portamento:
      return PitchTransitionRenderingHint::Portamento;
    case MidiPitchTransitionRendering::PitchBend:
      return PitchTransitionRenderingHint::PitchBend;
  }
  return hint;
}

[[nodiscard]] bool containsNote(const std::vector<PerformanceNoteId>& notes, PerformanceNoteId note) {
  return std::ranges::find(notes, note) != notes.end();
}

[[nodiscard]] NoteChain* findChain(std::vector<NoteChain>& chains, PerformanceNoteId note) {
  const auto found = std::ranges::find(chains, note, &NoteChain::note);
  return found == chains.end() ? nullptr : &*found;
}

[[nodiscard]] PerformanceEventHeader atTick(const PerformanceEventHeader& origin, u64 tick, u64& nextSequence) {
  auto header = origin;
  header.tick = tick;
  header.sequence = nextSequence++;
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

void splitForPortamento(NoteChain& chain, const PerformanceAutomation& automation,
                        const PitchTransitionIntent& transition, u64 startTick, u32 overlapTicks) {
  if (startTick <= chain.startTick) {
    if (!chain.segments.empty()) {
      chain.segments.front().key = transition.targetKey;
    }
    return;
  }

  const u64 clampedStart = std::min(startTick, chain.endTick);
  std::vector<NoteSegment> before;
  before.reserve(chain.segments.size() + 1);
  for (auto segment : chain.segments) {
    if (segment.startTick >= clampedStart) {
      continue;
    }
    if (segment.endTick > clampedStart) {
      segment.endTick = std::min(chain.endTick, clampedStart > std::numeric_limits<u64>::max() - overlapTicks
                                                    ? std::numeric_limits<u64>::max()
                                                    : clampedStart + overlapTicks);
      segment.key = transition.startKey;
    }
    if (segment.endTick > segment.startTick) {
      before.push_back(std::move(segment));
    }
  }

  if (clampedStart < chain.endTick) {
    before.push_back(NoteSegment{
        .header = automation.header,
        .note = chain.note,
        .lane = chain.lane,
        .startTick = clampedStart,
        .endTick = chain.endTick,
        .key = transition.targetKey,
        .linearVelocity = chain.linearVelocity,
    });
  }
  chain.segments = std::move(before);
}

[[nodiscard]] u16 pitchRangeCents(const PitchTransitionIntent& transition, const PerformanceAutomation& automation,
                                  const NoteChain& chain) {
  const u64 beginTick = std::max(chain.startTick, automation.realization.startTick);
  const u64 endTick = std::min(chain.endTick, automation.realization.endTick);
  if (endTick < beginTick) {
    return 200;
  }
  const u32 beginElapsed =
      static_cast<u32>(std::min<u64>(beginTick - automation.realization.startTick, std::numeric_limits<u32>::max()));
  const u32 endElapsed =
      static_cast<u32>(std::min<u64>(endTick - automation.realization.startTick, std::numeric_limits<u32>::max()));
  double maximum = std::max(std::abs(pitchTransitionValueAt(transition, beginElapsed) - chain.key),
                            std::abs(pitchTransitionValueAt(transition, endElapsed) - chain.key));
  if (beginTick > chain.startTick) {
    maximum = std::max(maximum, std::abs(transition.startKey - chain.key));
  }
  if (const auto* sampled = std::get_if<SampledAutomationCurve>(&transition.curve)) {
    for (const auto& sample : sampled->samples) {
      if (sample.tickOffset >= beginElapsed && sample.tickOffset <= endElapsed) {
        maximum = std::max(maximum, std::abs(sample.value - chain.key));
      }
    }
  }
  const double cents = std::ceil(maximum * 100.0);
  // MIDI's RPN stores 0-127 semitones plus 0-99 fine cents.
  return static_cast<u16>(std::clamp<double>(std::max(200.0, cents), 200.0, 12'799.0));
}

[[nodiscard]] bool transitionAffectsChain(const NoteChain& chain, const NoteChain& anchor,
                                          const PerformanceAutomation& automation,
                                          const PitchTransitionIntent& transition) {
  if (chain.note == anchor.note) {
    return true;
  }
  if (transition.interruptions.newNote != AutomationNewNotePolicy::Continue || chain.lane != transition.lane ||
      chain.startTick < anchor.startTick) {
    return false;
  }
  const u64 activeBegin = std::min(automation.realization.requestedStartTick, automation.realization.startTick);
  return chain.endTick > activeBegin && chain.startTick <= automation.realization.endTick;
}

[[nodiscard]] bool addPitchBendSegment(std::vector<PerformanceEvent>& events, const NoteChain& chain,
                                       const PerformanceAutomation& automation, const PitchTransitionIntent& transition,
                                       u64& nextSequence) {
  const u64 startTick = std::max(chain.startTick, automation.realization.startTick);
  const u64 endTick = std::min(chain.endTick, automation.realization.endTick);
  if (startTick >= chain.endTick || endTick < startTick) {
    return false;
  }

  // A delayed note-on envelope may begin away from the note's nominal key.
  // Establish that starting pitch at note-on and hold it until the motion
  // begins.
  if (startTick > chain.startTick && std::abs(transition.startKey - chain.key) > 0.000001) {
    events.emplace_back(PitchBendPerformanceEvent{
        .header = atTick(automation.header, chain.startTick, nextSequence),
        .semitones = transition.startKey - chain.key,
    });
  }

  const u64 changingTicks = endTick - startTick;
  for (u64 elapsed = 0; elapsed <= changingTicks; ++elapsed) {
    const u64 absoluteTick = startTick + elapsed;
    const u64 automationElapsed = absoluteTick - automation.realization.startTick;
    const u32 sourceElapsed = static_cast<u32>(std::min<u64>(automationElapsed, std::numeric_limits<u32>::max()));
    events.emplace_back(PitchBendPerformanceEvent{
        .header = atTick(automation.header, absoluteTick, nextSequence),
        .semitones = pitchTransitionValueAt(transition, sourceElapsed) - chain.key,
    });
  }
  return true;
}

void lowerTrackAutomations(PerformanceSequence& performance, PerformanceTrack& track,
                           const MidiExportOptions& options) {
  u64 nextSequence = 0;
  for (const auto& event : track.events) {
    nextSequence = std::max(nextSequence, performanceEventHeader(event).sequence + 1);
  }
  for (const auto& automation : track.automations) {
    nextSequence = std::max(nextSequence, automation.header.sequence + 1);
    for (const auto& point : automation.points) {
      nextSequence = std::max(nextSequence, performanceEventHeader(point).sequence + 1);
    }
  }

  std::vector<PerformanceNoteId> involvedNotes;
  for (const auto& automation : track.automations) {
    const auto* transition = pitchTransitionIntent(automation);
    if (transition == nullptr) {
      continue;
    }
    if (!containsNote(involvedNotes, transition->note)) {
      involvedNotes.push_back(transition->note);
    }
    if (transition->previousNote && !containsNote(involvedNotes, *transition->previousNote)) {
      involvedNotes.push_back(*transition->previousNote);
    }
  }

  std::vector<NoteChain> chains;
  for (const auto& event : track.events) {
    const auto* note = std::get_if<NotePerformanceEvent>(&event);
    if (note == nullptr || !note->note.valid()) {
      continue;
    }
    NoteChain* chain = findChain(chains, note->note);
    if (chain == nullptr) {
      chains.push_back(NoteChain{
          .note = note->note,
          .lane = note->lane,
          .header = note->header,
          .startTick = note->header.tick,
          .endTick = noteEnd(*note),
          .key = note->key,
          .linearVelocity = note->linearVelocity,
      });
      chain = &chains.back();
    } else {
      chain->startTick = std::min(chain->startTick, note->header.tick);
      chain->endTick = std::max(chain->endTick, noteEnd(*note));
    }
  }
  std::ranges::stable_sort(chains, [](const NoteChain& lhs, const NoteChain& rhs) {
    return std::tie(lhs.startTick, lhs.header.sequence) < std::tie(rhs.startTick, rhs.header.sequence);
  });
  for (auto& chain : chains) {
    chain.segments.push_back(NoteSegment{
        .header = chain.header,
        .note = chain.note,
        .lane = chain.lane,
        .startTick = chain.startTick,
        .endTick = chain.endTick,
        .key = chain.key,
        .linearVelocity = chain.linearVelocity,
    });
  }

  std::vector<PerformanceEvent> events;
  events.reserve(track.events.size() + track.automations.size() * 4);
  for (auto event : track.events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event);
        note != nullptr && note->note.valid() && containsNote(involvedNotes, note->note)) {
      continue;
    }
    if (const auto* settings = std::get_if<PitchTransitionSettingsPerformanceEvent>(&event)) {
      if (resolvedRendering(settings->renderingHint, options) == PitchTransitionRenderingHint::Portamento) {
        events.emplace_back(PortamentoPerformanceEvent{
            .header = settings->header,
            .timeMilliseconds = settings->timeMilliseconds,
        });
      }
      continue;
    }
    events.push_back(std::move(event));
  }

  std::vector<const PerformanceAutomation*> pitchAutomations;
  for (const auto& automation : track.automations) {
    if (pitchTransitionIntent(automation) != nullptr) {
      pitchAutomations.push_back(&automation);
    }
  }
  std::ranges::stable_sort(pitchAutomations, [](const auto* lhs, const auto* rhs) {
    return std::tie(lhs->realization.startTick, lhs->header.sequence) <
           std::tie(rhs->realization.startTick, rhs->header.sequence);
  });

  // Pitch-bend range is channel state. Choose one range large enough for every
  // transition on a note chain so later chained transitions cannot narrow it
  // before an earlier bend at the same note-on tick.
  for (const auto& chain : chains) {
    const PerformanceAutomation* rangeOrigin = nullptr;
    u16 rangeCents = 200;
    for (const auto* automation : pitchAutomations) {
      const auto& transition = *pitchTransitionIntent(*automation);
      const auto* anchor = findChain(chains, transition.note);
      if (anchor == nullptr || !transitionAffectsChain(chain, *anchor, *automation, transition) ||
          resolvedRendering(transition.renderingHint, options) != PitchTransitionRenderingHint::PitchBend) {
        continue;
      }
      if (rangeOrigin == nullptr) {
        rangeOrigin = automation;
      }
      rangeCents = std::max(rangeCents, pitchRangeCents(transition, *automation, chain));
    }
    if (rangeOrigin != nullptr) {
      events.emplace_back(PitchBendRangePerformanceEvent{
          .header = atTick(rangeOrigin->header, chain.startTick, nextSequence),
          .cents = rangeCents,
      });
    }
  }

  for (const auto* automation : pitchAutomations) {
    const auto& transition = *pitchTransitionIntent(*automation);
    NoteChain* chain = findChain(chains, transition.note);
    if (chain == nullptr) {
      addWarning(performance, *automation, "Pitch transition did not reference a rendered note");
      continue;
    }
    const auto rendering = resolvedRendering(transition.renderingHint, options);
    if (rendering == PitchTransitionRenderingHint::PitchBend) {
      std::optional<PitchTransitionRenderingHint> successorRendering;
      const auto successor = std::ranges::find_if(pitchAutomations, [&](const auto* candidate) {
        return candidate->realization.continuesFrom == automation->id;
      });
      if (successor != pitchAutomations.end()) {
        successorRendering = resolvedRendering(pitchTransitionIntent(**successor)->renderingHint, options);
      }

      const NoteChain* terminalChain = nullptr;
      for (const auto& candidate : chains) {
        if (transitionAffectsChain(candidate, *chain, *automation, transition) &&
            addPitchBendSegment(events, candidate, *automation, transition, nextSequence)) {
          terminalChain = &candidate;
        }
      }

      // Completed transitions hold their target for the remainder of the last
      // affected note. Explicit interruption resets at the transition
      // boundary. Chained pitch bends remain continuous; a successor rendered
      // another way needs a reset before taking over.
      const bool continuesAsPitchBend = successorRendering == PitchTransitionRenderingHint::PitchBend;
      const bool resetAtTransitionEnd =
          automation->realization.endReason == PerformanceAutomationEndReason::NewNote ||
          automation->realization.endReason == PerformanceAutomationEndReason::SourceStopped ||
          (successorRendering && !continuesAsPitchBend) ||
          (automation->realization.endReason == PerformanceAutomationEndReason::Replaced && !successorRendering);
      if (terminalChain != nullptr && resetAtTransitionEnd) {
        events.emplace_back(PitchBendPerformanceEvent{
            .header = atTick(automation->header, automation->realization.endTick, nextSequence),
            .semitones = 0.0,
        });
      } else if (terminalChain != nullptr && !successorRendering &&
                 automation->realization.endReason != PerformanceAutomationEndReason::Replaced) {
        events.emplace_back(PitchBendPerformanceEvent{
            .header = atTick(automation->header, terminalChain->endTick, nextSequence),
            .semitones = 0.0,
        });
      }
      continue;
    }

    const NativePortamentoHint native = transition.nativePortamento.value_or(NativePortamentoHint{
        .timeMilliseconds = static_cast<double>(transition.durationTicks),
    });
    if (std::holds_alternative<SampledAutomationCurve>(transition.curve)) {
      addWarning(performance, *automation,
                 "Native MIDI portamento cannot preserve the transition's exact sampled pitch curve");
    }

    const u64 startTick = automation->realization.startTick;
    if (startTick >= chain->endTick) {
      continue;
    }
    if (startTick <= chain->startTick && transition.previousNote) {
      if (auto* previous = findChain(chains, *transition.previousNote);
          previous != nullptr && !previous->segments.empty()) {
        auto& segment = previous->segments.back();
        const u64 overlapEnd = startTick > std::numeric_limits<u64>::max() - native.overlapTicks
                                   ? std::numeric_limits<u64>::max()
                                   : startTick + native.overlapTicks;
        segment.endTick = std::max(segment.endTick, overlapEnd);
      }
    }
    splitForPortamento(*chain, *automation, transition, startTick, native.overlapTicks);

    if (native.emitTime) {
      events.emplace_back(PortamentoPerformanceEvent{
          .header = atTick(automation->header, startTick, nextSequence),
          .timeMilliseconds = native.timeMilliseconds,
          .previousKey = transition.startKey,
      });
    } else {
      events.emplace_back(PortamentoControlPerformanceEvent{
          .header = atTick(automation->header, startTick, nextSequence),
          .previousKey = transition.startKey,
      });
    }
    if (native.restoreTimeMilliseconds) {
      events.emplace_back(PortamentoPerformanceEvent{
          .header = atTick(automation->header, chain->endTick, nextSequence),
          .timeMilliseconds = *native.restoreTimeMilliseconds,
      });
    }
  }

  for (const auto& chain : chains) {
    if (!containsNote(involvedNotes, chain.note)) {
      continue;
    }
    for (const auto& segment : chain.segments) {
      if (segment.endTick <= segment.startTick) {
        continue;
      }
      auto header = segment.header;
      header.tick = segment.startTick;
      header.sequence = nextSequence++;
      events.emplace_back(NotePerformanceEvent{
          .header = header,
          .key = segment.key,
          .linearVelocity = segment.linearVelocity,
          .durationTicks =
              static_cast<u32>(std::min<u64>(segment.endTick - segment.startTick, std::numeric_limits<u32>::max())),
          .note = segment.note,
          .lane = segment.lane,
      });
    }
  }

  std::ranges::stable_sort(events, [](const PerformanceEvent& lhs, const PerformanceEvent& rhs) {
    const auto& left = performanceEventHeader(lhs);
    const auto& right = performanceEventHeader(rhs);
    return std::tie(left.tick, left.sequence) < std::tie(right.tick, right.sequence);
  });
  track.events = std::move(events);
  std::erase_if(track.automations,
                [](const PerformanceAutomation& automation) { return pitchTransitionIntent(automation) != nullptr; });
}

}  // namespace

PerformanceSequence lowerMidiPerformanceAutomation(const PerformanceSequence& performance,
                                                   const MidiExportOptions& options) {
  PerformanceSequence lowered = performance;
  for (auto& track : lowered.tracks) {
    lowerTrackAutomations(lowered, track, options);
  }
  return lowered;
}

}  // namespace vgmtrans::core
