/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

enum class SeqMotionStatus {
  Inactive,
  Delayed,
  Running,
  Finished,
};

enum class ControllerMotionMode {
  ToTarget,
  ByStep,
};

// Format commands usually describe controller motion as either "reach this target over N ticks"
// or "move by this fixed step until the target is crossed"; lanes handle the tick mechanics.
template <typename ValueType>
struct ControllerMotionSpec {
  ValueType target {};
  ValueType delta {};
  uint32_t length = 0;
  ControllerMotionMode mode = ControllerMotionMode::ToTarget;

  // Target/length commands compute delta from the current lane value when motion starts.
  static ControllerMotionSpec toTarget(ValueType targetValue, uint32_t ticks) {
    return {targetValue, {}, ticks, ControllerMotionMode::ToTarget};
  }

  // Step commands preserve the format-provided delta and run until the target is crossed.
  static ControllerMotionSpec byStep(ValueType targetValue, ValueType step) {
    return {targetValue, step, 0, ControllerMotionMode::ByStep};
  }

  [[nodiscard]] bool usesLength() const { return mode == ControllerMotionMode::ToTarget; }
};

// Tick-level linear motion shared by controller, pitch, and LFO automation lanes.
// It models either a fixed-length motion that snaps to the target on the final tick,
// or a step-until-target motion for drivers that encode speed instead of duration.
template <typename ValueType>
class SeqLinearMotion {
 public:
  void reset(ValueType current = {}) {
    m_current = current;
    m_target = current;
    m_delta = {};
    m_delay = 0;
    m_ticksRemaining = 0;
    m_useLength = true;
  }

  void setCurrent(ValueType current) {
    m_current = current;
    clear();
  }

  void setCurrentValue(ValueType current) {
    m_current = current;
  }

  void clear() {
    m_target = m_current;
    m_delta = {};
    m_delay = 0;
    m_ticksRemaining = 0;
    m_useLength = true;
  }

  void startToTarget(ValueType target, ValueType delta, uint32_t length, uint32_t delay = 0) {
    m_target = target;
    m_delta = delta;
    m_delay = delay;
    m_ticksRemaining = length;
    m_useLength = true;
  }

  void startByStep(ValueType target, ValueType delta, uint32_t delay = 0) {
    m_target = target;
    m_delta = delta;
    m_delay = delay;
    m_ticksRemaining = 0;
    m_useLength = false;
  }

  [[nodiscard]] bool active() const {
    return m_delay != 0 || (m_useLength ? m_ticksRemaining != 0 : m_delta != ValueType {});
  }

  [[nodiscard]] ValueType current() const { return m_current; }
  [[nodiscard]] ValueType target() const { return m_target; }
  [[nodiscard]] ValueType delta() const { return m_delta; }
  [[nodiscard]] uint32_t delayRemaining() const { return m_delay; }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_ticksRemaining; }
  [[nodiscard]] bool usesLength() const { return m_useLength; }

  // One sequencer tick of motion. Delays are consumed before applying a value change; fixed-length
  // motion snaps to the target on the final tick, while step motion finishes after crossing target.
  SeqMotionStatus advance() {
    if (m_delay != 0) {
      m_delay -= 1;
      return SeqMotionStatus::Delayed;
    }

    if (m_useLength) {
      if (m_ticksRemaining == 0) {
        return SeqMotionStatus::Inactive;
      }

      m_ticksRemaining -= 1;
      if (m_ticksRemaining == 0) {
        m_current = m_target;
        return SeqMotionStatus::Finished;
      }

      m_current = static_cast<ValueType>(m_current + m_delta);
      return SeqMotionStatus::Running;
    }

    if (m_delta == ValueType {}) {
      return SeqMotionStatus::Inactive;
    }

    m_current = static_cast<ValueType>(m_current + m_delta);
    if ((m_delta > ValueType {} && m_current >= m_target) ||
        (m_delta < ValueType {} && m_current <= m_target)) {
      m_current = m_target;
      m_delta = {};
      return SeqMotionStatus::Finished;
    }

    return SeqMotionStatus::Running;
  }

 private:
  ValueType m_current {};
  ValueType m_target {};
  ValueType m_delta {};
  uint32_t m_delay = 0;
  uint32_t m_ticksRemaining = 0;
  bool m_useLength = true;
};

