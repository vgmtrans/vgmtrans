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

[[nodiscard]] constexpr double tempoBeatsPerMinute(u32 microsecondsPerQuarter) noexcept {
  return microsecondsPerQuarter == 0 ? 0.0 : 60000000.0 / microsecondsPerQuarter;
}

struct PerformanceNoteIdTag;
using PerformanceNoteId = Id<PerformanceNoteIdTag>;

struct PerformanceAutomationIdTag;
using PerformanceAutomationId = Id<PerformanceAutomationIdTag>;

struct PerformanceLaneIdTag;
using PerformanceLaneId = Id<PerformanceLaneIdTag>;

struct PerformanceEventHeader {
  CommandId sourceCommand;
  SourceAnnotationId sourceAnnotation;
  TrackId track;
  u64 tick = 0;
  // Stable execution order disambiguates events emitted at the same tick.
  u64 sequence = 0;
  // Realized events stay in the track's single timeline. This optional
  // association preserves the higher-level motion that produced them.
  std::optional<PerformanceAutomationId> automation;
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
  // Extensions share the same identity as the note they extend. A stable
  // identity lets later commands attach automation to a sounding note without
  // rewriting that note into MIDI-specific fragments.
  PerformanceNoteId note;
  // Most sequence tracks are one driver voice and therefore use lane zero.
  // The explicit lane leaves room for formats that multiplex voices in one
  // source track.
  PerformanceLaneId lane{0};
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
  // A missing source key configures the glide time without triggering a
  // transition. A present key also identifies the pitch the next note should
  // glide from.
  std::optional<double> previousKey;
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

enum class PitchTransitionRenderingHint {
  Portamento,
  PitchBend,
};

struct PitchTransitionSettingsPerformanceEvent {
  PerformanceEventHeader header;
  // Physical glide time retained independently of its eventual MIDI
  // representation. Pitch-bend lowering consumes the setting without writing
  // a portamento controller.
  double timeMilliseconds = 0.0;
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
                 LevelPerformanceEvent, ExpressionPerformanceEvent, PanPerformanceEvent, StereoBalancePerformanceEvent,
                 MasterLevelPerformanceEvent, ReverbPerformanceEvent, MonoModePerformanceEvent, TuningPerformanceEvent,
                 GlobalTransposePerformanceEvent, PortamentoPerformanceEvent, PortamentoEnablePerformanceEvent,
                 PortamentoTimePerformanceEvent, PortamentoControlPerformanceEvent, PitchBendPerformanceEvent,
                 PitchBendRangePerformanceEvent, VibratoDelayPerformanceEvent, TremoloDelayPerformanceEvent,
                 PitchTransitionSettingsPerformanceEvent, LegatoPedalPerformanceEvent, ModulationPerformanceEvent,
                 MarkerPerformanceEvent>;

enum class PerformanceAutomationTarget {
  Tempo,
  Level,
  Expression,
  Pan,
  Pitch,
  VibratoDepth,
  TremoloDepth,
};

enum class PerformanceAutomationMotion {
  TargetOverTicks,
  TargetByStep,
  Envelope,
};

// Scalar automation retains the intent behind an exact series of realized
// performance events. Constant source-space steps are not assumed to remain
// linear after conversion to tempo, gain, or pan.
struct ScalarPerformanceAutomationIntent {
  PerformanceAutomationTarget target = PerformanceAutomationTarget::Level;
  PerformanceAutomationMotion motion = PerformanceAutomationMotion::TargetOverTicks;
  std::optional<double> targetValue;
  u32 durationTicks = 0;
  u32 delayTicks = 0;
  bool restartsOnNote = false;
};

struct LinearAutomationCurve {};

struct AutomationSample {
  // Offset from the automation's realized start tick.
  u32 tickOffset = 0;
  // Pitch automation uses absolute key-space values. Other targets use the
  // neutral domain documented by their corresponding performance event.
  double value = 0.0;

  friend constexpr bool operator==(const AutomationSample&, const AutomationSample&) noexcept = default;
};

struct SampledAutomationCurve {
  std::vector<AutomationSample> samples;
};

using PerformanceAutomationCurve = std::variant<LinearAutomationCurve, SampledAutomationCurve>;

struct TempoRelativePitchSlideTiming {};

struct FixedDurationPitchSlideTiming {
  double milliseconds = 0.0;
};

struct FixedRatePitchSlideTiming {
  double semitonesPerSecond = 0.0;
};

using PitchSlidePhysicalTiming =
    std::variant<TempoRelativePitchSlideTiming, FixedDurationPitchSlideTiming, FixedRatePitchSlideTiming>;

// Timeline ticks determine sequencing, interruption, and pitch-bend samples.
// Physical timing preserves how the source driver controls an asynchronous
// glide. Most formats are tempo-relative and need only fromTicks().
struct PitchSlideTiming {
  u32 timelineTicks = 0;
  PitchSlidePhysicalTiming physical = TempoRelativePitchSlideTiming{};

  [[nodiscard]] static constexpr PitchSlideTiming fromTicks(u32 ticks) noexcept {
    return PitchSlideTiming{.timelineTicks = ticks};
  }

  [[nodiscard]] static constexpr PitchSlideTiming fixedDuration(u32 timelineTicks, double milliseconds) noexcept {
    return PitchSlideTiming{
        .timelineTicks = timelineTicks,
        .physical = FixedDurationPitchSlideTiming{.milliseconds = milliseconds},
    };
  }

