/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/sequence/SequenceDialect.h"

#include <any>
#include <utility>

namespace vgmtrans::core {

class SequenceVm;

namespace detail {
class RepeatState;
struct VmApiAccess;
struct VmTrackRuntime;
[[nodiscard]] std::any analyzeSequenceProgram(const SequenceVm& vm, const SequenceProgram& program,
                                              const SequenceDialect& dialect);
}  // namespace detail

struct BranchResult {
  bool taken = false;
  Effects effects;
};

class PerformanceAutomationBinding;
class PitchSlideBinding;

class RepeatCounter {
public:
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool firstVisit() const;
  [[nodiscard]] u32 remainingPlays() const;
  void start(u32 totalPlays);
  [[nodiscard]] bool consumeReplay();
  void finish();

private:
  friend class VmApi;

  RepeatCounter(detail::RepeatState& state, u8 slot) noexcept;

  detail::RepeatState* state_ = nullptr;
  u8 slot_ = 0;
};

// Commands call PerformanceEmitter to add notes, tempo changes, controller changes, and markers.
// PerformanceEmitter fills in the current tick and source command automatically.
class PerformanceEmitter {
public:
  PerformanceEmitter(PerformanceTrack& track, CommandId sourceCommand, SourceAnnotationId sourceAnnotation, u64 tick,
                     u64& nextSequence, u32& nextNote, u32& nextAutomation, PanLaw panLaw = PanLaw::Unspecified);

  [[nodiscard]] PerformanceEmitter at(u64 tick) const;
  PerformanceNoteId note(NotePerformanceEvent event);
  PerformanceNoteId note(double key, double linearVelocity, u32 durationTicks, bool extendsPrevious = false);
  // Formats whose slide command follows its note can revise the most recently
  // emitted note chain once the delayed transition point becomes known.
  [[nodiscard]] bool setPreviousNoteEnd(u64 endTick);
  void tempo(TempoPerformanceEvent event);
  void tempo(u32 microsecondsPerQuarter);
  void timeSignature(TimeSignaturePerformanceEvent event);
  void timeSignature(u8 numerator, u8 denominator, u8 clocksPerMetronomeClick);
  void instrument(InstrumentPerformanceEvent event);
  void instrument(InstrumentIdentity sourceInstrument);
  void instrument(u32 bank, u32 program, bool forceBankSelect = false);
  void level(LevelPerformanceEvent event);
  void level(double linearGain, ValueQuantization sourceQuantization);
  void level(double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit);
  void expression(ExpressionPerformanceEvent event);
  void expression(double linearGain, ValueQuantization sourceQuantization);
  void expression(double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit);
  void pan(PanPerformanceEvent event);
  void pan(double stereoPosition);
  void pan(double stereoPosition, double linearGain);
  void stereoBalance(StereoBalancePerformanceEvent event);
  void stereoBalance(double leftGain, double rightGain);
  void masterLevel(MasterLevelPerformanceEvent event);
  void masterLevel(double linearGain);
  void reverb(ReverbPerformanceEvent event);
  void reverb(double send);
  void tuning(TuningPerformanceEvent event);
  void tuning(double cents);
  void globalTranspose(GlobalTransposePerformanceEvent event);
  void globalTranspose(s32 semitones);
  void pitchBend(PitchBendPerformanceEvent event);
  void pitchBend(double semitones);
  void pitchBendRange(PitchBendRangePerformanceEvent event);
  void pitchBendRange(u8 semitones);
  void vibratoDelay(VibratoDelayPerformanceEvent event);
  void vibratoDelay(u32 delayTicks, u8 midiValue);
  void vibratoDelayTicks(u32 delayTicks);
  void vibratoDelayPhysical(u32 delayTicks, double milliseconds);
  void tremoloDelay(TremoloDelayPerformanceEvent event);
  void tremoloDelay(u32 delayTicks, u8 midiValue);
  void tremoloDelayTicks(u32 delayTicks);
  void tremoloDelayPhysical(u32 delayTicks, double milliseconds);
  void portamento(PortamentoPerformanceEvent event);
  void portamento(double timeMilliseconds, double previousKey);
  void portamentoEnable(PortamentoEnablePerformanceEvent event);
  void portamentoEnable(bool enabled);
  void portamentoTime(PortamentoTimePerformanceEvent event);
  void portamentoTime(double timeMilliseconds);
  void portamentoControl(PortamentoControlPerformanceEvent event);
  void portamentoControl(double previousKey);
  void pitchTransitionSettings(PitchTransitionSettingsPerformanceEvent event);
  void pitchTransitionSettings(double timeMilliseconds);
  void legatoPedal(LegatoPedalPerformanceEvent event);
  void legatoPedal(bool enabled);
  void modulation(ModulationPerformanceEvent event);
  void modulation(ModulationPerformanceTarget target, double amount);
  void vibratoDepth(double semitones, LfoPerformanceContext context = {});
  void vibratoRate(double hertz, LfoPerformanceContext context = {});
  void vibratoRateCyclesPerTick(double cycles, LfoPerformanceContext context = {});
  void tremoloDepth(double decibels, LfoPerformanceContext context = {});
  void tremoloRate(double hertz, LfoPerformanceContext context = {});
  void tremoloRateCyclesPerTick(double cycles, LfoPerformanceContext context = {});
  void panLfoDepth(double depth, LfoPerformanceContext context = {});
  void panLfoRate(double hertz, LfoPerformanceContext context = {});
  void panLfoRateCyclesPerTick(double cycles, LfoPerformanceContext context = {});
  void marker(MarkerPerformanceEvent event);
  void appendEvents(std::vector<PerformanceEvent> events);

