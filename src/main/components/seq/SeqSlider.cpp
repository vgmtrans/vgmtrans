/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Types.h"
#include "SeqSlider.h"

template<typename TNumber>
SeqSlider<TNumber>::SeqSlider(SeqTrack *track,
                              u32 time,
                              u32 duration,
                              TNumber initialValue,
                              TNumber targetValue) :
    m_track(track),
    m_time(time),
    m_duration(duration),
    m_initialValue(initialValue),
    m_targetValue(targetValue) {
}

template<typename TNumber>
TNumber SeqSlider<TNumber>::get(u32 time) const {
  if (!isActive(time))
    return 0;

  // linear interpolation
  u32 step = time - m_time;
  double alpha = static_cast<double>(step) / m_duration;
  return static_cast<TNumber>(m_initialValue * (1.0 - alpha) + m_targetValue * alpha);
}

template<typename TNumber>
void SeqSlider<TNumber>::write(u32 time) const {
  if (changesAt(time))
    writeMessage(get(time));
}

template<typename TNumber>
bool SeqSlider<TNumber>::changesAt(u32 time) const {
  if (!isActive(time))
    return false;

  if (time == m_time)
    return true;

  return get(time) != get(time - 1);
}

template<typename TNumber>
bool SeqSlider<TNumber>::isStarted(u32 time) const {
  return time >= m_time;
}

template<typename TNumber>
bool SeqSlider<TNumber>::isActive(u32 time) const {
    return isStarted(time) && time <= (m_time + m_duration);
}

VolSlider::VolSlider(SeqTrack *track, u32 time, u32 duration, u8 initialValue, u8 targetValue) :
    SeqSlider(track, time, duration, initialValue, targetValue) {
}

void VolSlider::writeMessage(u8 value) const {
  m_track->addVolNoItem(value);
}

MasterVolSlider::MasterVolSlider(SeqTrack *track,
                                 u32 time,
                                 u32 duration,
                                 u8 initialValue,
                                 u8 targetValue) :
    SeqSlider(track, time, duration, initialValue, targetValue) {
}

void MasterVolSlider::writeMessage(u8 value) const {
  m_track->addMasterVolNoItem(value);
}

ExpressionSlider::ExpressionSlider(SeqTrack *track,
                                   u32 time,
                                   u32 duration,
                                   u8 initialValue,
                                   u8 targetValue) :
    SeqSlider(track, time, duration, initialValue, targetValue) {
}

void ExpressionSlider::writeMessage(u8 value) const {
  m_track->addExpressionNoItem(value);
}

PanSlider::PanSlider(SeqTrack *track, u32 time, u32 duration, u8 initialValue, u8 targetValue) :
    SeqSlider(track, time, duration, initialValue, targetValue) {
}

void PanSlider::writeMessage(u8 value) const {
  m_track->addPanNoItem(value);
}
