#include "pch.h"
#include "SeqSlider.h"

template<typename TNumber>
SeqSlider<TNumber>::SeqSlider(SeqTrack *track,
                              uint32_t time,
                              uint32_t duration,
                              TNumber initialValue,
                              TNumber targetValue) :
    track(track),
    time(time),
    duration(duration),
    initialValue(initialValue),
    targetValue(targetValue) {
}

template<typename TNumber>
SeqSlider<TNumber>::~SeqSlider() {
}

template<typename TNumber>
TNumber SeqSlider<TNumber>::get(uint32_t time) const {
  if (!isActive(time))
    return 0;

  // linear interpolation
  uint32_t step = time - this->time;
  double alpha = (double) step / this->duration;
  return (TNumber) (initialValue * (1.0 - alpha) + targetValue * alpha);
}

template<typename TNumber>
void SeqSlider<TNumber>::write(uint32_t time) const {
  if (changesAt(time))
    writeMessage(get(time));
}

template<typename TNumber>
bool SeqSlider<TNumber>::changesAt(uint32_t time) const {
  if (!isActive(time))
    return false;

  if (time == this->time)
    return true;

  return get(time) != get(time - 1);
}

template<typename TNumber>
bool SeqSlider<TNumber>::isStarted(uint32_t time) const {
  return time >= this->time;
}

template<typename TNumber>
bool SeqSlider<TNumber>::isActive(uint32_t time) const {
  return isStarted(time) && time <= this->time + this->duration;
}

VolSlider::VolSlider(SeqTrack *track, uint32_t time, uint32_t duration, uint8_t initialValue, uint8_t targetValue) :
    SeqSlider(track, time, duration, initialValue, targetValue) {
}

void VolSlider::writeMessage(uint8_t value) const {
  track->AddVolNoItem(value);
}

MasterVolSlider::MasterVolSlider(SeqTrack *track,
                                 uint32_t time,
                                 uint32_t duration,
                                 uint8_t initialValue,
                                 uint8_t targetValue) :
    SeqSlider(track, time, duration, initialValue, targetValue) {
}

void MasterVolSlider::writeMessage(uint8_t value) const {
  track->AddMasterVolNoItem(value);
}

ExpressionSlider::ExpressionSlider(SeqTrack *track,
                                   uint32_t time,
                                   uint32_t duration,
                                   uint8_t initialValue,
                                   uint8_t targetValue) :
    SeqSlider(track, time, duration, initialValue, targetValue) {
}

void ExpressionSlider::writeMessage(uint8_t value) const {
  track->AddExpressionNoItem(value);
}

PanSlider::PanSlider(SeqTrack *track, uint32_t time, uint32_t duration, uint8_t initialValue, uint8_t targetValue) :
    SeqSlider(track, time, duration, initialValue, targetValue) {
}

void PanSlider::writeMessage(uint8_t value) const {
  track->AddPanNoItem(value);
}
