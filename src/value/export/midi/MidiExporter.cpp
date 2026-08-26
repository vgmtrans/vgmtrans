/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/MidiExporter.h"
#include "value/export/BinaryWriter.h"

#include <algorithm>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

namespace {

struct MidiMessage {
  // Priority orders simultaneous events into stable MIDI-friendly order. For example,
  // bank select should precede program changes, and end-of-track should be last.
  u64 tick = 0;
  int priority = 0;
  size_t sequence = 0;
  std::vector<u8> bytes;
};

void writeVariableLength(std::vector<u8>& bytes, u64 value) {
  // Standard MIDI files encode delta times as big-endian base-128 variable-length values.
  u64 buffer = value & 0x7f;
  while ((value >>= 7) != 0) {
    buffer <<= 8;
    buffer |= ((value & 0x7f) | 0x80);
  }
  while (true) {
    bytes.push_back(static_cast<u8>(buffer & 0xff));
    if ((buffer & 0x80) == 0) {
      break;
    }
    buffer >>= 8;
  }
}

[[nodiscard]] u8 data7(s32 value) {
  return static_cast<u8>(std::clamp<s32>(value, 0, 127));
}

[[nodiscard]] u8 channel4(u8 channel) {
  return static_cast<u8>(channel & 0x0f);
}

void addMessage(std::vector<MidiMessage>& messages, u64 tick, int priority, std::vector<u8> bytes) {
  messages.push_back(MidiMessage{
      .tick = tick,
      .priority = priority,
      .sequence = messages.size(),
      .bytes = std::move(bytes),
  });
}

[[nodiscard]] std::vector<u8> metaEvent(u8 type, std::span<const u8> payload) {
  std::vector<u8> bytes{0xff, type};
  writeVariableLength(bytes, payload.size());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

[[nodiscard]] std::vector<u8> textMetaEvent(u8 type, const std::string& text) {
  const auto* data = reinterpret_cast<const u8*>(text.data());
  return metaEvent(type, std::span<const u8>(data, text.size()));
}

[[nodiscard]] std::vector<u8> sysexEvent(std::span<const u8> payload) {
  std::vector<u8> bytes{0xf0};
  writeVariableLength(bytes, payload.size());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

void addChannelMessage(std::vector<MidiMessage>& messages, u64 tick, int priority, const MidiChannelMessage& message) {
  const u8 channel = channel4(message.channel);
  switch (message.kind) {
    case MidiChannelMessageKind::ControlChange:
      addMessage(messages, tick, priority, {static_cast<u8>(0xb0 | channel), message.parameter, data7(message.value)});
      return;
    case MidiChannelMessageKind::ProgramChange:
      addMessage(messages, tick, priority, {static_cast<u8>(0xc0 | channel), data7(message.value)});
      return;
    case MidiChannelMessageKind::PitchBend: {
      const s32 value = std::clamp<s32>(message.value + 0x2000, 0, 0x3fff);
      addMessage(
          messages, tick, priority,
          {static_cast<u8>(0xe0 | channel), static_cast<u8>(value & 0x7f), static_cast<u8>((value >> 7) & 0x7f)});
      return;
    }
  }
}

void addEventMessages(std::vector<MidiMessage>& messages, const MidiEvent& event, u64& endTick) {
  std::visit(
      [&](const auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, NoteDuration>) {
          // Keep a zero-length note's on/off pair in source order. Sorting its
          // off before its on would leave the attack hanging until a later release.
          const int noteOnPriority = payload.duration == 0 ? 40 : event.priority;
          addMessage(messages, event.tick, noteOnPriority,
                     {static_cast<u8>(0x90 | channel4(payload.channel)), data7(payload.key), payload.velocity});
          addMessage(messages, event.tick + payload.duration, 40,
                     {static_cast<u8>(0x80 | channel4(payload.channel)), data7(payload.key), 64});
          endTick = std::max(endTick, event.tick + payload.duration);
        } else if constexpr (std::is_same_v<Payload, BankSelect>) {
          const s32 bankMsb = payload.writeLsb ? (payload.bank >> 7) & 0x7f : payload.bank & 0x7f;
          addChannelMessage(messages, event.tick, event.priority,
                            MidiChannelMessage{.kind = MidiChannelMessageKind::ControlChange,
                                               .channel = payload.channel,
                                               .parameter = static_cast<u8>(MidiController::BankSelectMsb),
                                               .value = bankMsb});
          if (payload.writeLsb) {
            addChannelMessage(messages, event.tick, event.priority,
                              MidiChannelMessage{.kind = MidiChannelMessageKind::ControlChange,
                                                 .channel = payload.channel,
                                                 .parameter = static_cast<u8>(MidiController::BankSelectLsb),
                                                 .value = payload.bank & 0x7f});
          }
        } else if constexpr (std::is_same_v<Payload, MidiChannelMessage>) {
          addChannelMessage(messages, event.tick, event.priority, payload);
        } else if constexpr (std::is_same_v<Payload, MidiMetaMessage>) {
          addMessage(messages, event.tick, event.priority, metaEvent(payload.type, payload.data));
        } else if constexpr (std::is_same_v<Payload, MidiSysExMessage>) {
          addMessage(messages, event.tick, event.priority, sysexEvent(payload.data));
        }
      },
      event.payload);
  endTick = std::max(endTick, event.tick);
}

[[nodiscard]] std::vector<u8> writeTrack(const MidiTrack& track) {
  // Convert absolute event ticks to SMF delta times after sorting all generated messages.
  std::vector<MidiMessage> messages;
  u64 endTick = track.endTick;
  if (!track.name.empty()) {
    addMessage(messages, 0, -10, textMetaEvent(0x03, track.name));
  }
  for (const auto& event : track.events) {
    addEventMessages(messages, event, endTick);
  }
  addMessage(messages, endTick, 1000, metaEvent(0x2f, std::span<const u8>()));

  std::ranges::stable_sort(messages, [](const MidiMessage& a, const MidiMessage& b) {
    if (a.tick != b.tick) {
      return a.tick < b.tick;
    }
    if (a.priority != b.priority) {
      return a.priority < b.priority;
    }
    return a.sequence < b.sequence;
  });

  std::vector<u8> trackData;
  u64 previousTick = 0;
  for (const auto& message : messages) {
    writeVariableLength(trackData, message.tick - previousTick);
    trackData.insert(trackData.end(), message.bytes.begin(), message.bytes.end());
    previousTick = message.tick;
  }

  std::vector<u8> bytes;
  writeAscii(bytes, "MTrk");
  writeBe32(bytes, static_cast<u32>(trackData.size()));
  bytes.insert(bytes.end(), trackData.begin(), trackData.end());
  return bytes;
}

}  // namespace

std::vector<u8> encodeMidiFile(const MidiSequence& sequence) {
  std::vector<u8> bytes;
  writeAscii(bytes, "MThd");
  writeBe32(bytes, 6);
  writeBe16(bytes, 1);
  writeBe16(bytes, static_cast<u16>(sequence.tracks.size()));
  writeBe16(bytes, static_cast<u16>(sequence.timebase.ppqn));

  for (const auto& track : sequence.tracks) {
    auto trackBytes = writeTrack(track);
    bytes.insert(bytes.end(), trackBytes.begin(), trackBytes.end());
  }
  return bytes;
}

}  // namespace vgmtrans::core
