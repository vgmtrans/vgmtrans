/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MetadataModel.h"

#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// MidiModel is the performance layer used by MIDI-like exports. It is intentionally
// lower-level than PerformanceSequence: driver loops have been resolved according to
// policy, and controller values are already quantized to MIDI-friendly units.

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
  // Duration form is easier for lowerings to manipulate; MidiExporter expands it to
  // note-on/note-off events when writing a Standard MIDI File.
  u32 duration = 0;
};

struct Tempo {
  u64 tick = 0;
  u32 microsecondsPerQuarter = 500000;
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

using MidiEvent =
    std::variant<NoteOn, NoteOff, NoteDuration, Tempo, ProgramChange, BankSelect, Volume, Volume14, Pan, Expression,
                 Expression14, MasterVolume, Reverb, FineTune, CoarseTune, PitchBend, PitchBendRange, VibratoDepth,
                 VibratoFrequency, VibratoDelay, TremoloDepth, TremoloFrequency, TremoloDelay, PortamentoEnable,
                 PortamentoTime, PortamentoTime14, PortamentoControl, LegatoPedal, MonoMode, EndOfTrack, Marker>;

struct MidiTrack {
  // Empty names are valid; the exporter will omit track-name meta events.
  std::string name;
  std::vector<MidiEvent> events;
};

struct MidiSequence {
  Timebase timebase;
  std::vector<MidiTrack> tracks;
  // Diagnostics are kept with the lowered sequence so MIDI export can still return a
  // usable artifact with warnings rather than forcing scan-time failure.
  std::vector<Diagnostic> diagnostics;
};

}  // namespace vgmtrans::core