  // Declares a note-anchored transition between absolute keys, where 60.0 is
  // middle C and 60.5 is halfway to C-sharp. Unlike fade(Pitch, ...), this
  // represents a musical glide that may be lowered as pitch bend or native
  // portamento. The glide may occur within one note or cross a note boundary;
  // continueFrom(previousNote) means it continues the previous note without
  // retriggering the instrument's attack. A zero duration is an immediate
  // attack-free key change.
  PitchSlideBinding pitchSlide(PerformanceNoteId note, double startKey, double targetKey, u32 durationTicks,
                               PerformanceLaneId lane = PerformanceLaneId{0});
  PitchSlideBinding pitchSlide(PerformanceNoteId note, double startKey, double targetKey, PitchSlideTiming timing,
                               PerformanceLaneId lane = PerformanceLaneId{0});

  [[nodiscard]] PerformanceAutomationBinding fade(PerformanceAutomationTarget target, double targetValue,
                                                  u32 durationTicks, u32 delayTicks = 0);
  [[nodiscard]] PerformanceAutomationBinding noteFade(PerformanceAutomationTarget target, double targetValue,
                                                      u32 durationTicks, u32 delayTicks = 0);
  [[nodiscard]] PerformanceAutomationBinding step(PerformanceAutomationTarget target, double targetValue,
                                                  u32 durationTicks = 0, u32 delayTicks = 0);
  [[nodiscard]] PerformanceAutomationBinding noteEnvelope(PerformanceAutomationTarget target, double targetValue,
                                                          u32 durationTicks, u32 delayTicks = 0);

private:
  friend class PerformanceAutomationBinding;

  [[nodiscard]] PerformanceAutomationBinding beginAutomation(ScalarPerformanceAutomationIntent intent);
  [[nodiscard]] PerformanceEmitter withAutomation(const PerformanceAutomationBinding& automation) const;
  [[nodiscard]] PerformanceEventHeader header();
  void append(PerformanceEvent event);
  void automationSample(u32 automation, double value);
  void stopAutomation(u32 automation);
  void interruptPitchSlidesForNewNote(PerformanceLaneId lane);

  PerformanceTrack& track_;
  CommandId sourceCommand_;
  SourceAnnotationId sourceAnnotation_;
  u64 tick_ = 0;
  u64& nextSequence_;
  u32& nextNote_;
  u32& nextAutomation_;
  PanLaw panLaw_ = PanLaw::Unspecified;
  std::optional<u32> automation_;
};

// Opaque association between a source motion object and its structured
// performance automation. Formats retain this small handle, never an IR
// container or pointer into one.
class PerformanceAutomationBinding {
public:
  PerformanceAutomationBinding() = default;

  void clear() noexcept {
    owner_ = nullptr;
    automation_ = 0;
  }
  [[nodiscard]] PerformanceEmitter output(const PerformanceEmitter& out) const { return out.withAutomation(*this); }
  [[nodiscard]] PerformanceEmitter at(const PerformanceEmitter& out, u64 tick) const {
    return out.at(tick).withAutomation(*this);
  }
  void stop(const PerformanceEmitter& out) const;
  void sample(const PerformanceEmitter& out, double value) const;

private:
  friend class PerformanceEmitter;
  friend class PitchSlideBinding;

  PerformanceAutomationBinding(PerformanceTrack& owner, u32 automation) : owner_(&owner), automation_(automation) {}

  PerformanceTrack* owner_ = nullptr;
  u32 automation_ = 0;
};

// The common pitchSlide() call declares only musical intent. This small handle
// adds the source driver's unusual behavior without exposing the stored IR
// representation to format code.
class PitchSlideBinding : public PerformanceAutomationBinding {
public:
  PitchSlideBinding() = default;

