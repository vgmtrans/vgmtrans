/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "VGMFile.h"
#include "RawFile.h"
#include "MidiFile.h"

class SeqTrack;
class SeqEvent;
class ISeqSlider;

enum ReadMode : uint8_t {
  READMODE_ADD_TO_UI,
  READMODE_CONVERT_TO_MIDI,
  READMODE_FIND_DELTA_LENGTH
};

enum class PanVolumeCorrectionMode : uint8_t {
  kNoVolumeAdjust,
  kAdjustVolumeController,
  kAdjustExpressionController
};

class VGMSeq : public VGMFile {
 public:
  VGMSeq(const std::string &format, RawFile *file, uint32_t offset, uint32_t length = 0,
         std::string name = "VGM Sequence");
  ~VGMSeq() override;

  bool loadVGMFile() override;
  bool load() override;              // Function to load all the information about the sequence
  virtual bool loadMain();
  virtual bool parseHeader();
  virtual bool parseTrackPointers();  // Function to find all of the track pointers.   Returns number of total tracks.
  virtual void resetVars();
  virtual void useColl(const VGMColl* coll) {}
  virtual MidiFile *convertToMidi(const VGMColl* coll = nullptr);
  virtual MidiTrack *firstMidiTrack();
  void setPPQN(uint16_t ppqn);
  [[nodiscard]] uint16_t ppqn() const;
  // void setTimeSignature(uint8_t numer, denom);
  void addInstrumentRef(uint32_t progNum);

  void useReverb() { m_use_reverb = true; }

  [[nodiscard]] bool usesMonophonicTracks() const { return m_use_monophonic_tracks; }
  void setUsesMonophonicTracks() { m_use_monophonic_tracks = true; }

  [[nodiscard]] bool usesLinearAmplitudeScale() const { return m_use_linear_amplitude_scale; }
  void setUseLinearAmplitudeScale(bool set) { m_use_linear_amplitude_scale = set; }

  [[nodiscard]] bool usesLinearPanAmplitudeScale() const { return m_use_linear_pan_amplitude_scale; }
  void setUseLinearPanAmplitudeScale(PanVolumeCorrectionMode mode) {
    m_use_linear_pan_amplitude_scale = true;
    panVolumeCorrectionMode = mode;
  }

  [[nodiscard]] bool alwaysWriteInitialVol() const { return m_always_write_initial_vol; }
  void setAlwaysWriteInitialVol(uint8_t theVol = 100) {
    m_always_write_initial_vol = true;
    m_initial_volume = theVol;
  }
  [[nodiscard]] bool alwaysWriteInitialExpression() const { return m_always_write_initial_expression; }
  void setAlwaysWriteInitialExpression(uint8_t level = 127) {
    m_always_write_initial_expression = true;
    m_initial_expression = level;
  }

  [[nodiscard]] bool alwaysWriteInitialReverb() const { return m_always_write_initial_reverb; }
  void setAlwaysWriteInitialReverb(uint8_t level = 127) {
    m_always_write_initial_reverb = true;
    m_initial_reverb_level = level;
  }

  [[nodiscard]] bool alwaysWriteInitialPitchBendRange() const { return m_always_write_initial_pitch_bend_range; }
  void setAlwaysWriteInitialPitchBendRange(uint16_t cents) {
    m_always_write_initial_pitch_bend_range = true;
    m_initial_pitch_bend_range_cents = cents;
  }

  [[nodiscard]] bool alwaysWriteInitialTempo() const { return m_always_write_initial_tempo; }
  void setAlwaysWriteInitialTempo(double beatsPerMin) {
    m_always_write_initial_tempo = true;
    initialTempoBPM = beatsPerMin;
  }

