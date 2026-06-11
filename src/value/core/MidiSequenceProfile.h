/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiModel.h"
#include "value/core/SequenceModel.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vgmtrans::core {

// MidiTrackState is the mutable interpreter state used while lowering one CommandTrack.
// Profiles own the meaning of these fields; the shared lowering loop only advances
// control flow and calls profile hooks at the right time.
struct MidiTrackState {
  u32 trackIndex = 0;
  u8 channel = 0;
  u64 tick = 0;
  u32 durationRate = 0;
  s32 transpose = 0;
  s32 globalTranspose = 0;
  u8 modulationRate = 0;
  u8 vibratoDepth = 0;
  u8 tremoloDepth = 0;
  double portamentoMillisecondsPerCent = 0.0;
  u16 lastPortamentoTime = 0;
  u32 noteOctave = 0;
  bool noteDotted = false;
  bool noteWait = true;
  bool noteTriplet = false;
  bool noteSlurred = false;
  bool noteOctaveUp = false;
  bool lastNoteSlurred = false;
  bool didRest = false;
  s32 lastKey = -1;
  std::array<u32, 4> repeatCounters{};
};

struct MidiNoteTiming {
  // soundingTicks is note length; advanceTicks is how far the driver's time cursor moves.
  // They differ for slurs, legato, note-wait modes, and other driver-specific behavior.
  u8 key = 0;
  u8 velocity = 127;
  u32 soundingTicks = 0;
  u32 advanceTicks = 0;
  bool extendsPrevious = false;
  std::vector<MidiEvent> beforeEvents;
};

// Default hooks implement a plain MIDI-ish interpretation. Format profiles override only
// the hooks whose driver math or state differs from the shared fallback.
void defaultBeginTrack(const CommandSequence& commandSequence, const CommandTrack& track, MidiTrackState& state,
                       std::vector<MidiEvent>& events);
