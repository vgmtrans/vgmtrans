/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/midi/MidiModel.h"

#include <optional>
#include <span>
#include <vector>

inline const vgmtrans::core::NoteDuration* midiNote(const vgmtrans::core::MidiEvent& event) {
  return std::get_if<vgmtrans::core::NoteDuration>(&event.payload);
}

inline const vgmtrans::core::BankSelect* midiBankSelect(const vgmtrans::core::MidiEvent& event) {
  return std::get_if<vgmtrans::core::BankSelect>(&event.payload);
}

inline const vgmtrans::core::MidiChannelMessage* midiController(const vgmtrans::core::MidiEvent& event,
                                                                vgmtrans::core::MidiController controller) {
  const auto* message = std::get_if<vgmtrans::core::MidiChannelMessage>(&event.payload);
  return message != nullptr && message->kind == vgmtrans::core::MidiChannelMessageKind::ControlChange &&
                 message->parameter == static_cast<u8>(controller)
             ? message
             : nullptr;
}

inline bool isMidiController(const vgmtrans::core::MidiEvent& event, vgmtrans::core::MidiController controller) {
  return midiController(event, controller) != nullptr;
}

inline bool isMidiControllerLsb(const vgmtrans::core::MidiEvent& event, vgmtrans::core::MidiController msbController) {
  return isMidiController(event, static_cast<vgmtrans::core::MidiController>(static_cast<u8>(msbController) + 32));
}

inline const vgmtrans::core::MidiChannelMessage* midiChannelMessage(const vgmtrans::core::MidiEvent& event,
                                                                    vgmtrans::core::MidiChannelMessageKind kind) {
  const auto* message = std::get_if<vgmtrans::core::MidiChannelMessage>(&event.payload);
  return message != nullptr && message->kind == kind ? message : nullptr;
}

inline bool isMidiChannelMessage(const vgmtrans::core::MidiEvent& event, vgmtrans::core::MidiChannelMessageKind kind) {
  return midiChannelMessage(event, kind) != nullptr;
}

inline const vgmtrans::core::MidiMetaMessage* midiMeta(const vgmtrans::core::MidiEvent& event, u8 type) {
  const auto* message = std::get_if<vgmtrans::core::MidiMetaMessage>(&event.payload);
  return message != nullptr && message->type == type ? message : nullptr;
}

inline std::optional<u32> midiTempo(const vgmtrans::core::MidiEvent& event) {
  const auto* message = midiMeta(event, 0x51);
  if (message == nullptr || message->data.size() != 3) {
    return std::nullopt;
  }
  return (static_cast<u32>(message->data[0]) << 16) | (static_cast<u32>(message->data[1]) << 8) | message->data[2];
}

inline std::optional<u8> midiPort(const vgmtrans::core::MidiEvent& event) {
  const auto* message = midiMeta(event, 0x21);
  return message != nullptr && message->data.size() == 1 ? std::optional{message->data[0]} : std::nullopt;
}

inline std::optional<u16> midiMasterVolume(const vgmtrans::core::MidiEvent& event) {
  const auto* message = std::get_if<vgmtrans::core::MidiSysExMessage>(&event.payload);
  if (message == nullptr || message->data.size() != 7 || message->data[0] != 0x7f || message->data[2] != 0x04 ||
      message->data[3] != 0x01) {
    return std::nullopt;
  }
  return static_cast<u16>(message->data[4] | (message->data[5] << 7));
}

struct MidiNoteView {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
  u8 velocity = 0;
  u32 duration = 0;
};

inline std::vector<MidiNoteView> midiNotes(std::span<const vgmtrans::core::MidiEvent> events) {
  std::vector<MidiNoteView> result;
  for (const auto& event : events) {
    if (const auto* note = midiNote(event)) {
      result.push_back(MidiNoteView{.tick = event.tick,
                                    .channel = note->channel,
                                    .key = note->key,
                                    .velocity = note->velocity,
                                    .duration = note->duration});
    }
  }
  return result;
}

struct MidiRpnView {
  u64 tick = 0;
  u8 channel = 0;
  u8 parameterMsb = 0;
  u8 parameterLsb = 0;
  u16 value = 0;
};

inline std::vector<MidiRpnView> midiRpns(std::span<const vgmtrans::core::MidiEvent> events) {
  using namespace vgmtrans::core;
  std::vector<MidiRpnView> result;
  for (size_t index = 0; index + 3 < events.size(); ++index) {
    const auto* parameterMsb = midiController(events[index], MidiController::RpnParameterMsb);
    const auto* parameterLsb = midiController(events[index + 1], MidiController::RpnParameterLsb);
    const auto* dataMsb = midiController(events[index + 2], MidiController::RpnDataMsb);
    const auto* dataLsb = midiController(events[index + 3], MidiController::RpnDataLsb);
    if (parameterMsb == nullptr || parameterLsb == nullptr || dataMsb == nullptr || dataLsb == nullptr ||
        events[index].tick != events[index + 1].tick || events[index].tick != events[index + 2].tick ||
        events[index].tick != events[index + 3].tick || parameterMsb->channel != parameterLsb->channel ||
        parameterMsb->channel != dataMsb->channel || parameterMsb->channel != dataLsb->channel) {
      continue;
    }
    result.push_back(MidiRpnView{
        .tick = events[index].tick,
        .channel = parameterMsb->channel,
        .parameterMsb = static_cast<u8>(parameterMsb->value),
        .parameterLsb = static_cast<u8>(parameterLsb->value),
        .value = static_cast<u16>((dataMsb->value << 7) | dataLsb->value),
    });
    index += 3;
  }
  return result;
}

inline std::vector<std::pair<u64, u16>> midiPitchBendRanges(std::span<const vgmtrans::core::MidiEvent> events) {
  std::vector<std::pair<u64, u16>> result;
  for (const auto& rpn : midiRpns(events)) {
    if (rpn.parameterMsb == 0 && rpn.parameterLsb == 0) {
      result.emplace_back(rpn.tick, static_cast<u16>((rpn.value >> 7) * 100 + (rpn.value & 0x7f)));
    }
  }
  return result;
}

inline std::optional<u16> firstMidiController14(std::span<const vgmtrans::core::MidiEvent> events,
                                                vgmtrans::core::MidiController msb) {
  const auto lsb = static_cast<vgmtrans::core::MidiController>(static_cast<u8>(msb) + 32);
  for (size_t index = 0; index + 1 < events.size(); ++index) {
    const auto* firstMsb = midiController(events[index], msb);
    const auto* firstLsb = midiController(events[index + 1], lsb);
    if (firstMsb != nullptr && firstLsb != nullptr && events[index].tick == events[index + 1].tick &&
        firstMsb->channel == firstLsb->channel) {
      return static_cast<u16>((firstMsb->value << 7) | firstLsb->value);
    }
    const auto* firstLsbReversed = midiController(events[index], lsb);
    const auto* firstMsbReversed = midiController(events[index + 1], msb);
    if (firstLsbReversed != nullptr && firstMsbReversed != nullptr && events[index].tick == events[index + 1].tick &&
        firstLsbReversed->channel == firstMsbReversed->channel) {
      return static_cast<u16>((firstMsbReversed->value << 7) | firstLsbReversed->value);
    }
  }
  return std::nullopt;
}