template <typename ValueType>
class ControllerLane {
 public:
  void reset(ValueType current = {}) { m_motion.reset(current); }
  void setCurrent(ValueType current) { m_motion.setCurrent(current); }
  void setCurrentValue(ValueType current) { m_motion.setCurrentValue(current); }
  void clear() { m_motion.clear(); }

  void startToTarget(ValueType target, ValueType delta, uint32_t length, uint32_t delay = 0) {
    m_motion.startToTarget(target, delta, length, delay);
  }

  void startByStep(ValueType target, ValueType delta, uint32_t delay = 0) {
    m_motion.startByStep(target, delta, delay);
  }

  void startMotion(const ControllerMotionSpec<ValueType>& motion, uint32_t delay = 0) {
    if (motion.usesLength()) {
      if (motion.length == 0) {
        setCurrent(motion.target);
        return;
      }
      const auto delta = static_cast<ValueType>(
          (motion.target - current()) / static_cast<ValueType>(motion.length));
      startToTarget(motion.target, delta, motion.length, delay);
    } else {
      startByStep(motion.target, motion.delta, delay);
    }
  }

  [[nodiscard]] bool active() const { return m_motion.active(); }
  [[nodiscard]] ValueType current() const { return m_motion.current(); }
  [[nodiscard]] ValueType target() const { return m_motion.target(); }
  [[nodiscard]] ValueType delta() const { return m_motion.delta(); }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_motion.ticksRemaining(); }
  [[nodiscard]] bool usesLength() const { return m_motion.usesLength(); }

  SeqMotionStatus advance() { return m_motion.advance(); }

  template <typename Apply>
  SeqMotionStatus advanceAndApply(Apply&& apply, bool applyDelayedStep = false) {
    const SeqMotionStatus step = advance();
    if (step != SeqMotionStatus::Inactive &&
        (applyDelayedStep || step != SeqMotionStatus::Delayed)) {
      std::forward<Apply>(apply)(m_motion.current());
    }
    return step;
  }

 private:
  SeqLinearMotion<ValueType> m_motion;
};

// Controller lane for drivers that store controller fades in fixed-point units, commonly 8.8.
// Format code deals in raw values; the lane keeps fractional deltas internally between ticks.
template <typename ValueType = int32_t, unsigned FractionBits = 8>
class FixedPointControllerLane {
 public:
  static constexpr ValueType kScale = static_cast<ValueType>(1) << FractionBits;

  static constexpr ValueType toFixed(ValueType value) {
    return value << FractionBits;
  }

  static constexpr ValueType toRaw(ValueType value) {
    return value >> FractionBits;
  }

  void reset(ValueType currentRaw = {}) { m_lane.reset(toFixed(currentRaw)); }
  void setCurrent(ValueType currentRaw) { m_lane.setCurrent(toFixed(currentRaw)); }
  void setCurrentFixed(ValueType currentFixed) { m_lane.setCurrent(currentFixed); }
  void clear() { m_lane.clear(); }

