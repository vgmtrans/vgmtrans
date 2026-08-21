/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/EnvelopeModel.h"
#include "value/model/InstrumentIdentity.h"
#include "value/model/ModulationModel.h"
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
  // Some hardware stops a voice after a fixed real-time counter even when the
  // sequence gate remains open. Renderers clamp durationTicks to this limit.
  std::optional<double> maximumDurationMilliseconds;
  // Extend the previously emitted voice at the same pitch without another
  // attack. Key-changing continuations are pitch transitions linked with
  // continueFrom(previousNote); target-specific lowering may then use this
  // flag for the resulting same-voice MIDI representation.
  bool extendsPrevious = false;
  // Native portamento may need another MIDI note to continue a source voice.
  // This distinguishes that synthetic note from a genuine envelope restart.
  bool restartsEnvelope = true;
  // A fresh attack may override the track's selected instrument. Performance
  // preparation uses this for generated presets; tied continuations inherit
  // the sounding voice.
  std::optional<InstrumentAddress> instrumentAddress;
  // Source voices normally restart their LFOs on a fresh attack, but some
  // drivers can disable that reset or suppress it for legato notes.
  bool restartsLfoPhase = true;
  // Target-specific overrides preserve formats where pitch and amplitude LFOs
  // have independent note-reset rules. Absent overrides retain the legacy
  // combined restartsLfoPhase behavior.
  std::optional<bool> restartsVibratoLfoPhase;
  std::optional<bool> restartsTremoloLfoPhase;
  // Source-level extensions normally share one identity. A stable identity
  // lets later commands attach automation to a sounding note without rewriting
  // that note into MIDI-specific fragments.
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

enum class InstrumentEnvelopeMode : u8 {
  UseInstrumentEnvelope,
  PreserveDynamicOverride,
};

struct InstrumentPerformanceEvent {
  PerformanceEventHeader header;
  // Direct selections use the same logical preset bank as InstrumentAddress.
  // MIDI bank packing belongs exclusively to MIDI lowering. Semantic formats
  // may instead set sourceInstrument and leave address resolution to export.
  u32 bank = 0;
  u32 program = 0;
  bool forceBankSelect = false;
  std::optional<InstrumentIdentity> sourceInstrument;
  // Instrument selection governs future attacks. Drivers that retain their
  // dynamic ADSR state across a selection opt out explicitly.
  InstrumentEnvelopeMode envelopeMode = InstrumentEnvelopeMode::UseInstrumentEnvelope;
};

struct EnvelopePerformanceEvent {
  PerformanceEventHeader header;
  EnvelopeUpdate update;
  VoiceEnvelopeScope scope = VoiceEnvelopeScope::FutureAttacks;
  PerformanceLaneId lane{0};
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
  // Legacy destination-shaped hint for older cursor formats. Semantic formats use
  // sourceQuantization, and export options may override either one.
  LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit;
  std::optional<ValueQuantization> sourceQuantization;
};

struct ExpressionPerformanceEvent {
  PerformanceEventHeader header;
  // Interpreted expression as linear amplitude/gain, not a MIDI controller value.
  double linearGain = 1.0;
  // Legacy destination-shaped hint for older cursor formats. Semantic formats use
  // sourceQuantization, and export options may override either one.
  LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit;
  std::optional<ValueQuantization> sourceQuantization;
};

struct PanPerformanceEvent {
  PerformanceEventHeader header;
  // -1.0 is hard left, 0.0 is center, and 1.0 is hard right.
  double stereoPosition = 0.0;
  // Resolved from the source program's driver behavior when emitted. Keeping
  // it on the event makes the performance IR self-contained.
  PanLaw law = PanLaw::Unspecified;
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
  // Song-wide DSP writes use this mask; absent means the event is track-local.
  std::optional<u8> voiceMask;
  double send = 0.0;
  // Source DSP state retained beyond the portable MIDI wet-send approximation.
  std::optional<double> leftGain;
  std::optional<double> rightGain;
  std::optional<double> delayMilliseconds;
  std::optional<double> feedback;
  std::optional<u8> filterIndex;
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
  // A source pitch wheel may rely on the selected instrument for its range.
  // Collection-aware lowerers use this position instead of the fallback above.
  std::optional<double> normalizedWheelPosition;
};