  [[nodiscard]] static constexpr PitchSlideTiming fixedRate(u32 timelineTicks, double semitonesPerSecond) noexcept {
    return PitchSlideTiming{
        .timelineTicks = timelineTicks,
        .physical = FixedRatePitchSlideTiming{.semitonesPerSecond = semitonesPerSecond},
    };
  }
};

// Native portamento cannot reproduce an arbitrary pitch curve, but it is often
// the most compatible rendering of a source driver's glide. These physical
// timing hints preserve that option without turning the transition itself into
// MIDI controller events.
struct NativePortamentoHint {
  bool useCurrentTiming = false;
  u32 overlapTicks = 1;
  std::optional<double> restoreTimeMilliseconds;
};

struct PitchTransitionIntent {
  PerformanceNoteId note;
  // When a transition begins exactly at a new note, native portamento extends
  // this preceding note by overlapTicks. Delayed transitions split note.
  std::optional<PerformanceNoteId> previousNote;
  PerformanceLaneId lane{0};
  double startKey = 0.0;
  double targetKey = 0.0;
  PitchSlideTiming timing;
  PerformanceAutomationCurve curve = LinearAutomationCurve{};
  // Source playback normally replaces a slide at a new note. Formats whose
  // driver carries one live motion across note boundaries opt in explicitly.
  bool continuesAcrossNotes = false;
  NativePortamentoHint nativePortamento;
};

using PerformanceAutomationIntent = std::variant<ScalarPerformanceAutomationIntent, PitchTransitionIntent>;

enum class PerformanceAutomationEndReason {
  // The final value remains until the attached note ends.
  Completed,
  // Another transition takes over without resetting pitch.
  Continued,
  // The source stopped the transition at realization.endTick.
  Interrupted,
};

struct PerformanceAutomationRealization {
  u64 startTick = 0;
  // The half-open end of the changing portion of the automation. The terminal
  // value remains in force until another event or the attached note ends.
  u64 endTick = 0;
  PerformanceAutomationEndReason endReason = PerformanceAutomationEndReason::Completed;
};

struct PerformanceAutomation {
  PerformanceAutomationId id;
  PerformanceEventHeader header;
  PerformanceAutomationIntent intent;
  PerformanceAutomationRealization realization;
};

struct PerformanceTrack {
  TrackId id;
  u32 sourceTrackNumber = 0;
  u64 endTick = 0;
  std::vector<PerformanceEvent> events;
  std::vector<PerformanceAutomation> automations;
};

// Maps an executed source command to the half-open interval during which it is
// active in the rendered sequence. This includes commands such as rests and
// control flow that do not emit a PerformanceEvent. Spans are kept in scheduled
// execution order, with nondecreasing begin ticks.
struct SourcePlaybackSpan {
  SourceAnnotationId annotation;
  u64 beginTick = 0;
  u64 endTick = 0;

  friend bool operator==(const SourcePlaybackSpan&, const SourcePlaybackSpan&) noexcept = default;
};

// Output from SequenceVm. Events point back to the source command that produced
// them, while sourceSpans preserve runtime timing for every executed annotated
// command. MIDI controller encoding happens later in the export layer.
struct PerformanceSequence {
  Timebase timebase;
  PitchTransitionRenderingHint preferredPitchTransitionRendering = PitchTransitionRenderingHint::Portamento;
  std::vector<PerformanceTrack> tracks;
  std::vector<SourcePlaybackSpan> sourceSpans;
  std::vector<Diagnostic> diagnostics;
};

// One song-wide tempo view shared by physical-time lowering and MIDI rendering.
// Changes retain event identity so redundant source tempo writes can be omitted
// after a PerformanceSequence is copied for lowering.
class PerformanceTempoMap {
public:
  explicit PerformanceTempoMap(const PerformanceSequence& performance);

  [[nodiscard]] u32 microsecondsPerQuarterAt(u64 tick) const;
  [[nodiscard]] double tickSeconds(u64 tick) const;
  [[nodiscard]] double durationMilliseconds(u64 startTick, u32 durationTicks) const;
  [[nodiscard]] bool contains(const TempoPerformanceEvent& event) const;

private:
  struct Change {
    u64 tick = 0;
    u32 microsecondsPerQuarter = 500000;
    TrackId track;
    u64 sequence = 0;
    size_t order = 0;
  };

  Timebase timebase_;
  std::vector<Change> changes_;
};

[[nodiscard]] const PerformanceEventHeader& performanceEventHeader(const PerformanceEvent& event);
[[nodiscard]] const PitchTransitionIntent* pitchTransitionIntent(const PerformanceAutomation& automation);
[[nodiscard]] PitchTransitionIntent* pitchTransitionIntent(PerformanceAutomation& automation);
[[nodiscard]] double pitchTransitionValueAt(const PitchTransitionIntent& transition, u32 elapsedTicks);
[[nodiscard]] const PerformanceTrack* performanceTrackById(const PerformanceSequence& sequence, TrackId id);
[[nodiscard]] const SourceCommand* sourceCommandForEvent(const SequenceProgram& program,
                                                         const PerformanceEventHeader& header);
[[nodiscard]] std::vector<const PerformanceEvent*> performanceEventsForCommand(const PerformanceTrack& track,
                                                                               CommandId command);

}  // namespace vgmtrans::core