  [[nodiscard]] bool active() const { return m_lane.active(); }
  [[nodiscard]] ValueType currentFixed() const { return m_lane.current(); }
  [[nodiscard]] ValueType currentRaw() const { return toRaw(m_lane.current()); }
  [[nodiscard]] ValueType targetFixed() const { return m_lane.target(); }
  [[nodiscard]] ValueType targetRaw() const { return toRaw(m_lane.target()); }
  [[nodiscard]] ValueType delta() const { return m_lane.delta(); }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_lane.ticksRemaining(); }
  [[nodiscard]] bool usesLength() const { return m_lane.usesLength(); }

  // Re-quantize through raw units before computing a new fade so successive fades do not inherit
  // stale fractional residue from the previous fixed-point motion.
  [[nodiscard]] ValueType currentRawFixed() const {
    return toFixed(currentRaw());
  }

  [[nodiscard]] ValueType deltaToTarget(ValueType targetRaw, uint32_t length) const {
    if (length == 0) {
      return {};
    }
    return static_cast<ValueType>((toFixed(targetRaw) - currentRawFixed()) /
                                  static_cast<ValueType>(length));
  }

  void startToTarget(ValueType targetRaw, uint32_t length, uint32_t delay = 0) {
    m_lane.setCurrent(currentRawFixed());
    m_lane.startToTarget(toFixed(targetRaw), deltaToTarget(targetRaw, length), length, delay);
  }

  void startToTarget(ValueType targetRaw, ValueType delta, uint32_t length, uint32_t delay = 0) {
    m_lane.setCurrent(currentRawFixed());
    m_lane.startToTarget(toFixed(targetRaw), delta, length, delay);
  }

  void startByStep(ValueType targetRaw, ValueType delta, uint32_t delay = 0) {
    m_lane.setCurrent(currentRawFixed());
    m_lane.startByStep(toFixed(targetRaw), delta, delay);
  }

  template <typename Apply>
  bool beginToTarget(ValueType targetRaw, uint32_t length, Apply&& apply) {
    // Return true when the target was applied immediately and no active motion was started.
    if (length == 0) {
      setCurrent(targetRaw);
      std::forward<Apply>(apply)(currentRaw());
      return true;
    }

    startToTarget(targetRaw, length);
    return false;
  }

  template <typename Apply>
  bool beginByStep(ValueType targetRaw, ValueType delta, Apply&& apply) {
    // Return true when the target was applied immediately and no active motion was started.
    if (delta == ValueType {}) {
      setCurrent(targetRaw);
      std::forward<Apply>(apply)(currentRaw());
      return true;
    }

    startByStep(targetRaw, delta);
    return false;
  }

  template <typename Apply>
  bool beginMotion(const ControllerMotionSpec<ValueType>& motion, Apply&& apply) {
    // Return true when the target was applied immediately and no active motion was started.
    if (motion.usesLength()) {
      return beginToTarget(motion.target, motion.length, std::forward<Apply>(apply));
    }
    return beginByStep(motion.target, motion.delta, std::forward<Apply>(apply));
  }

  SeqMotionStatus advance() { return m_lane.advance(); }

  template <typename Apply>
  SeqMotionStatus advanceAndApply(Apply&& apply, bool applyDelayedStep = false) {
    return m_lane.advanceAndApply(std::forward<Apply>(apply), applyDelayedStep);
  }

  template <typename Apply>
  SeqMotionStatus advanceAndApplyRaw(Apply&& apply, bool applyDelayedStep = false) {
    return advanceAndApply(
        [apply = std::forward<Apply>(apply)](ValueType currentFixed) mutable {
          apply(toRaw(currentFixed));
        },
        applyDelayedStep);
  }

 private:
  ControllerLane<ValueType> m_lane;
};

template <typename PitchType>
struct PitchMotionSpec {
  // Normalized pitch-slide state after format-specific parsing has already happened.
  PitchType target {};
  PitchType delta {};
  uint32_t length = 0;
  uint32_t delay = 0;
};

// Tracks logical pitch motion and converts it to MIDI pitch bend only when asked to emit.
// The lane owns bend range/current-bend caching, while SeqTrack supplies the actual MIDI writers.
// Pitch values stay in the format's native units until emission so callers can choose an
// appropriate MIDI bend range before converting the current pitch to a 14-bit bend value.
template <typename PitchType>
class PitchBendLane {
 public:
  explicit PitchBendLane(double centsPerPitchUnit = 100.0)
      : m_centsPerPitchUnit(centsPerPitchUnit) {}

