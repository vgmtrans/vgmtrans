/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/MidiModel.h"

#include <utility>

namespace vgmtrans::core::midi {

namespace {

MidiEvent channelMessage(u64 tick, MidiChannelMessageKind kind, u8 channel, s32 parameter, s32 value, int priority) {
  return {tick, MidiChannelMessage{kind, channel, parameter, value, priority}};
}

}  // namespace

MidiEvent note(u64 tick, u8 channel, u8 key, u8 velocity, u32 duration) {
  return {tick, NoteDuration{channel, key, velocity, duration}};
}

MidiEvent bankSelect(u64 tick, u8 channel, u16 bank, bool writeLsb) {
  return {tick, BankSelect{channel, bank, writeLsb}};
}

MidiEvent controller(u64 tick, u8 channel, MidiController controllerNumber, s32 value, int priority, MidiValueUnit unit,
                     std::optional<double> normalizedAmount) {
  MidiEvent event = channelMessage(tick, MidiChannelMessageKind::ControlChange, channel,
                                   static_cast<u8>(controllerNumber), value, priority);
  auto& message = std::get<MidiChannelMessage>(event.payload);
  message.valueUnit = unit;
  message.normalizedAmount = normalizedAmount;
  return event;
}

MidiEvent programChange(u64 tick, u8 channel, u8 program) {
  return channelMessage(tick, MidiChannelMessageKind::ProgramChange, channel, 0, program, 15);
}

MidiEvent pitchBend(u64 tick, u8 channel, s16 value) {
  return channelMessage(tick, MidiChannelMessageKind::PitchBend, channel, 0, value, 25);
}

MidiEvent meta(u64 tick, u8 type, std::vector<u8> data, int priority) {
  return {tick, MidiMetaMessage{type, std::move(data), priority}};
}

MidiEvent sysex(u64 tick, std::vector<u8> data, int priority) {
  return {tick, MidiSysExMessage{std::move(data), priority}};
}

void appendController14(MidiTrack& track, u64 tick, u8 channel, MidiController msb, MidiController lsb, u16 value,
                        bool lsbFirst) {
  const MidiEvent most = controller(tick, channel, msb, (value >> 7) & 0x7f);
  const MidiEvent least = controller(tick, channel, lsb, value & 0x7f);
  if (lsbFirst) {
    track.events.push_back(least);
    track.events.push_back(most);
  } else {
    track.events.push_back(most);
    track.events.push_back(least);
  }
}

void appendRpn(MidiTrack& track, u64 tick, u8 channel, u8 parameterMsb, u8 parameterLsb, u16 value, int priority) {
  track.events.push_back(controller(tick, channel, MidiController::RpnParameterMsb, parameterMsb, priority));
  track.events.push_back(controller(tick, channel, MidiController::RpnParameterLsb, parameterLsb, priority));
  track.events.push_back(controller(tick, channel, MidiController::RpnDataMsb, (value >> 7) & 0x7f, priority));
  track.events.push_back(controller(tick, channel, MidiController::RpnDataLsb, value & 0x7f, priority));
}

}  // namespace vgmtrans::core::midi