struct PitchBendRangePerformanceEvent {
  PerformanceEventHeader header;
  u16 cents = 200;
};

// Controls when a new vibrato or tremolo delay takes effect.
enum class LfoDelayUpdateMode {
  // Apply the new delay to the LFO already playing and to later notes that
  // restart it. Keep the amount of delay time that has already elapsed.
  CurrentAndFutureNotes,
  // Leave the LFO already playing unchanged. Use the new delay when a later
  // note restarts it.
  FutureNotesOnly,
};

struct VibratoDelayPerformanceEvent {
  PerformanceEventHeader header;
  // Delay in rendered sequence ticks, used when vibrato is simulated as pitch bend.
  u32 delayTicks = 0;
  // Some source LFOs run on a fixed driver clock rather than sequence time.
  // When present, simulation uses this physical duration instead of delayTicks.
  std::optional<double> milliseconds;
  // Sequence-relative delays remain exact in event simulation. The shared
  // resolver also supplies milliseconds for synth-modulator lowering.
  bool tempoRelative = false;
  // Controls whether this delay affects the LFO already playing or only later
  // notes that restart it.
  LfoDelayUpdateMode updateMode = LfoDelayUpdateMode::CurrentAndFutureNotes;
  // Legacy controller fallback for formats that do not provide milliseconds.
  u8 midiValue = 0;
};

struct TremoloDelayPerformanceEvent {
  PerformanceEventHeader header;
  u32 delayTicks = 0;
  std::optional<double> milliseconds;
  bool tempoRelative = false;
  // Controls whether this delay affects the LFO already playing or only later
  // notes that restart it.
  LfoDelayUpdateMode updateMode = LfoDelayUpdateMode::CurrentAndFutureNotes;
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
  PanDepth,
  PanRate,
};

// Defines the repeating shape of a vibrato, tremolo, or pan LFO. Most formats
// need only a standard waveform. Formats with a lookup table can also provide
// its exact samples; playback then uses the samples, while simpler exports use
// the standard waveform as the closest available shape.
struct LfoShape {
  // The ordinary sine, triangle, or other named waveform for this shape.
  // Playback uses it when samples is empty. Exports that cannot reproduce an
  // exact sample table use it as the closest available shape.
  LfoWaveform waveform = LfoWaveform::Triangle;
  // Each number is the LFO output at one step of a complete cycle, from -1 to
  // +1. Playback gives every step the same duration and uses these numbers
  // directly, so they must already include any one-sided or reversed motion.
  // An empty vector means to calculate the named waveform above instead.
  std::vector<double> samples;
};

// Controls how an LFO restarts when a rate or depth event occurs during
// playback. "Phase" means the current position within the repeating waveform.
enum class LfoRestartMode {
  // Continue from the current waveform position and current delay progress.
  None,
  // Jump to the waveform's starting position, but keep the current delay
  // progress.
  Phase,
  // Jump to the waveform's starting position and begin its delay again from
  // zero.
  PhaseAndDelay,
};

// Controls what happens to the pitch or volume change currently being heard
// when an event sets vibrato or tremolo depth to zero.
enum class LfoZeroDepthBehavior {
  // Cancel the current pitch or volume change immediately.
  CenterOutput,
  // Keep the current pitch or volume change until the next note, then cancel
  // it. The next note does not restart the LFO unless its normal restart rules
  // also say to do so.
  HoldOutputUntilNextNote,
};