  void reset(uint16_t defaultRangeCents) {
    m_baseValid = false;
    m_basePitch = {};
    m_motion.reset();
    m_pitchBendRangeCents = defaultRangeCents;
    m_currentPitchBend = 0;
  }

  void setCentsPerPitchUnit(double centsPerPitchUnit) {
    m_centsPerPitchUnit = centsPerPitchUnit;
  }

  void beginNote(PitchType basePitch) {
    m_baseValid = true;
    m_basePitch = basePitch;
    m_motion.setCurrent(basePitch);
  }

  void invalidateBase() { m_baseValid = false; }
  [[nodiscard]] bool baseValid() const { return m_baseValid; }

  void clearMotion() { m_motion.clear(); }

  void setCurrentPitch(PitchType pitch) {
    m_motion.setCurrent(pitch);
  }

  [[nodiscard]] PitchType deltaToTarget(PitchType targetPitch, uint32_t length) const {
    if (length == 0) {
      return {};
    }
    return static_cast<PitchType>((targetPitch - currentPitch()) / static_cast<PitchType>(length));
  }

  [[nodiscard]] PitchMotionSpec<PitchType> motionToTarget(PitchType targetPitch,
                                                          uint32_t length,
                                                          uint32_t delay = 0) const {
    return {targetPitch, deltaToTarget(targetPitch, length), length, delay};
  }

  bool startMotion(PitchType targetPitch, PitchType delta, uint32_t length, uint32_t delay = 0) {
    if (!m_baseValid || length == 0) {
      return false;
    }
    m_motion.startToTarget(targetPitch, delta, length, delay);
    return true;
  }

  bool startMotion(const PitchMotionSpec<PitchType>& motion) {
    return startMotion(motion.target, motion.delta, motion.length, motion.delay);
  }

  SeqMotionStatus advanceMotion() {
    if (!m_baseValid) {
      return SeqMotionStatus::Inactive;
    }
    return m_motion.advance();
  }

  template <typename EmitBend>
  SeqMotionStatus advanceAndApplyBend(EmitBend&& emitBend) {
    const SeqMotionStatus step = advanceMotion();
    if (step != SeqMotionStatus::Inactive && step != SeqMotionStatus::Delayed) {
      applyCurrentBend(std::forward<EmitBend>(emitBend));
    }
    return step;
  }

  [[nodiscard]] PitchType basePitch() const { return m_basePitch; }
  [[nodiscard]] PitchType currentPitch() const { return m_motion.current(); }
  [[nodiscard]] bool motionActive() const { return m_motion.active(); }
  [[nodiscard]] uint32_t motionTicksRemaining() const { return m_motion.ticksRemaining(); }

  [[nodiscard]] uint16_t rangeCentsForPitchSpan(double pitchUnits,
                                                uint16_t minimumRangeCents) const {
    // Pick the smallest MIDI bend range that can cover a driver-space pitch span.
    const auto cents = static_cast<uint16_t>(
        std::ceil(std::abs(pitchUnits) * m_centsPerPitchUnit));
    return std::max<uint16_t>(minimumRangeCents, cents);
  }

  [[nodiscard]] uint16_t rangeCentsForSlide(PitchType startPitch,
                                            PitchType targetPitch,
                                            uint16_t minimumRangeCents) const {
    const double startDeviation = std::abs(static_cast<double>(startPitch - m_basePitch));
    const double targetDeviation = std::abs(static_cast<double>(targetPitch - m_basePitch));
    return rangeCentsForPitchSpan(std::max(startDeviation, targetDeviation), minimumRangeCents);
  }

  [[nodiscard]] bool atRest(uint16_t defaultRangeCents) const {
    return m_pitchBendRangeCents == defaultRangeCents && m_currentPitchBend == 0;
  }

