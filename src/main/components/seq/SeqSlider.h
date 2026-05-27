#pragma once

#include "base/types.h"
#include "SeqTrack.h"

class ISeqSlider {
 public:
  virtual ~ISeqSlider() { }

  virtual void write(u32 time) const = 0;
  virtual bool isStarted(u32 time) const = 0;
  virtual bool isActive(u32 time) const = 0;
};

template<typename TNumber>
class SeqSlider: public ISeqSlider {
 public:
  SeqSlider(SeqTrack *track, u32 time, u32 duration, TNumber initialValue, TNumber targetValue);
  ~SeqSlider() override = default;

  virtual TNumber get(u32 time) const;
  void write(u32 time) const override;
  virtual void writeMessage(TNumber value) const = 0;
  virtual bool changesAt(u32 time) const;
  bool isStarted(u32 time) const override;
  bool isActive(u32 m_time) const override;

 protected:
  SeqTrack *m_track;
  u32 m_time;
  u32 m_duration;
  TNumber m_initialValue;
  TNumber m_targetValue;
};

class VolSlider: public SeqSlider<u8> {
 public:
  VolSlider(SeqTrack *track, u32 time, u32 duration, u8 initialValue, u8 targetValue);
  void writeMessage(u8 value) const override;
};

class MasterVolSlider: public SeqSlider<u8> {
 public:
  MasterVolSlider(SeqTrack *track, u32 time, u32 duration, u8 initialValue, u8 targetValue);
  void writeMessage(u8 value) const override;
};

class ExpressionSlider: public SeqSlider<u8> {
 public:
  ExpressionSlider(SeqTrack *track, u32 time, u32 duration, u8 initialValue, u8 targetValue);
  void writeMessage(u8 value) const override;
};

class PanSlider: public SeqSlider<u8> {
 public:
  PanSlider(SeqTrack *track, u32 time, u32 duration, u8 initialValue, u8 targetValue);
  void writeMessage(u8 value) const override;
};
