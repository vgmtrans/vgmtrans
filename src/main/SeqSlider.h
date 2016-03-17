#pragma once
#include "SeqTrack.h"

class ISeqSlider {
 public:
  virtual ~ISeqSlider() { }

  virtual void write(uint32_t time) const = 0;
  virtual bool isStarted(uint32_t time) const = 0;
  virtual bool isActive(uint32_t time) const = 0;
};

template<typename TNumber>
class SeqSlider: public ISeqSlider {
 public:
  SeqSlider(SeqTrack *track, uint32_t time, uint32_t duration, TNumber initialValue, TNumber targetValue);
  virtual ~SeqSlider();

  virtual TNumber get(uint32_t time) const;
  virtual void write(uint32_t time) const;
  virtual void writeMessage(TNumber value) const = 0;
  virtual bool changesAt(uint32_t time) const;
  virtual bool isStarted(uint32_t time) const;
  virtual bool isActive(uint32_t time) const;

 public:
  SeqTrack *track;
  uint32_t time;
  uint32_t duration;
  TNumber initialValue;
  TNumber targetValue;
};

class VolSlider: public SeqSlider<uint8_t> {
 public:
  VolSlider(SeqTrack *track, uint32_t time, uint32_t duration, uint8_t initialValue, uint8_t targetValue);
  virtual void writeMessage(uint8_t value) const;
};

class MasterVolSlider: public SeqSlider<uint8_t> {
 public:
  MasterVolSlider(SeqTrack *track, uint32_t time, uint32_t duration, uint8_t initialValue, uint8_t targetValue);
  virtual void writeMessage(uint8_t value) const;
};

class ExpressionSlider: public SeqSlider<uint8_t> {
 public:
  ExpressionSlider(SeqTrack *track, uint32_t time, uint32_t duration, uint8_t initialValue, uint8_t targetValue);
  virtual void writeMessage(uint8_t value) const;
};

class PanSlider: public SeqSlider<uint8_t> {
 public:
  PanSlider(SeqTrack *track, uint32_t time, uint32_t duration, uint8_t initialValue, uint8_t targetValue);
  virtual void writeMessage(uint8_t value) const;
};