  [[nodiscard]] int16_t bendForPitch(PitchType pitch) const {
    const auto bend = static_cast<int32_t>(
        std::lround((((static_cast<double>(pitch - m_basePitch) * m_centsPerPitchUnit) /
                      m_pitchBendRangeCents) * 8192.0)));
    return static_cast<int16_t>(std::clamp(bend, kMinMidiPitchBend, kMaxMidiPitchBend));
  }

  [[nodiscard]] int16_t bendForCurrentPitch() const {
    return bendForPitch(m_motion.current());
  }

  template <typename EmitBend>
  bool setBend(int16_t bend, EmitBend&& emitBend) {
    // Cache the last emitted bend so per-tick callers can ask to apply without duplicating events.
    if (bend == m_currentPitchBend) {
      return false;
    }

    std::forward<EmitBend>(emitBend)(bend);
    m_currentPitchBend = bend;
    return true;
  }

  template <typename EmitBend>
  bool applyCurrentBend(EmitBend&& emitBend) {
    if (!m_baseValid) {
      return setBend(0, std::forward<EmitBend>(emitBend));
    }

    return setBend(bendForCurrentPitch(), std::forward<EmitBend>(emitBend));
  }

  template <typename EmitRange, typename EmitBend>
  bool setRange(uint16_t cents, EmitRange&& emitRange, EmitBend&& emitBend) {
    if (cents == 0 || cents == m_pitchBendRangeCents) {
      return false;
    }

    std::forward<EmitRange>(emitRange)(cents);
    m_pitchBendRangeCents = cents;
    if (m_baseValid) {
      applyCurrentBend(std::forward<EmitBend>(emitBend));
    }
    return true;
  }

  template <typename EmitRange, typename EmitBend>
  void resetRangeAndBend(uint16_t defaultRangeCents, EmitRange&& emitRange, EmitBend&& emitBend) {
    if (m_pitchBendRangeCents != defaultRangeCents) {
      std::forward<EmitRange>(emitRange)(defaultRangeCents);
      m_pitchBendRangeCents = defaultRangeCents;
    }
    setBend(0, std::forward<EmitBend>(emitBend));
  }

 private:
  static constexpr int32_t kMinMidiPitchBend = -8192;
  static constexpr int32_t kMaxMidiPitchBend = 8191;

  bool m_baseValid = false;
  PitchType m_basePitch {};
  SeqLinearMotion<PitchType> m_motion;
  uint16_t m_pitchBendRangeCents = 200;
  int16_t m_currentPitchBend = 0;
  double m_centsPerPitchUnit = 100.0;
};

// Tracks a synth-native LFO controlled by MIDI controllers. The stored config is driver state,
// reusable fade settings describe per-note fade-ins, and m_midiDepth is the last emitted
// controller depth so duplicate MIDI events can be skipped.
class SynthLfoLane {
 public:
  void reset() {
    m_delay = 0;
    m_rate = 0;
    m_depth = 0;
    m_fade.reset();
    m_fadeLength = 0;
    m_fadeStep = 0;
    m_midiDepth = 0;
  }

  void configure(uint8_t delay, uint8_t rate, uint8_t depth) {
    m_delay = delay;
    m_rate = rate;
    m_depth = depth;
  }

  void clearConfig() {
    m_delay = 0;
    m_rate = 0;
    m_depth = 0;
    clearReusableFade();
    setCurrentDepth(0);
    m_midiDepth = 0;
  }

  [[nodiscard]] bool active() const { return m_rate != 0 && m_depth != 0; }
  [[nodiscard]] uint8_t delay() const { return m_delay; }
  [[nodiscard]] uint8_t rate() const { return m_rate; }
  [[nodiscard]] uint8_t depth() const { return m_depth; }
  void setRate(uint8_t rate) { m_rate = rate; }
  void setDepth(uint8_t depth) { m_depth = depth; }

  // Some drivers fade LFO depth in fixed-point units while others use raw depth.
  [[nodiscard]] int32_t configuredDepth(uint8_t fractionalBits = 0) const {
    return static_cast<int32_t>(m_depth) << fractionalBits;
  }

