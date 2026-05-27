/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/types.h"

#include "ConversionContext.h"
#include "VGMFile.h"
#include "RawFile.h"
#include "MidiFile.h"
#include "SeqEventTimeIndex.h"
#include <set>
#include <filesystem>

class SeqTrack;
class SeqEvent;
class ISeqSlider;

enum ReadMode : u8 {
  READMODE_ADD_TO_UI,
  READMODE_CONVERT_TO_MIDI,
  READMODE_FIND_DELTA_LENGTH
};

enum class PanVolumeCorrectionMode : u8 {
  kNoVolumeAdjust,
  kAdjustVolumeController,
  kAdjustExpressionController
};

class VGMSeq : public VGMFile {
 public:
  VGMSeq(const std::string &format, RawFile *file, u32 offset, u32 length = 0,
         std::string name = "VGM Sequence");
  ~VGMSeq() override;

  bool loadVGMFile(bool useMatcher = true) override;
  bool load() override;
  virtual bool parseHeader();
  virtual bool parseTrackPointers();  // Function to find all of the track pointers.   Returns number of total tracks.
  virtual void resetVars();
  virtual void onTickEnd() {}
  virtual void useColl(const VGMColl* coll) {}
  virtual MidiFile *convertToMidi(const VGMColl* coll = nullptr);
  virtual MidiFile *convertToMidi(const VGMColl* coll, const ConversionContext& context);
  virtual MidiTrack *firstMidiTrack();
  void setPPQN(u16 ppqn);
  [[nodiscard]] u16 ppqn() const;
  // void setTimeSignature(u8 numer, denom);
  void addInstrumentRef(u32 progNum);
  void addBankReference(u16 bank);
  [[nodiscard]] const std::set<u16>& referencedBanks() const;

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
  void setAlwaysWriteInitialVol(u8 theVol = 100) {
    m_always_write_initial_vol = true;
    m_initial_volume = theVol;
  }
  [[nodiscard]] bool alwaysWriteInitialExpression() const { return m_always_write_initial_expression; }
  void setAlwaysWriteInitialExpression(u8 level = 127) {
    m_always_write_initial_expression = true;
    m_initial_expression = level;
  }

  [[nodiscard]] bool alwaysWriteInitialReverb() const { return m_always_write_initial_reverb; }
  void setAlwaysWriteInitialReverb(u8 level = 127) {
    m_always_write_initial_reverb = true;
    m_initial_reverb_level = level;
  }

  [[nodiscard]] bool alwaysWriteInitialPitchBendRange() const { return m_always_write_initial_pitch_bend_range; }
  void setAlwaysWriteInitialPitchBendRange(u16 cents) {
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

  void setShouldTrackControlFlowState(bool enable) { m_track_control_flow_state = enable; }
  [[nodiscard]] bool shouldTrackControlFlowState() const { return m_track_control_flow_state; }

  bool saveAsMidi(const std::filesystem::path &filepath, const VGMColl* coll = nullptr);
  bool saveAsMidi(const std::filesystem::path& filepath, const VGMColl* coll, const ConversionContext& context);
  [[nodiscard]] const ConversionContext& conversionContext() const { return m_conversionContext; }

  void deactivateAllTracks();

  [[nodiscard]] u8 initialVolume() const { return m_initial_volume; }
  void setInitialVolume(u8 volume) { m_initial_volume = volume; }
  [[nodiscard]] u8 initialExpression() const { return m_initial_expression; }
  void setInitialExpression(u8 expression) { m_initial_expression = expression; }
  [[nodiscard]] u8 initialReverbLevel() const { return m_initial_reverb_level; }
  void setInitialReverbLevel(u8 reverb_level) { m_initial_reverb_level = reverb_level; }
  [[nodiscard]] u16 initialPitchBendRange() const { return m_initial_pitch_bend_range_cents; }
  void setInitialPitchBendRange(u16 cents) { m_initial_pitch_bend_range_cents = cents; }

  SeqEventTimeIndex& timedEventIndex() { return m_timedEvents; }

 protected:
  void setConversionContext(const ConversionContext& context) { m_conversionContext = context; }

  virtual bool loadTracks(ReadMode readMode, u32 stopTime = 1000000);
  virtual void loadTracksMain(u32 stopTime);
  virtual bool postLoad();

private:
  bool hasActiveTracks();
  int foreverLoopCount();

 public:
  MidiFile *midi;
  u32 nNumTracks;
  ReadMode readMode;
  double tempoBPM;
  u32 time;                // absolute current time (ticks)

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
  std::vector<u32> aInstrumentsUsed;

  std::vector<ISeqSlider *> aSliders;

private:
  ConversionContext m_conversionContext;
  std::set<u16> m_referencedBanks;

  // Timeline of sequence events emitted during MIDI conversion.
  SeqEventTimeIndex m_timedEvents;

  u16 m_ppqn;

  u8 m_initial_volume;
  u8 m_initial_expression;
  u8 m_initial_reverb_level;
  u16 m_initial_pitch_bend_range_cents;

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

  bool m_track_control_flow_state;  // When enabled, we log a hash of the control flow state every
                                    // time we parse an event to allow detection of infinite loops.

};

extern u8 mode;
