/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/SequenceProgram.h"

#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

struct PerformanceEventHeader {
  CommandId sourceCommand;
  SourceAnnotationId sourceAnnotation;
  TrackId track;
  u64 tick = 0;
};

struct NotePerformanceEvent {
  PerformanceEventHeader header;
  double key = 0.0;
  // Interpreted note loudness as linear amplitude/gain. MIDI velocity curves
  // are applied only by MIDI-like renderers.
  double linearVelocity = 1.0;
  u32 durationTicks = 0;
  // The source format already decided this note extends the previous emitted note.
  // Renderers should not re-test pitch after target-specific transforms such as global transpose.
  bool extendsPrevious = false;
};

struct TempoPerformanceEvent {
  PerformanceEventHeader header;
  u32 microsecondsPerQuarter = 500000;
};

struct InstrumentPerformanceEvent {
  PerformanceEventHeader header;
  u32 bank = 0;
  u32 program = 0;
  bool forceBankSelect = false;
};

enum class LevelPrecisionHint {
  SevenBit,
  FourteenBit,
};

struct LevelPerformanceEvent {
  PerformanceEventHeader header;
  // Interpreted loudness as linear amplitude/gain, not a MIDI controller value.
  double linearGain = 1.0;
  // Source precision hint. MIDI export options may override this without
  // changing format code.
  LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit;
};

struct ExpressionPerformanceEvent {
  PerformanceEventHeader header;
  // Interpreted expression as linear amplitude/gain, not a MIDI controller value.
  double linearGain = 1.0;
  // Source precision hint. MIDI export options may override this without
  // changing format code.
  LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit;
};

struct PanPerformanceEvent {
  PerformanceEventHeader header;
  // -1.0 is hard left, 0.0 is center, and 1.0 is hard right.
  double stereoPosition = 0.0;
  // Some source pan laws also change loudness. Keep that as neutral gain here;
  // MIDI-specific expression compensation belongs to the renderer.
  double linearGain = 1.0;
  // True means the source pan law intentionally supplied linearGain, even when it
  // is 1.0 and should reset a previous expression compensation.
  bool hasLinearGain = false;
};

struct MasterLevelPerformanceEvent {
  PerformanceEventHeader header;
  // Interpreted master loudness as linear amplitude/gain.
  double linearGain = 1.0;
};

struct ReverbPerformanceEvent {
  PerformanceEventHeader header;
  double send = 0.0;
};

struct MonoModePerformanceEvent {
  PerformanceEventHeader header;
  u8 channels = 0;
};

struct TuningPerformanceEvent {
  PerformanceEventHeader header;
  double cents = 0.0;
};

struct GlobalTransposePerformanceEvent {
  PerformanceEventHeader header;
  s32 semitones = 0;
};

struct PitchBendPerformanceEvent {
  PerformanceEventHeader header;
  // Musical bend amount. MIDI renderers quantize this using the active pitch-bend range.
  double semitones = 0.0;
};

struct PitchBendRangePerformanceEvent {
  PerformanceEventHeader header;
  u8 semitones = 2;
};

struct PortamentoPerformanceEvent {
  PerformanceEventHeader header;
  double timeMilliseconds = 0.0;
  double previousKey = 0.0;
};

struct PortamentoEnablePerformanceEvent {
  PerformanceEventHeader header;
  bool enabled = false;
};

struct PortamentoTimePerformanceEvent {
  PerformanceEventHeader header;
  // Musical glide time. MIDI renderers decide whether to write 7-bit or 14-bit controller data.
  double timeMilliseconds = 0.0;
};

struct PortamentoControlPerformanceEvent {
  PerformanceEventHeader header;
  double previousKey = 0.0;
};

struct LegatoPedalPerformanceEvent {
  PerformanceEventHeader header;
  bool enabled = false;
};

enum class ModulationPerformanceTarget {
  VibratoDepth,
  VibratoRate,
  TremoloDepth,
  TremoloRate,
};

struct ModulationPerformanceEvent {
  PerformanceEventHeader header;
  ModulationPerformanceTarget target = ModulationPerformanceTarget::VibratoDepth;
  // Normalized driver amount. MIDI and synth exporters decide how to quantize it.
  double amount = 0.0;
};

struct MarkerPerformanceEvent {
  PerformanceEventHeader header;
  std::string text;
};

using PerformanceEvent =
    std::variant<NotePerformanceEvent, TempoPerformanceEvent, InstrumentPerformanceEvent, LevelPerformanceEvent,
                 ExpressionPerformanceEvent, PanPerformanceEvent, MasterLevelPerformanceEvent, ReverbPerformanceEvent,
                 MonoModePerformanceEvent, TuningPerformanceEvent, GlobalTransposePerformanceEvent,
                 PortamentoPerformanceEvent, PortamentoEnablePerformanceEvent, PortamentoTimePerformanceEvent,
                 PortamentoControlPerformanceEvent, PitchBendPerformanceEvent, PitchBendRangePerformanceEvent,
                 LegatoPedalPerformanceEvent, ModulationPerformanceEvent, MarkerPerformanceEvent>;

struct PerformanceTrack {
  TrackId id;
  u32 sourceTrackNumber = 0;
  u64 endTick = 0;
  std::vector<PerformanceEvent> events;
};

// Output from SequenceVm. Events point back to the source command that produced
// them, but MIDI controller encoding happens later in the export layer.
struct PerformanceSequence {
  Timebase timebase;
  std::vector<PerformanceTrack> tracks;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] const PerformanceEventHeader& performanceEventHeader(const PerformanceEvent& event);
[[nodiscard]] const PerformanceTrack* performanceTrackById(const PerformanceSequence& sequence, TrackId id);
[[nodiscard]] const SourceCommand* sourceCommandForEvent(const SequenceProgram& program,
                                                         const PerformanceEventHeader& header);
[[nodiscard]] std::vector<const PerformanceEvent*> performanceEventsForCommand(const PerformanceTrack& track,
                                                                               CommandId command);

}  // namespace vgmtrans::core