// Describes how a format's vibrato, tremolo, or pan LFO behaves during
// playback. Format code supplies this alongside depth and rate values so the
// playback and export code can reproduce the source waveform and timing.
struct LfoPerformanceContext {
  std::optional<double> frequencyHz;
  // Oscillator cycles advanced by one sequence tick. The shared resolver
  // derives frequencyHz from the global tempo timeline.
  std::optional<double> cyclesPerTick;
  std::optional<u32> delayTicks;
  std::optional<double> delayMilliseconds;
  bool delayIsTempoRelative = false;
  // Replaces the current LFO shape, including both its standard waveform and
  // its exact sample table. A missing value leaves the current shape unchanged.
  std::optional<LfoShape> shape;
  // Maps the oscillator's normal -1..+1 output to 0..+1 or -1..0.
  // A missing value retains the ordinary bipolar range.
  std::optional<LfoPolarity> polarity;
  // Sets the position within the waveform where the LFO begins. Zero is the
  // start of the cycle and 0.5 is halfway through it.
  std::optional<double> initialPhaseCycles;
  // Sets a different starting position when a note restarts the LFO. When this
  // is absent, note restarts use initialPhaseCycles instead.
  std::optional<double> noteRestartInitialPhaseCycles;
  // Optional asymmetric pitch endpoints for one normalized LFO cycle. The
  // ordinary pitchDepthSemitones value remains the maximum absolute excursion
  // used by target-neutral modulation planning.
  std::optional<ModulationRange> pitchRangeSemitones;
  // A stepped attack starts at 1/N depth and advances one step after each
  // complete oscillator cycle until it reaches full depth.
  std::optional<u32> steppedDepthAttackSteps;
  // Calculate the first oscillator sample on a fresh note instead of waiting
  // for the first rendered sequence-tick boundary.
  bool sampleImmediatelyOnNote = false;
  // Reverse the oscillator's phase advance after this many active source
  // ticks. Folded accumulator LFOs use this to alternate sawtooth direction.
  std::optional<u32> directionReversalTicks;
  // Controls whether this delay affects the LFO already playing or only later
  // notes that restart it.
  LfoDelayUpdateMode delayUpdateMode = LfoDelayUpdateMode::CurrentAndFutureNotes;
  // Controls whether this event continues the LFO, moves it back to the start
  // of its waveform, or also begins its delay again.
  LfoRestartMode restartMode = LfoRestartMode::None;
  bool phaseRunsAtZeroDepth = false;
  // Some register-driven oscillators also pause their delay counter while
  // either rate or depth is zero. The default keeps the existing behavior.
  bool delayRunsWhileInactive = true;
  // Controls whether setting depth to zero cancels the current pitch or volume
  // change immediately or leaves it in place until the next note.
  LfoZeroDepthBehavior zeroDepthBehavior = LfoZeroDepthBehavior::CenterOutput;
  TremoloGainMode tremoloGainMode = TremoloGainMode::BipolarAroundNominal;
  // Pan LFOs move through the source engine's pan law. MIDI simulation uses
  // this to retain loudness when its equal-power controller law differs.
  PanLaw panLaw = PanLaw::Unspecified;
};

struct ModulationPerformanceEvent {
  PerformanceEventHeader header;
  ModulationPerformanceTarget target = ModulationPerformanceTarget::VibratoDepth;
  // Legacy normalized fallback for formats that do not provide a physical value.
  double amount = 0.0;
  // The shared modulation planner derives both MIDI controls and synth
  // modulation from these physical values.
  std::optional<double> pitchDepthSemitones;
  std::optional<double> volumeDepthDecibels;
  // Maximum linear-gain excursion for hardware that multiplies nominal
  // amplitude by 1 + depth * LFO. Unlike decibels, this exactly preserves an
  // asymmetric pair such as 0.25x/1.75x around nominal gain.
  std::optional<double> volumeDepthLinearGain;
  std::optional<double> panDepth;
  LfoPerformanceContext context;
};

struct MarkerPerformanceEvent {
  PerformanceEventHeader header;
  std::string text;
};

using PerformanceEvent =
    std::variant<NotePerformanceEvent, TempoPerformanceEvent, TimeSignaturePerformanceEvent, InstrumentPerformanceEvent,
                 EnvelopePerformanceEvent, LevelPerformanceEvent, ExpressionPerformanceEvent, PanPerformanceEvent,
                 StereoBalancePerformanceEvent, MasterLevelPerformanceEvent, ReverbPerformanceEvent,
                 MonoModePerformanceEvent, TuningPerformanceEvent, GlobalTransposePerformanceEvent,
                 PortamentoPerformanceEvent, PortamentoEnablePerformanceEvent, PortamentoTimePerformanceEvent,
                 PortamentoControlPerformanceEvent, PitchBendPerformanceEvent, PitchBendRangePerformanceEvent,
                 VibratoDelayPerformanceEvent, TremoloDelayPerformanceEvent, PitchTransitionSettingsPerformanceEvent,
                 LegatoPedalPerformanceEvent, ModulationPerformanceEvent, MarkerPerformanceEvent>;