  [[nodiscard]] bool alwaysWriteInitialMonoMode() const { return m_always_write_initial_mono_mode; }
  void setAlwaysWriteInitialMonoMode(bool set) { m_always_write_initial_mono_mode = set; }
  [[nodiscard]] bool allowDiscontinuousTrackData() const { return m_allow_discontinuous_track_data; }
  void setAllowDiscontinuousTrackData(bool allow) { m_allow_discontinuous_track_data = allow; }

  bool saveAsMidi(const std::string &filepath, const VGMColl* coll = nullptr);

  void deactivateAllTracks();

  [[nodiscard]] uint8_t initialVolume() const { return m_initial_volume; }
  void setInitialVolume(uint8_t volume) { m_initial_volume = volume; }
  [[nodiscard]] uint8_t initialExpression() const { return m_initial_expression; }
  void setInitialExpression(uint8_t expression) { m_initial_expression = expression; }
  [[nodiscard]] uint8_t initialReverbLevel() const { return m_initial_reverb_level; }
  void setInitialReverbLevel(uint8_t reverb_level) { m_initial_reverb_level = reverb_level; }
  [[nodiscard]] uint16_t initialPitchBendRange() const { return m_initial_pitch_bend_range_cents; }
  void setInitialPitchBendRange(uint16_t cents) { m_initial_pitch_bend_range_cents = cents; }

 protected:
  virtual bool loadTracks(ReadMode readMode, uint32_t stopTime = 1000000);
  virtual void loadTracksMain(uint32_t stopTime);
  virtual bool postLoad();

private:
  bool hasActiveTracks();
  int foreverLoopCount();

 public:
  MidiFile *midi;
  uint32_t nNumTracks;
  ReadMode readMode;
  double tempoBPM;
  uint32_t time;                // absolute current time (ticks)

  PanVolumeCorrectionMode panVolumeCorrectionMode{PanVolumeCorrectionMode::kNoVolumeAdjust};

  // True if each tracks in a sequence needs to be loaded simultaneously in tick by tick, as the real music player does.
  // Pros:
  //   - It allows to share some variables between two or more tracks.
  //   - It might be useful when you want to emulate envelopes like vibrato for every ticks.
  // Cons:
  //   - It prohibits that each tracks have different timings (delta time).
  //   - It is not very suitable for formats like SMF format 1 (multitrack, delta time first format),
  //     because a track must pause the parsing every time a parser encounters to delta time event.
  //     It can be used anyway, but it is useless and annoys you, in most cases.
  bool bLoadTickByTick;
  
  // There may be a rare case when you do not want to increment the tick counter after
  // processing a tick because you want to go to a new section.
  bool bIncTickAfterProcessingTracks;

  double initialTempoBPM;

  std::vector<SeqTrack *> aTracks;  // array of track pointers
  std::vector<uint32_t> aInstrumentsUsed;

  std::vector<ISeqSlider *> aSliders;

private:
  VGMColl* m_coll = nullptr;

  uint16_t m_ppqn;

  uint8_t m_initial_volume;
  uint8_t m_initial_expression;
  uint8_t m_initial_reverb_level;
  uint16_t m_initial_pitch_bend_range_cents;

  bool m_always_write_initial_tempo;
  bool m_always_write_initial_vol;
  bool m_always_write_initial_expression;
  bool m_always_write_initial_reverb;
  bool m_always_write_initial_pitch_bend_range;
  bool m_always_write_initial_mono_mode;
  bool m_allow_discontinuous_track_data;

  // attributes
  bool m_use_monophonic_tracks;   // Only 1 voice at a time on a track.  We can assume note offs always
                                // use last note on key. which is important when drivers allow things
                                // like global transposition events mid note
  bool m_use_linear_amplitude_scale;  // This will cause all all velocity, volume, and expression
                                  // events to be automatically converted from a linear scale to
                                  // MIDI's logarithmic scale
  bool m_use_linear_pan_amplitude_scale; // This will cause all all pan events to be automatically
                                    // converted from a linear scale to MIDI's sin/cos scale
  bool m_use_reverb;

};

extern uint8_t mode;
