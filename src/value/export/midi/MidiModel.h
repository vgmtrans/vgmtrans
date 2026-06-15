/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"

#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// MIDI event model used just before writing a Standard MIDI File. At this point
// source playback has already run, loops have been handled, channels/ports have
// been assigned, and levels have been converted to MIDI controller values.

struct NoteOn {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
  u8 velocity = 0;
};

struct NoteOff {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
  u8 velocity = 0;
};

struct NoteDuration {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
  u8 velocity = 0;
  // The renderer keeps note duration as one event. MidiExporter expands it to
  // note-on/note-off messages when writing the file.
  u32 duration = 0;
};

struct Tempo {
  u64 tick = 0;
  u32 microsecondsPerQuarter = 500000;
};

struct MidiPort {
  u64 tick = 0;
  u8 port = 0;
};

struct ProgramChange {
  u64 tick = 0;
  u8 channel = 0;
  u8 program = 0;
};

struct BankSelect {
  u64 tick = 0;
  u8 channel = 0;
  u16 bank = 0;
  bool writeLsb = true;
};

struct Volume {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct Volume14 {
  u64 tick = 0;
  u8 channel = 0;
  // Some source drivers have finer resolution or nonlinear amplitude curves. Keep a
  // 14-bit event here so exporters can write MSB/LSB controller pairs when useful.
  u16 value = 0;
};

struct Pan {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 64;
};

struct Expression {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 127;
};

struct Expression14 {
  u64 tick = 0;
  u8 channel = 0;
  u16 value = 16383;
};

struct MasterVolume {
  u64 tick = 0;
  u16 value = 0;
};

struct Reverb {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct FineTune {
  u64 tick = 0;
  u8 channel = 0;
  double cents = 0.0;
};

struct CoarseTune {
  u64 tick = 0;
  u8 channel = 0;
  s8 semitones = 0;
};

struct PitchBend {
  u64 tick = 0;
  u8 channel = 0;
  s16 value = 0;
};

struct PitchBendRange {
  u64 tick = 0;
  u8 channel = 0;
  u8 semitones = 2;
};

struct VibratoDepth {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct VibratoFrequency {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct VibratoDelay {
  u64 tick = 0;
  u8 channel = 0;
  u32 ticks = 0;
};

struct TremoloDepth {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct TremoloFrequency {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct TremoloDelay {
  u64 tick = 0;
  u8 channel = 0;
  u32 ticks = 0;
};

struct PortamentoEnable {
  u64 tick = 0;
  u8 channel = 0;
  bool enabled = false;
};

struct PortamentoTime {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct PortamentoTime14 {
  u64 tick = 0;
  u8 channel = 0;
  u16 value = 0;
};

struct PortamentoControl {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
};

struct LegatoPedal {
  u64 tick = 0;
  u8 channel = 0;
  bool enabled = false;
};

struct MonoMode {
  u64 tick = 0;
  u8 channel = 0;
  u8 channels = 1;
};

struct EndOfTrack {
  u64 tick = 0;
};

struct Marker {
  u64 tick = 0;
  std::string text;
};

using MidiEvent = std::variant<NoteOn, NoteOff, NoteDuration, Tempo, MidiPort, ProgramChange, BankSelect, Volume,
                               Volume14, Pan, Expression, Expression14, MasterVolume, Reverb, FineTune, CoarseTune,
                               PitchBend, PitchBendRange, VibratoDepth, VibratoFrequency, VibratoDelay, TremoloDepth,
                               TremoloFrequency, TremoloDelay, PortamentoEnable, PortamentoTime, PortamentoTime14,
                               PortamentoControl, LegatoPedal, MonoMode, EndOfTrack, Marker>;

struct MidiTrack {
  // Empty names are valid; the exporter will omit track-name meta events.
  std::string name;
  std::vector<MidiEvent> events;
};

struct MidiSequence {
  Timebase timebase;
  std::vector<MidiTrack> tracks;
  // Keep export warnings with the rendered MIDI data so callers can still receive a usable file.
  std::vector<Diagnostic> diagnostics;
};

}  // namespace vgmtrans::core
