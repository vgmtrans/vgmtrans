/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// Compact MIDI target used after performance rendering. Most payloads are
// already MIDI messages; only duration notes and logical bank selection remain
// structured because later export stages still operate on them.

enum class MidiChannelMessageKind : u8 {
  ControlChange,
  ProgramChange,
  PitchBend,
};

enum class MidiController : u8 {
  BankSelectMsb = 0,
  Modulation = 1,
  PortamentoTime = 5,
  RpnDataMsb = 6,
  ChannelVolume = 7,
  Pan = 10,
  Expression = 11,
  BankSelectLsb = 32,
  RpnDataLsb = 38,
  Portamento = 65,
  Legato = 68,
  TremoloRate = 75,
  VibratoRate = 76,
  VibratoDelay = 78,
  TremoloDelay = 79,
  PortamentoControl = 84,
  Reverb = 91,
  TremoloDepth = 92,
  RpnParameterLsb = 100,
  RpnParameterMsb = 101,
  AllSoundOff = 120,
  MonoMode = 126,
};

struct MidiChannelMessage {
  MidiChannelMessageKind kind = MidiChannelMessageKind::ControlChange;
  u8 channel = 0;
  u8 parameter = 0;
  s32 value = 0;
  // Physical modulation scaling may revise four controller values after tracks
  // have been combined. Other channel messages leave this empty.
  std::optional<double> normalizedAmount;
};

struct NoteDuration {
  u8 channel = 0;
  u8 key = 0;
  u8 velocity = 0;
  u32 duration = 0;
};

struct BankSelect {
  u8 channel = 0;
  // Logical bank; the exporter applies the selected MSB-only or MSB/LSB encoding.
  u16 bank = 0;
  bool writeLsb = true;
};

struct MidiMetaMessage {
  u8 type = 0;
  std::vector<u8> data;
};

struct MidiSysExMessage {
  // Data excludes the leading F0 status byte and includes any terminating F7.
  std::vector<u8> data;
};

struct MidiEvent {
  u64 tick = 0;
  int priority = 20;
  std::variant<NoteDuration, BankSelect, MidiChannelMessage, MidiMetaMessage, MidiSysExMessage> payload;
};

struct MidiTrack {
  // Empty names are valid; the exporter will omit track-name meta events.
  std::string name;
  std::vector<MidiEvent> events;
  u64 endTick = 0;
};

struct MidiSequence {
  Timebase timebase;
  std::vector<MidiTrack> tracks;
  // Keep export warnings with the rendered MIDI data so callers can still receive a usable file.
  std::vector<Diagnostic> diagnostics;
};

namespace midi {

[[nodiscard]] MidiEvent note(u64 tick, u8 channel, u8 key, u8 velocity, u32 duration);
[[nodiscard]] MidiEvent bankSelect(u64 tick, u8 channel, u16 bank, bool writeLsb);
[[nodiscard]] MidiEvent controller(u64 tick, u8 channel, MidiController controller, s32 value, int priority = 20,
                                   std::optional<double> normalizedAmount = std::nullopt);
[[nodiscard]] MidiEvent programChange(u64 tick, u8 channel, u8 program);
[[nodiscard]] MidiEvent pitchBend(u64 tick, u8 channel, s16 value);
[[nodiscard]] MidiEvent meta(u64 tick, u8 type, std::vector<u8> data, int priority = 0);
[[nodiscard]] MidiEvent sysex(u64 tick, std::vector<u8> data, int priority = 5);

void appendController14(MidiTrack& track, u64 tick, u8 channel, MidiController msb, u16 value, bool lsbFirst = false);
void appendRpn(MidiTrack& track, u64 tick, u8 channel, u8 parameterMsb, u8 parameterLsb, u16 value, int priority = 18);

}  // namespace midi

}  // namespace vgmtrans::core
