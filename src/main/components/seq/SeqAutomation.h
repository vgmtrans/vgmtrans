/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <cstdint>

enum class SeqMotionStep {
  Inactive,
  Delayed,
  Running,
  Finished,
};

template <typename ValueType, typename DeltaType = ValueType>
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

  void startToTarget(ValueType target, DeltaType delta, uint32_t length, uint32_t delay = 0) {
    m_target = target;
    m_delta = delta;
    m_delay = delay;
    m_ticksRemaining = length;
    m_useLength = true;
  }

  void startByStep(ValueType target, DeltaType delta, uint32_t delay = 0) {
    m_target = target;
    m_delta = delta;
    m_delay = delay;
    m_ticksRemaining = 0;
    m_useLength = false;
  }

  [[nodiscard]] bool active() const {
    return m_delay != 0 || (m_useLength ? m_ticksRemaining != 0 : m_delta != DeltaType {});
  }

  [[nodiscard]] ValueType current() const { return m_current; }
  [[nodiscard]] ValueType target() const { return m_target; }
  [[nodiscard]] DeltaType delta() const { return m_delta; }
  [[nodiscard]] uint32_t delayRemaining() const { return m_delay; }
  [[nodiscard]] uint32_t ticksRemaining() const { return m_ticksRemaining; }
  [[nodiscard]] bool usesLength() const { return m_useLength; }

  SeqMotionStep advance() {
    if (m_delay != 0) {
      m_delay -= 1;
      return SeqMotionStep::Delayed;
    }

    if (m_useLength) {
      if (m_ticksRemaining == 0) {
        return SeqMotionStep::Inactive;
      }

      m_ticksRemaining -= 1;
      if (m_ticksRemaining == 0) {
        m_current = m_target;
        return SeqMotionStep::Finished;
      }

      m_current = static_cast<ValueType>(m_current + m_delta);
      return SeqMotionStep::Running;
    }

    if (m_delta == DeltaType {}) {
      return SeqMotionStep::Inactive;
    }

    m_current = static_cast<ValueType>(m_current + m_delta);
    if ((m_delta > DeltaType {} && m_current >= m_target) ||
        (m_delta < DeltaType {} && m_current <= m_target)) {
      m_current = m_target;
      m_delta = {};
      return SeqMotionStep::Finished;
    }

    return SeqMotionStep::Running;
  }

 private:
  ValueType m_current {};
  ValueType m_target {};
  DeltaType m_delta {};
  uint32_t m_delay = 0;
  uint32_t m_ticksRemaining = 0;
  bool m_useLength = true;
};

template <typename PitchType, typename DeltaType = PitchType>
class SeqPitchBendState {
 public:
  void reset(uint16_t defaultRangeCents) {
    m_baseValid = false;
    m_basePitch = {};
    m_motion.reset();
    m_pitchBendRangeCents = defaultRangeCents;
    m_currentPitchBend = 0;
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

  void startMotion(PitchType targetPitch, DeltaType delta, uint32_t length, uint32_t delay = 0) {
    m_motion.startToTarget(targetPitch, delta, length, delay);
  }

  SeqMotionStep advanceMotion() {
    if (!m_baseValid) {
      return SeqMotionStep::Inactive;
    }
    return m_motion.advance();
  }

  [[nodiscard]] PitchType basePitch() const { return m_basePitch; }
  [[nodiscard]] PitchType currentPitch() const { return m_motion.current(); }
  [[nodiscard]] PitchType targetPitch() const { return m_motion.target(); }
  [[nodiscard]] bool motionActive() const { return m_motion.active(); }
  [[nodiscard]] uint32_t motionTicksRemaining() const { return m_motion.ticksRemaining(); }

  [[nodiscard]] uint16_t pitchBendRangeCents() const { return m_pitchBendRangeCents; }
  void setPitchBendRangeCents(uint16_t cents) { m_pitchBendRangeCents = cents; }

  [[nodiscard]] int16_t currentPitchBend() const { return m_currentPitchBend; }
  void setCurrentPitchBend(int16_t bend) { m_currentPitchBend = bend; }

 private:
  bool m_baseValid = false;
  PitchType m_basePitch {};
  SeqLinearMotion<PitchType, DeltaType> m_motion;
  uint16_t m_pitchBendRangeCents = 200;
  int16_t m_currentPitchBend = 0;
};

class SeqLfoState {
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

  void clearReusableFade() {
    m_fadeLength = 0;
    m_fadeStep = 0;
    m_fade.clear();
  }

  void setReusableFade(uint32_t length, int32_t step) {
    m_fadeLength = length;
    m_fadeStep = step;
  }

  [[nodiscard]] bool hasReusableFade() const { return m_fadeLength != 0; }
  [[nodiscard]] uint32_t reusableFadeLength() const { return m_fadeLength; }
  [[nodiscard]] int32_t reusableFadeStep() const { return m_fadeStep; }

  void startReusableFade(uint32_t delay, int32_t targetDepth, int32_t initialDepth = 0) {
    if (m_fadeLength == 0) {
      m_fade.clear();
      return;
    }
    m_fade.setCurrent(initialDepth);
    m_fade.startToTarget(targetDepth, m_fadeStep, m_fadeLength, delay);
  }

  SeqMotionStep advanceFade() { return m_fade.advance(); }
  [[nodiscard]] bool fadeActive() const { return m_fade.active(); }

  void setCurrentDepth(int32_t depth) {
    m_fade.setCurrentValue(depth);
  }

  [[nodiscard]] int32_t currentDepth() const { return m_fade.current(); }
  [[nodiscard]] uint8_t midiDepth() const { return m_midiDepth; }
  void setMidiDepth(uint8_t depth) { m_midiDepth = depth; }

 private:
  uint8_t m_delay = 0;
  uint8_t m_rate = 0;
  uint8_t m_depth = 0;
  SeqLinearMotion<int32_t, int32_t> m_fade;
  uint32_t m_fadeLength = 0;
  int32_t m_fadeStep = 0;
  uint8_t m_midiDepth = 0;
};