  [[nodiscard]] int32_t clampToConfiguredDepth(int32_t depth, uint8_t fractionalBits = 0) const {
    return std::min(configuredDepth(fractionalBits), depth);
  }

  [[nodiscard]] uint8_t outputDepthWhen(bool enabled) const {
    return enabled ? m_depth : 0;
  }

  void clearReusableFade() {
    m_fadeLength = 0;
    m_fadeStep = 0;
    m_fade.clear();
  }

  void setReusableFade(uint32_t length, int32_t step) {
    m_fadeLength = length;
    m_fadeStep = step;
  }

  // Stores reusable fade settings without starting them; note-on code decides when to apply them.
  void setReusableFadeToTarget(uint32_t length, int32_t targetDepth) {
    setReusableFade(length, length == 0 ? 0 : targetDepth / static_cast<int32_t>(length));
  }

  void setReusableFadeToConfiguredDepth(uint32_t length, uint8_t fractionalBits = 0) {
    setReusableFadeToTarget(length, configuredDepth(fractionalBits));
  }

  [[nodiscard]] bool hasReusableFade() const { return m_fadeLength != 0; }
  [[nodiscard]] uint32_t reusableFadeLength() const { return m_fadeLength; }
  [[nodiscard]] int32_t reusableFadeStep() const { return m_fadeStep; }

  void startReusableFade(uint32_t delay, int32_t targetDepth, int32_t initialDepth = 0) {
    // Apply the reusable fade settings to live fade state for the current note.
    if (m_fadeLength == 0) {
      m_fade.clear();
      return;
    }
    m_fade.setCurrent(initialDepth);
    m_fade.startToTarget(targetDepth, m_fadeStep, m_fadeLength, delay);
  }

  void startReusableFadeToConfiguredDepth(uint8_t fractionalBits = 0, int32_t initialDepth = 0) {
    startReusableFade(m_delay, configuredDepth(fractionalBits), initialDepth);
  }

  SeqMotionStatus advanceFade() { return m_fade.advance(); }
  [[nodiscard]] bool fadeActive() const { return m_fade.active(); }

  void setCurrentDepth(int32_t depth) {
    m_fade.setCurrentValue(depth);
  }

  [[nodiscard]] int32_t currentDepth() const { return m_fade.current(); }
  [[nodiscard]] uint8_t midiDepth() const { return m_midiDepth; }
  void setMidiDepth(uint8_t depth) { m_midiDepth = depth; }

  template <typename EmitDepth>
  bool setOutputDepth(uint8_t depth, EmitDepth&& emitDepth, bool force = false) {
    if (!force && depth == m_midiDepth) {
      return false;
    }

    std::forward<EmitDepth>(emitDepth)(depth);
    m_midiDepth = depth;
    return true;
  }

  template <typename ApplyDepth>
  SeqMotionStatus advanceFadeAndApply(ApplyDepth&& applyDepth) {
    if (!fadeActive()) {
      return SeqMotionStatus::Inactive;
    }

    const SeqMotionStatus step = advanceFade();
    if (step != SeqMotionStatus::Inactive && step != SeqMotionStatus::Delayed) {
      std::forward<ApplyDepth>(applyDepth)(currentDepth());
    }
    return step;
  }

  template <typename ApplyDepth>
  SeqMotionStatus advanceFadeAndApplyClamped(uint8_t fractionalBits, ApplyDepth&& applyDepth) {
    return advanceFadeAndApply(
        [this, fractionalBits, applyDepth = std::forward<ApplyDepth>(applyDepth)](int32_t depth) mutable {
          applyDepth(clampToConfiguredDepth(depth, fractionalBits));
        });
  }

 private:
  uint8_t m_delay = 0;
  uint8_t m_rate = 0;
  uint8_t m_depth = 0;
  SeqLinearMotion<int32_t> m_fade;
  uint32_t m_fadeLength = 0;
  int32_t m_fadeStep = 0;
  uint8_t m_midiDepth = 0;
};
