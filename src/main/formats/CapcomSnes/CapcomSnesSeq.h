#pragma once

#include "base/types.h"
#include "VGMSeq.h"
#include "automation/SeqMidiAutomation.h"
#include "SeqTrack.h"
#include "SeqEvent.h"
#include "CapcomSnesFormat.h"

#define CAPCOM_SNES_REPEAT_SLOT_MAX 4

#define CAPCOM_SNES_MASK_NOTE_OCTAVE    0x07
#define CAPCOM_SNES_MASK_NOTE_OCTAVE_UP 0x08
#define CAPCOM_SNES_MASK_NOTE_DOTTED    0x10
#define CAPCOM_SNES_MASK_NOTE_TRIPLET   0x20
#define CAPCOM_SNES_MASK_NOTE_SLURRED   0x40

enum CapcomSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_TOGGLE_TRIPLET,
  EVENT_TOGGLE_SLUR,
  EVENT_DOTTED_NOTE_ON,
  EVENT_TOGGLE_OCTAVE_UP,
  EVENT_NOTE_ATTRIBUTES,
  EVENT_TEMPO,
  EVENT_DURATION,
  EVENT_VOLUME,
  EVENT_PROGRAM_CHANGE,
  EVENT_OCTAVE,
  EVENT_GLOBAL_TRANSPOSE,
  EVENT_TRANSPOSE,
  EVENT_TUNING,
  EVENT_PORTAMENTO_TIME,
  EVENT_REPEAT_UNTIL_1,
  EVENT_REPEAT_UNTIL_2,
  EVENT_REPEAT_UNTIL_3,
  EVENT_REPEAT_UNTIL_4,
  EVENT_REPEAT_BREAK_1,
  EVENT_REPEAT_BREAK_2,
  EVENT_REPEAT_BREAK_3,
  EVENT_REPEAT_BREAK_4,
  EVENT_GOTO,
  EVENT_END,
  EVENT_PAN,
  EVENT_MASTER_VOLUME,
  EVENT_LFO,
  EVENT_ECHO_PARAM,
  EVENT_ECHO_ONOFF,
  EVENT_RELEASE_RATE,
  EVENT_NOP,
};

class CapcomSnesSeq
    : public VGMSeq {
 public:
  CapcomSnesSeq(RawFile *file,
                CapcomSnesVersion ver,
                u32 seqdata_offset,
                bool priorityInHeader,
                std::string newName = "Capcom SNES Seq");
  ~CapcomSnesSeq() override = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  CapcomSnesVersion version;
  std::map<u8, CapcomSnesSeqEventType> EventMap;

  bool priorityInHeader;
  s8 transpose;
  u16 tempo;
  u8 midiReverb;

  static const u8 volTable[];
  static const u8 panTable[];

  double getTempoInBPM() const;
  static double getTempoInBPM(u16 tempo);

 private:
  void loadEventMap();
};


class CapcomSnesTrack
    : public SeqTrack {
 public:
  CapcomSnesTrack(CapcomSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;
  void onTickBegin() override;
  void onTickEnd() override;

  u8 getNoteOctave() const;
  void setNoteOctave(u8 octave);
  bool isNoteOctaveUp() const;
  void setNoteOctaveUp(bool octave_up);
  bool isNoteDotted() const;
  void setNoteDotted(bool dotted);
  bool isNoteTriplet() const;
  void setNoteTriplet(bool triplet);
  bool isNoteSlurred() const;
  void setNoteSlurred(bool slurred);

 private:
  [[nodiscard]] bool areLfoOutputsEnabled() const { return vibrato.rate() != 0; }
  void addVibratoDepthEvent(u32 offset, u32 length, u8 depth);
  void setLfoOutputsEnabled(bool enabled);
  void handleLfoRateChange(u8 lfoRateByte);

  u8 repeatCount[CAPCOM_SNES_REPEAT_SLOT_MAX];      // repeat count for repeat command
  u8 noteAttributes;
  u8 durationRate;
  //s8 transpose;

  bool lastNoteSlurred;
  bool didRest;
  s8 lastKey;
  SeqSynthLfoAutomation vibrato;
  SeqSynthLfoAutomation tremolo;
  u16 lastPortamentoTime;
  double portamentoMillisecondsPerCent;

  static double getTuningInSemitones(s8 tuning);
};