[[nodiscard]] u32 defaultRestTicks(const RestCommand& command, MidiTrackState& state);
[[nodiscard]] MidiNoteTiming defaultNoteTiming(const NoteCommand& command, MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretNoteState(const NoteStateCommand& command, MidiTrackState& state);
void defaultApplyDuration(const DurationCommand& command, MidiTrackState& state);
void defaultApplyTranspose(const TransposeCommand& command, MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretTempo(const TempoCommand& command, const MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretProgram(const ProgramCommand& command,
                                                             const MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretVolume(const VolumeCommand& command, const MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretPan(const PanCommand& command, const MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretMasterVolume(const MasterVolumeCommand& command,
                                                                  const MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretReverb(const ReverbCommand& command, const MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretTuning(const TuningCommand& command, const MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretPortamento(const PortamentoCommand& command,
                                                                MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretVibrato(const VibratoCommand& command, MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretTremolo(const TremoloCommand& command, MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretModulationRate(const ModulationRateCommand& command,
                                                                    MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretEnvelope(const EnvelopeCommand& command,
                                                              const MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretDriverSpecific(const DriverSpecificCommand& command,
                                                                    MidiTrackState& state);
[[nodiscard]] std::vector<MidiEvent> defaultInterpretRepeatBreak(const RepeatBreakCommand& command,
                                                                 MidiTrackState& state);

struct MidiSequenceProfile {
  // Function pointers keep the profile copyable and allocation-free. They are intentionally
  // narrow: format code describes driver semantics as a small table of behavior hooks.
  using BeginTrack = void (*)(const CommandSequence& commandSequence, const CommandTrack& track, MidiTrackState& state,
                              std::vector<MidiEvent>& events);
  using RestTicks = u32 (*)(const RestCommand& command, MidiTrackState& state);
  using NoteTiming = MidiNoteTiming (*)(const NoteCommand& command, MidiTrackState& state);
  using InterpretNoteState = std::vector<MidiEvent> (*)(const NoteStateCommand& command, MidiTrackState& state);
  using ApplyDuration = void (*)(const DurationCommand& command, MidiTrackState& state);
  using ApplyTranspose = void (*)(const TransposeCommand& command, MidiTrackState& state);
  using InterpretTempo = std::vector<MidiEvent> (*)(const TempoCommand& command, const MidiTrackState& state);
  using InterpretProgram = std::vector<MidiEvent> (*)(const ProgramCommand& command, const MidiTrackState& state);
  using InterpretVolume = std::vector<MidiEvent> (*)(const VolumeCommand& command, const MidiTrackState& state);
  using InterpretPan = std::vector<MidiEvent> (*)(const PanCommand& command, const MidiTrackState& state);
  using InterpretMasterVolume = std::vector<MidiEvent> (*)(const MasterVolumeCommand& command,
                                                           const MidiTrackState& state);
  using InterpretReverb = std::vector<MidiEvent> (*)(const ReverbCommand& command, const MidiTrackState& state);
  using InterpretTuning = std::vector<MidiEvent> (*)(const TuningCommand& command, const MidiTrackState& state);
  using InterpretPortamento = std::vector<MidiEvent> (*)(const PortamentoCommand& command, MidiTrackState& state);
  using InterpretVibrato = std::vector<MidiEvent> (*)(const VibratoCommand& command, MidiTrackState& state);
  using InterpretTremolo = std::vector<MidiEvent> (*)(const TremoloCommand& command, MidiTrackState& state);
  using InterpretModulationRate = std::vector<MidiEvent> (*)(const ModulationRateCommand& command,
                                                             MidiTrackState& state);
  using InterpretEnvelope = std::vector<MidiEvent> (*)(const EnvelopeCommand& command, const MidiTrackState& state);
  using InterpretDriverSpecific = std::vector<MidiEvent> (*)(const DriverSpecificCommand& command,
                                                             MidiTrackState& state);
  using InterpretRepeatBreak = std::vector<MidiEvent> (*)(const RepeatBreakCommand& command, MidiTrackState& state);

  BeginTrack beginTrack = defaultBeginTrack;
  RestTicks restTicks = defaultRestTicks;
  NoteTiming noteTiming = defaultNoteTiming;
  InterpretNoteState interpretNoteState = defaultInterpretNoteState;
  ApplyDuration applyDuration = defaultApplyDuration;
  ApplyTranspose applyTranspose = defaultApplyTranspose;
  InterpretTempo interpretTempo = defaultInterpretTempo;
  InterpretProgram interpretProgram = defaultInterpretProgram;
  InterpretVolume interpretVolume = defaultInterpretVolume;
  InterpretPan interpretPan = defaultInterpretPan;
  InterpretMasterVolume interpretMasterVolume = defaultInterpretMasterVolume;
  InterpretReverb interpretReverb = defaultInterpretReverb;
  InterpretTuning interpretTuning = defaultInterpretTuning;
  InterpretPortamento interpretPortamento = defaultInterpretPortamento;
  InterpretVibrato interpretVibrato = defaultInterpretVibrato;
  InterpretTremolo interpretTremolo = defaultInterpretTremolo;
  InterpretModulationRate interpretModulationRate = defaultInterpretModulationRate;
  InterpretEnvelope interpretEnvelope = defaultInterpretEnvelope;
  InterpretDriverSpecific interpretDriverSpecific = defaultInterpretDriverSpecific;
  InterpretRepeatBreak interpretRepeatBreak = defaultInterpretRepeatBreak;
};

class MidiSequenceProfileRegistry {
public:
  // The registry stores descriptors by value. Sessions can be copied or rebuilt without
  // retaining hidden factory objects or profile lifetimes.
  void add(std::string format, MidiSequenceProfile profile);
  [[nodiscard]] const MidiSequenceProfile* find(std::string_view format) const;
  [[nodiscard]] bool contains(std::string_view format) const;

private:
  std::unordered_map<std::string, MidiSequenceProfile> profiles_;
};

}  // namespace vgmtrans::core
