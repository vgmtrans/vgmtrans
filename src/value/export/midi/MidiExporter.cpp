/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/MidiExporter.h"
#include "value/export/BinaryWriter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
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
    if ((buffer & 0x80) != 0) {
      buffer >>= 8;
    } else {
      break;
    }
  }
}

[[nodiscard]] u8 data7(u32 value) {
  return static_cast<u8>(std::min<u32>(value, 127));
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

void addController(std::vector<MidiMessage>& messages, u64 tick, u8 channel, u8 controller, u8 value,
                   int priority = 20) {
  addMessage(messages, tick, priority, {static_cast<u8>(0xb0 | channel4(channel)), controller, data7(value)});
}

void addRpn(std::vector<MidiMessage>& messages, u64 tick, u8 channel, u8 parameterMsb, u8 parameterLsb, u16 value,
            int priority = 18) {
  // RPN writes are emitted as controller sequences. Callers use this for pitch bend range
  // and fine/coarse tuning because SMF has no shorter channel event for them.
  addController(messages, tick, channel, 101, parameterMsb, priority);
  addController(messages, tick, channel, 100, parameterLsb, priority);
  addController(messages, tick, channel, 6, static_cast<u8>((value >> 7) & 0x7f), priority);
  addController(messages, tick, channel, 38, static_cast<u8>(value & 0x7f), priority);
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

[[nodiscard]] u8 denominatorPower(u8 denominator) {
  if (denominator == 0) {
    return 0;
  }
  constexpr double ln2 = 0.69314718055994530942;
  return data7(static_cast<u32>(std::log(static_cast<double>(denominator)) / ln2));
}

void addEventMessages(std::vector<MidiMessage>& messages, const MidiEvent& event, u64& endTick) {
  // Convert one MidiEvent to raw SMF messages. Source-driver interpretation should
  // already be finished before this point.
  std::visit(
      [&](const auto& typedEvent) {
        using TypedEvent = std::decay_t<decltype(typedEvent)>;
        if constexpr (std::is_same_v<TypedEvent, NoteOn>) {
          addMessage(
              messages, typedEvent.tick, 50,
              {static_cast<u8>(0x90 | channel4(typedEvent.channel)), data7(typedEvent.key), typedEvent.velocity});
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, NoteOff>) {
          addMessage(
              messages, typedEvent.tick, 40,
              {static_cast<u8>(0x80 | channel4(typedEvent.channel)), data7(typedEvent.key), typedEvent.velocity});
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, NoteDuration>) {
          addMessage(
              messages, typedEvent.tick, 50,
              {static_cast<u8>(0x90 | channel4(typedEvent.channel)), data7(typedEvent.key), typedEvent.velocity});
          addMessage(messages, typedEvent.tick + typedEvent.duration, 40,
                     {static_cast<u8>(0x80 | channel4(typedEvent.channel)), data7(typedEvent.key), 64});
          endTick = std::max(endTick, typedEvent.tick + typedEvent.duration);
        } else if constexpr (std::is_same_v<TypedEvent, Tempo>) {
          const std::array<u8, 3> tempoBytes{
              static_cast<u8>((typedEvent.microsecondsPerQuarter >> 16) & 0xff),
              static_cast<u8>((typedEvent.microsecondsPerQuarter >> 8) & 0xff),
              static_cast<u8>(typedEvent.microsecondsPerQuarter & 0xff),
          };
          addMessage(messages, typedEvent.tick, 0, metaEvent(0x51, tempoBytes));
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, TimeSignature>) {
          const std::array<u8, 4> timeSignatureBytes{
              typedEvent.numerator,
              denominatorPower(typedEvent.denominator),
              typedEvent.clocksPerMetronomeClick,
              8,
          };
          addMessage(messages, typedEvent.tick, 0, metaEvent(0x58, timeSignatureBytes));
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, MidiPort>) {
          const std::array<u8, 1> portBytes{typedEvent.port};
          addMessage(messages, typedEvent.tick, -5, metaEvent(0x21, portBytes));
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, ProgramChange>) {
          addMessage(messages, typedEvent.tick, 15,
                     {static_cast<u8>(0xc0 | channel4(typedEvent.channel)), data7(typedEvent.program)});
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, BankSelect>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 0,
                        static_cast<u8>((typedEvent.bank >> 7) & 0x7f), 15);
          if (typedEvent.writeLsb) {
            addController(messages, typedEvent.tick, typedEvent.channel, 32, static_cast<u8>(typedEvent.bank & 0x7f),
                          15);
          }
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, Volume>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 7, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, Volume14>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 7,
                        static_cast<u8>((typedEvent.value >> 7) & 0x7f), 20);
          addController(messages, typedEvent.tick, typedEvent.channel, 39, static_cast<u8>(typedEvent.value & 0x7f),
                        20);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, Pan>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 10, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, Expression>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 11, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, Expression14>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 11,
                        static_cast<u8>((typedEvent.value >> 7) & 0x7f), 20);
          addController(messages, typedEvent.tick, typedEvent.channel, 43, static_cast<u8>(typedEvent.value & 0x7f),
                        20);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, MasterVolume>) {
          const std::array<u8, 7> payload{
              0x7f,
              0x7f,
              0x04,
              0x01,
              static_cast<u8>(typedEvent.value & 0x7f),
              static_cast<u8>((typedEvent.value >> 7) & 0x7f),
              0xf7,
          };
          addMessage(messages, typedEvent.tick, 5, sysexEvent(payload));
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, Reverb>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 91, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, FineTune>) {
          const double semitones = std::clamp(typedEvent.cents / 100.0, -1.0, 1.0);
          const s32 value = std::min(static_cast<int>(std::lround(8192 * semitones)), 8191) + 8192;
          addRpn(messages, typedEvent.tick, typedEvent.channel, 0, 1, static_cast<u16>(value), 8);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, CoarseTune>) {
          const s32 value = std::clamp<s32>((typedEvent.semitones + 64) << 7, 0, 16383);
          addRpn(messages, typedEvent.tick, typedEvent.channel, 0, 2, static_cast<u16>(value), 8);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, PitchBend>) {
          const s32 value = std::clamp<s32>(typedEvent.value + 0x2000, 0, 0x3fff);
          addMessage(messages, typedEvent.tick, 25,
                     {static_cast<u8>(0xe0 | channel4(typedEvent.channel)), static_cast<u8>(value & 0x7f),
                      static_cast<u8>((value >> 7) & 0x7f)});
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, PitchBendRange>) {
          const u8 semitones = static_cast<u8>(std::min<u16>(typedEvent.cents / 100, 127));
          const u8 fineCents = static_cast<u8>(std::min<u16>(typedEvent.cents % 100, 127));
          addRpn(messages, typedEvent.tick, typedEvent.channel, 0, 0, static_cast<u16>((semitones << 7) | fineCents));
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, VibratoDepth>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 1, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, VibratoFrequency>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 76, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, VibratoDelay>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 78, data7(typedEvent.ticks));
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, TremoloDepth>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 92, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, TremoloFrequency>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 75, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, TremoloDelay>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 79, data7(typedEvent.ticks));
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoEnable>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 65, typedEvent.enabled ? 127 : 0);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoTime>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 5, typedEvent.value);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoTime14>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 5,
                        static_cast<u8>((typedEvent.value >> 7) & 0x7f), 20);
          addController(messages, typedEvent.tick, typedEvent.channel, 37, static_cast<u8>(typedEvent.value & 0x7f),
                        20);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoControl>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 84, typedEvent.key);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, LegatoPedal>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 68, typedEvent.enabled ? 127 : 0);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, MonoMode>) {
          addController(messages, typedEvent.tick, typedEvent.channel, 126, typedEvent.channels);
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, EndOfTrack>) {
          endTick = std::max(endTick, typedEvent.tick);
        } else if constexpr (std::is_same_v<TypedEvent, Marker>) {
          addMessage(messages, typedEvent.tick, 90, textMetaEvent(0x06, typedEvent.text));
          endTick = std::max(endTick, typedEvent.tick);
        }
      },
      event);
}

[[nodiscard]] std::vector<u8> writeTrack(const MidiTrack& track) {
  // Convert absolute event ticks to SMF delta times after sorting all generated messages.
  std::vector<MidiMessage> messages;
  u64 endTick = 0;

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