  PitchSlideBinding& continueFrom(PerformanceNoteId previousNote);
  PitchSlideBinding& continueAcrossNotes(bool enabled = true);
  PitchSlideBinding& preferPortamento();
  PitchSlideBinding& preferPitchBend();

  // Retains portamento time already established by an earlier driver command.
  PitchSlideBinding& useCurrentPortamentoTiming();
  // Reinstates a persistent driver setting after a temporary native slide.
  PitchSlideBinding& restorePortamentoTiming(double timeMilliseconds);
  PitchSlideBinding& portamentoOverlap(u32 ticks);

private:
  friend class PerformanceEmitter;

  PitchSlideBinding(PerformanceTrack& owner, u32 automation) : PerformanceAutomationBinding(owner, automation) {}

  [[nodiscard]] PitchTransitionIntent* intent() const;
};

// Adds an output binding to an existing sequence-motion type without coupling
// the arithmetic type itself to PerformanceSequence.
template <class Motion>
class PerformanceBoundMotion : public Motion {
public:
  using Motion::begin;

  void bind(PerformanceAutomationBinding binding) { binding_ = std::move(binding); }

  template <class Plan>
  decltype(auto) begin(PerformanceAutomationBinding binding, const Plan& plan) {
    bind(std::move(binding));
    return Motion::begin(plan);
  }

  void clearAutomation() noexcept { binding_.clear(); }
  [[nodiscard]] PerformanceEmitter output(const PerformanceEmitter& out) const { return binding_.output(out); }

private:
  PerformanceAutomationBinding binding_;
};

// Commands use VmApi for playback flow that is shared across formats: next, end,
// jump, call, return, repeat handling, diagnostics, and current tick.
class VmApi {
public:
  [[nodiscard]] Step next() const noexcept;
  [[nodiscard]] Step end() const noexcept;
  [[nodiscard]] Step endSection() const noexcept;
  [[nodiscard]] Step jump(Address destination) const noexcept;
  [[nodiscard]] Step finiteBranch(Address destination) const noexcept;
  [[nodiscard]] Step loopCandidate(Address destination) const noexcept;
  [[nodiscard]] Step declaredLoop(Address destination) const noexcept;
  [[nodiscard]] Step call(Address destination) const noexcept;
  [[nodiscard]] Step return_() const noexcept;

  // Formats can manage repeat counters directly when their driver does not fit
  // the counted-repeat helpers below.
  [[nodiscard]] RepeatCounter repeatCounter(u8 slot);

  // Counted-repeat helpers cover drivers where the first encounter counts as
  // one play and a repeat command jumps back to a decoded source block.
  [[nodiscard]] Effects countedRepeatUntil(u8 slot, u32 totalPlays, Address destination);
  [[nodiscard]] BranchResult countedRepeatBreak(u8 slot, Address destination);

  [[nodiscard]] u64 tick() const noexcept;
  [[nodiscard]] const PerformanceSequence& sequence() const noexcept;
  void diagnostic(Diagnostic diagnostic);

private:
  friend struct detail::VmApiAccess;

  VmApi(detail::VmTrackRuntime& runtime, PerformanceSequence& sequence, const SourceCommand& command);

  detail::VmTrackRuntime& runtime_;
  PerformanceSequence& sequence_;
  const SourceCommand& command_;
};

struct SequenceVmOptions {
  LoopPolicy loopPolicy = LoopPolicy::Default;
  // Extra runtime loop repeats after the first pass through an infinite loop.
  u32 sequenceLoops = 0;
};

// SequenceVm turns a parsed source-driver program into target-neutral performance events.
// Legacy dialects are walked track-by-track. Semantic dialects are globally
// scheduled by (tick, stable track order), which matches multi-channel driver
// execution and gives them one program-wide runtime state.
// MIDI or other exporters consume the resulting PerformanceSequence later.
class SequenceVm {
public:
  SequenceVm() = default;
  explicit SequenceVm(LoopPolicy loopPolicy);
  explicit SequenceVm(SequenceVmOptions options);

  [[nodiscard]] PerformanceSequence render(const SequenceProgram& program, const SequenceDialect& dialect) const;

private:
  friend std::any detail::analyzeSequenceProgram(const SequenceVm&, const SequenceProgram&, const SequenceDialect&);

  [[nodiscard]] PerformanceSequence renderImpl(const SequenceProgram& program, const SequenceDialect& dialect,
                                               std::any* analyzedProgramState) const;
  [[nodiscard]] SequenceProgramBehavior resolvedBehavior(const SequenceProgram& program,
                                                         const SequenceDialect& dialect) const;

  SequenceVmOptions options_;
};

}  // namespace vgmtrans::core