enum class PerformanceAutomationTarget {
  Tempo,
  Level,
  MasterLevel,
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
  // This transition continues the preceding voice without a new attack.
  // Native portamento realizes that as overlapping notes; pitch bend retains
  // the already-sounding MIDI note.
  std::optional<PerformanceNoteId> previousNote;
  PerformanceLaneId lane{0};
  double startKey = 0.0;
  double targetKey = 0.0;
  PitchSlideTiming timing;
  PerformanceAutomationCurve curve = LinearAutomationCurve{};
  // Empty inherits the format default; explicit export policy can still
  // override every transition.
  std::optional<PitchTransitionRenderingHint> preferredRendering;
  // Source playback normally replaces a slide at a new note. Formats whose
  // driver carries one live motion across note boundaries opt in explicitly.
  bool continuesAcrossNotes = false;
  NativePortamentoHint nativePortamento;
};

using PerformanceAutomationIntent = std::variant<ScalarPerformanceAutomationIntent, PitchTransitionIntent>;

enum class PerformanceAutomationEndReason {
  // The motion reached its final value, which remains on the sounding voice
  // until another source event replaces it.
  Completed,
  // Another transition takes over without resetting pitch.
  Continued,
  // The source stopped the transition at realization.endTick.
  Interrupted,
};

struct PerformanceAutomationRealization {
  u64 startTick = 0;
  // The half-open end of the changing portion of the automation. The terminal
  // value remains in force until another source event replaces it.
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
  // Lets physical modulation analysis return immediately for ordinary tracks.
  bool hasPhysicalModulation = false;
  // Events are stored in chronological execution order.
  std::vector<PerformanceEvent> events;
  std::vector<PerformanceAutomation> automations;
};

// Maps an executed source command to the half-open interval during which it is
// active in the rendered sequence. This includes commands such as rests and
// control flow that do not emit a PerformanceEvent. Spans are kept in scheduled
// execution order, with nondecreasing begin ticks.
struct SourcePlaybackSpan {
  SourceAnnotationId annotation;
  // Identifies the source channel for commands decoded from an interleaved
  // stream. Track-based formats leave this empty and use annotation ownership.
  std::optional<u32> channel;
  u64 beginTick = 0;
  u64 endTick = 0;

  friend bool operator==(const SourcePlaybackSpan&, const SourcePlaybackSpan&) noexcept = default;
};

// Output from SequenceVm. Events point back to the source command that produced
// them, while sourceSpans preserve runtime timing for every executed annotated
// command. MIDI controller encoding happens later in the export layer.
struct PerformanceSequence {
  Timebase timebase;
  u32 initialTempoMicrosecondsPerQuarter = 500000;
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
  struct Point {
    u64 tick = 0;
    u32 microsecondsPerQuarter = 500000;
  };

  explicit PerformanceTempoMap(const PerformanceSequence& performance);

  [[nodiscard]] u32 microsecondsPerQuarterAt(u64 tick) const;
  [[nodiscard]] double tickSeconds(u64 tick) const;
  [[nodiscard]] double durationMilliseconds(u64 startTick, u32 durationTicks) const;
  [[nodiscard]] u32 durationTicksForMilliseconds(u64 startTick, double milliseconds) const;
  [[nodiscard]] bool contains(const TempoPerformanceEvent& event) const;
  [[nodiscard]] std::vector<Point> points() const;

private:
  struct Change {
    u64 tick = 0;
    u32 microsecondsPerQuarter = 500000;
    TrackId track;
    u64 sequence = 0;
    size_t order = 0;
  };

  Timebase timebase_;
  u32 initialTempoMicrosecondsPerQuarter_ = 500000;
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
