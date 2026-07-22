/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/InstrumentIdentity.h"
#include "value/sequence/SequenceProgram.h"

#include <optional>
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

struct TimeSignaturePerformanceEvent {
  PerformanceEventHeader header;
  u8 numerator = 4;
  u8 denominator = 4;
  u8 clocksPerMetronomeClick = 24;
};

struct InstrumentPerformanceEvent {
  PerformanceEventHeader header;
  // Legacy bank/program selection remains temporarily for cursor dialects.
  // Semantic formats set sourceInstrument and leave target addressing to export.
  u32 bank = 0;
  u32 program = 0;
  bool forceBankSelect = false;
  std::optional<InstrumentIdentity> sourceInstrument;
};

enum class LevelPrecisionHint {
  SevenBit,
  FourteenBit,
};

struct ValueQuantization {
  // Number of distinct source-domain values, not a destination bit width.
  // Zero means the source is continuous or its quantization is unknown.
  u32 levels = 0;
};

struct LevelPerformanceEvent {
  PerformanceEventHeader header;
  // Interpreted loudness as linear amplitude/gain, not a MIDI controller value.
  double linearGain = 1.0;
  // Legacy destination-shaped hint for cursor dialects. Semantic formats use
  // sourceQuantization, and export options may override either one.
  LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit;
  std::optional<ValueQuantization> sourceQuantization;
};

struct ExpressionPerformanceEvent {
  PerformanceEventHeader header;
  // Interpreted expression as linear amplitude/gain, not a MIDI controller value.
  double linearGain = 1.0;
  // Legacy destination-shaped hint for cursor dialects. Semantic formats use
  // sourceQuantization, and export options may override either one.
  LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit;
  std::optional<ValueQuantization> sourceQuantization;
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

struct StereoBalancePerformanceEvent {
  PerformanceEventHeader header;
  // Source-engine channel gains before any target pan-law quantization.
  double leftGain = 1.0;
  double rightGain = 1.0;
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
  u16 cents = 200;
};

struct VibratoDelayPerformanceEvent {
  PerformanceEventHeader header;
  // Delay in rendered sequence ticks, used when vibrato is simulated as pitch bend.
  u32 delayTicks = 0;
  // Controller value to write when exporting synth-style MIDI controls.
  u8 midiValue = 0;
};

struct TremoloDelayPerformanceEvent {
  PerformanceEventHeader header;
  u32 delayTicks = 0;
  u8 midiValue = 0;
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
  // Optional physical values used by sequence-event simulation when the source
  // scanner can provide them. Generic controller export continues to use amount.
  std::optional<double> pitchDepthSemitones;
  std::optional<double> frequencyHz;
  // Optional controller scaling ceiling, normalized to the same full range as amount.
  // Formats with sequence-derived modulation ranges can use this so MIDI controller
  // scaling and synth modulator scaling share the same denominator even when the
  // rendered event stream does not hit the maximum.
  std::optional<double> controllerRangeMaxAmount;
  bool controllerRangeOnly = false;
};

struct MarkerPerformanceEvent {
  PerformanceEventHeader header;
  std::string text;
};

using PerformanceEvent =
    std::variant<NotePerformanceEvent, TempoPerformanceEvent, TimeSignaturePerformanceEvent, InstrumentPerformanceEvent,
                 LevelPerformanceEvent, ExpressionPerformanceEvent, PanPerformanceEvent,
                 StereoBalancePerformanceEvent, MasterLevelPerformanceEvent, ReverbPerformanceEvent,
                 MonoModePerformanceEvent, TuningPerformanceEvent,
                 GlobalTransposePerformanceEvent, PortamentoPerformanceEvent, PortamentoEnablePerformanceEvent,
                 PortamentoTimePerformanceEvent, PortamentoControlPerformanceEvent, PitchBendPerformanceEvent,
                 PitchBendRangePerformanceEvent, VibratoDelayPerformanceEvent, TremoloDelayPerformanceEvent,
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
