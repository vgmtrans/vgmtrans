/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CompileSnes/CompileSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace vgmtrans::formats::compile_snes {

using namespace core;

namespace {

constexpr PitchBendLayerId kTuningAndVibratoLayer{1};

constexpr std::array<u8, 32> kVolumeTable{
    0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0a, 0x0c, 0x0d, 0x0f, 0x12, 0x14, 0x17, 0x1a, 0x1c,
    0x1f, 0x24, 0x28, 0x2d, 0x31, 0x36, 0x3a, 0x3f, 0x47, 0x4f, 0x57, 0x5f, 0x67, 0x6f, 0x77, 0x7f,
};

enum class Curve : u8 {
  Volume,
  Vibrato,
  Gain,
  Pan,
  Echo,
};

[[nodiscard]] u16 pointer(ByteReader reader, u16 list, u8 index) {
  const u32 address = list + index * 2u;
  return reader.has(address, 2) ? reader.le16(address) : u16{0};
}

struct TrackHeader {
  u8 channel = 0;
  u8 flags = 0;
  u8 volume = 0;
  u8 volumeEnvelope = 0;
  u8 vibrato = 0;
  s8 transpose = 0;
  u8 tempo = 0;
  u8 branchId = 0;
  u8 program = 0;
  u8 adsr = 0;
  s8 pan = 0;
};

// A bounded, wrapping view of one SPC700 driver table.
class DriverTable {
public:
  DriverTable() = default;
  DriverTable(ByteReader reader, u16 address, size_t size) : reader_(reader), address_(address), size_(size) {}

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_t size() const noexcept { return size_; }
  [[nodiscard]] u16 address() const noexcept { return address_; }
  [[nodiscard]] u8 operator[](size_t index) const {
    return reader_.u8At(static_cast<u16>(address_ + static_cast<u16>(index)));
  }

private:
  ByteReader reader_;
  u16 address_ = 0;
  size_t size_ = 0;
};

// Typed access to the original driver tables replaces the private packed-word
// image previously assembled solely for deferred playback.
class DriverData {
public:
  DriverData(RetainedSource source, Layout layout) : source_(std::move(source)), layout_(std::move(layout)) {}

  [[nodiscard]] bool early() const noexcept { return layout_.early(); }
  [[nodiscard]] bool echoCapable() const noexcept { return layout_.hasEchoCommands(); }
  [[nodiscard]] bool stereoEnabled() const noexcept { return layout_.stereoEnabled; }

  [[nodiscard]] TrackHeader trackHeader(u32 index) const {
    const ByteReader reader = source_.reader();
    const u32 item = layout_.songHeaderAddress + 1 + index * 14u;
    if (!reader.has(item, 14)) {
      return {};
    }
    return TrackHeader{
        .channel = reader.u8At(item),
        .flags = reader.u8At(item + 1),
        .volume = reader.u8At(item + 2),
        .volumeEnvelope = reader.u8At(item + 3),
        .vibrato = reader.u8At(item + 4),
        .transpose = static_cast<s8>(reader.u8At(item + 5) + layout_.globalTranspose),
        .tempo = reader.u8At(item + 6),
        .branchId = reader.u8At(item + 7),
        .program = reader.u8At(item + 10),
        .adsr = reader.u8At(item + 11),
        .pan = static_cast<s8>(reader.u8At(item + 12)),
    };
  }

  [[nodiscard]] InstrumentInfo instrument(u8 program) const {
    const ByteReader reader = source_.reader();
    return readInstrumentInfo(reader, layout_, program)
        .value_or(InstrumentInfo{.program = program, .pitchTableAddress = layout_.regularPitchTableAddress});
  }

  [[nodiscard]] u16 pitch(const InstrumentInfo& instrument, u8 key) const {
    return instrumentPitch(source_.reader(), instrument, key);
  }

  [[nodiscard]] double unityKey(const InstrumentInfo& instrument) const {
    return instrumentUnityKey(source_.reader(), instrument);
  }

  [[nodiscard]] u16 adsr(u8 index) const {
    const ByteReader reader = source_.reader();
    const u32 address = layout_.adsrTableAddress + index * 2u;
    return index < 128 && reader.has(address, 2) ? reader.le16(address) : u16{0};
  }

  [[nodiscard]] std::pair<u32, u32> percussion(u8 index) const {
    const ByteReader reader = source_.reader();
    const u32 address = layout_.percussionTableAddress + index * 8u;
    return index < 30 && reader.has(address, 8) ? std::pair{reader.le32(address), reader.le32(address + 4)}
                                                : std::pair<u32, u32>{};
  }

  [[nodiscard]] DriverTable curve(Curve kind, u8 index) const {
    u16 list = 0;
    size_t size = 256;
    switch (kind) {
      case Curve::Volume:
        list = layout_.volumeEnvelopeTableAddress;
        break;
      case Curve::Vibrato:
        list = layout_.vibratoTableAddress;
        break;
      case Curve::Gain:
        list = layout_.gainEnvelopeTableAddress;
        break;
      case Curve::Pan:
        list = layout_.panEnvelopeTableAddress;
        break;
      case Curve::Echo:
        list = layout_.echoPresetTableAddress;
        size = 16;
        break;
    }
    const ByteReader reader = source_.reader();
    const u16 address = pointer(reader, list, index);
    return address != 0 && reader.size() == kAramSize ? DriverTable{reader, address, size} : DriverTable{};
  }

private:
  RetainedSource source_;
  Layout layout_{};
};

namespace math {

[[nodiscard]] u32 ticks(u8 value) {
  return value == 0 ? 256 : value;
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 tempo) {
  // Timer 1 target $80 gives one driver frame every 16 ms. The sequence
  // accumulator advances by tempo once per frame and ticks on carry.
  return 49'152'000u / (tempo == 0 ? 256u : tempo);
}

[[nodiscard]] u32 driverFrames(u32 sourceTicks, u8 tempo, u8 accumulator) {
  if (tempo == 0) {
    return sourceTicks;
  }
  const u64 distance = static_cast<u64>(sourceTicks) * 0x100u - accumulator;
  return static_cast<u32>((distance + tempo - 1u) / tempo);
}

[[nodiscard]] u16 advancePortamento(u16 pitch, u16 target, u8 divisor) {
  if (pitch == target || divisor == 0) {
    return pitch;
  }
  const u8 high = static_cast<u8>(pitch >> 8);
  // $0f98 divides half the current DSP pitch by $0490+x, using a
  // quantized 16-bit fallback when the quotient does not fit in A.
  const u16 half = high == static_cast<u8>(target >> 8) ? pitch >> 1 : (high * 0x101u) >> 1;
  const u16 step =
      (half >> 8) < divisor ? std::max<u16>(1, half / divisor) : static_cast<u16>(((half >> 5) / divisor) << 5);
  const u16 distance = pitch < target ? target - pitch : pitch - target;
  return distance <= step ? target : static_cast<u16>(pitch < target ? pitch + step : pitch - step);
}

[[nodiscard]] double volumeGain(u8 volume, u8 envelope) {
  const u8 logical = envelope == 0xff ? volume : static_cast<u8>((volume * (envelope + 1u)) >> 5);
  return kVolumeTable[std::min<u8>(logical, 31)] / 127.0;
}

[[nodiscard]] StereoBalance panGains(u8 pan, u8 phase = 0) {
  double left = (pan ^ 0x80) / 256.0;
  double right = (pan ^ 0x7f) / 256.0;
  if ((phase & 1) != 0) {
    left = -left;
  }
  if ((phase & 2) != 0) {
    right = -right;
  }
  return {.leftGain = left, .rightGain = right};
}

[[nodiscard]] double pitchSemitones(u16 base, s32 delta) {
  if (base == 0 || static_cast<s32>(base) + delta <= 0) {
    return 0.0;
  }
  return 12.0 * std::log2((static_cast<s32>(base) + delta) / static_cast<double>(base));
}

[[nodiscard]] u32 gateTicks(u32 duration, u8 gate) {
  if (gate == 0) {
    return duration;
  }
  if (gate < 0x80) {
    return std::min<u32>(duration, gate);
  }
  return std::max<u32>(1, duration > 0x100u - gate ? duration - (0x100u - gate) : 1);
}

}  // namespace math

struct CurveState {
  u8 index = 0;
  u8 value = 0;
  u16 counter = 1;
  s16 position = -1;

  void reset(u8 next, u8 initial = 0) {
    index = next;
    value = initial;
    counter = 1;
    position = -1;
  }

  [[nodiscard]] bool advance(const DriverTable& bytes, bool noteActive = false) {
    if (index == 0 || bytes.empty()) {
      return false;
    }
    if (counter > 1) {
      --counter;
      return false;
    }
    if (counter == 1) {
      counter = 0;
    }
    s32 cursor = position + 1;
    for (u32 guard = 0; guard < 32; ++guard) {
      if (cursor < 0 || static_cast<size_t>(cursor) >= bytes.size()) {
        index = 0;
        return false;
      }
      const u8 command = static_cast<u8>(bytes[cursor]);
      if (command < 0x80) {
        if (static_cast<size_t>(cursor + 1) >= bytes.size()) {
          index = 0;
          return false;
        }
        counter = command == 0 ? 0x100 : command;
        position = static_cast<s16>(cursor + 1);
        const u8 next = static_cast<u8>(bytes[cursor + 1]);
        const bool changed = next != value;
        value = next;
        return changed;
      }
      if (command == 0x80) {
        position = static_cast<s16>(cursor - 1);
        counter = 0x100;
        return false;
      }
      if (command == 0x81 && static_cast<size_t>(cursor + 1) < bytes.size()) {
        cursor -= static_cast<u8>(bytes[cursor + 1]);
        continue;
      }
      if (command == 0x82) {
        cursor = 0;
        continue;
      }
      if (command == 0x83) {
        if (noteActive) {
          position = static_cast<s16>(cursor - 1);
          counter = 1;
          return false;
        }
        ++cursor;
        continue;
      }
      if (noteActive && static_cast<size_t>(cursor + 1) < bytes.size()) {
        cursor -= static_cast<u8>(bytes[cursor + 1]);
      } else {
        ++cursor;
      }
    }
    return false;
  }
};

struct ProgramState {
  explicit ProgramState(const DriverData& data) : data(&data), echoCapable(data.echoCapable()) {
    if (!echoCapable) {
      return;
    }
    const auto preset = data.curve(Curve::Echo, 0);
    if (preset.size() >= 16) {
      echo.delayMilliseconds = (preset[1] & 0x0f) * 16.0;
      const StereoBalance pan = math::panGains(preset[3], preset[7]);
      echo.leftGain = preset[2] / 127.0 * pan.leftGain;
      echo.rightGain = preset[2] / 127.0 * pan.rightGain;
      echo.feedback = static_cast<s8>(preset[4]) / 128.0;
      echo.filterIndex = 0;
    }
  }

  const DriverData* data;
  bool echoCapable = false;
  bool echoGloballyEnabled = false;
  ReverbPerformanceEvent echo;
};

struct TrackState {
  TrackState(const TrackProgram& source, const DriverData& data)
      : trackNumber(source.sourceTrackNumber), early(data.early()), stereoEnabled(data.stereoEnabled()) {
    const TrackHeader header = data.trackHeader(source.sourceTrackNumber);
    channel = header.channel;
    headerFlags = header.flags;
    volume = static_cast<u8>(header.volume & 0x1f);
    volumeEnvelope.reset(header.volumeEnvelope, 31);
    vibrato.reset(header.vibrato, 0);
    transpose = header.transpose;
    tempo = header.tempo;
    tempoAccumulator = static_cast<u8>(tempo - 1u);
    branchId = header.branchId;
    program = header.program;
    adsr = header.adsr;
    pan = stereoEnabled ? header.pan : 0;
    slur = (headerFlags & 0x10) != 0;
  }

  u32 trackNumber = 0;
  u8 channel = 0;
  bool early = false;
  bool stereoEnabled = false;
  u8 headerFlags = 0;
  u8 volume = 31;
  s8 transpose = 0;
  u8 tempo = 0;
  u8 tempoAccumulator = 0;
  u8 branchId = 0;
  u8 program = 0;
  u8 adsr = 0;
  s8 pan = 0;
  u8 stereoPhase = 0;
  u8 gate = 0;
  u32 duration = 1;
  u8 rawNote = 0;
  u64 activeUntil = 0;
  bool slur = false;
  u8 portamentoDivisor = 0;
  bool retriggerNextNote = false;
  u16 currentPitch = 0;
  s16 tuning = 0;
  s8 pitchSweep = 0;
  std::array<u8, 256> loops{};
  CurveState volumeEnvelope;
  CurveState vibrato;
  CurveState gainEnvelope;
  PerformanceNoteId lastNote;
  bool initialized = false;

  // Volume target/sweep uses an 8-bit fractional accumulator exactly like the driver.
  bool volumeSweepActive = false;
  bool volumeSweepDown = false;
  u8 volumeTarget = 0;
  u8 volumeSweepSpeed = 0;
  u8 volumeSweepAccumulator = 0;

  bool panEnvelopeActive = false;
  bool percussionPanLocked = false;
  u8 panEnvelopeIndex = 0;
  u16 panPosition = 0;
  u8 panAccumulator = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& programState;

  [[nodiscard]] const DriverData& data() const { return *programState.data; }

  void beforeCommand() {
    if (track.initialized) {
      return;
    }
    track.initialized = true;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.program},
                   InstrumentEnvelopeMode::PreserveDynamicOverride);
    setAdsr(track.adsr);
    emitLevel();
    emitPan();
    out.tempo(math::tempoMicrosecondsPerQuarter(track.tempo));
    if (track.slur) {
      out.legatoPedal(true);
    }
  }

  [[nodiscard]] u16 pitchForSequenceNote(u8 note) const {
    const InstrumentInfo instrument = data().instrument(track.program);
    const u8 key = static_cast<u8>(note + instrument.transpose);
    return key > 0 && key < 0x79 ? data().pitch(instrument, key) : 0;
  }

  [[nodiscard]] u8 sequenceNote(u8 rawNote) const { return static_cast<u8>(rawNote + track.transpose); }

  [[nodiscard]] u16 basePitch(u8 rawNote) const { return pitchForSequenceNote(sequenceNote(rawNote)); }

  [[nodiscard]] double patchUnity() const { return data().unityKey(data().instrument(track.program)); }

  [[nodiscard]] double keyForPitch(u16 pitch) const {
    return pitch == 0 ? 0.0 : patchUnity() + 12.0 * std::log2(pitch / 4096.0);
  }

  [[nodiscard]] double keyForSequenceNote(u8 note) const { return keyForPitch(pitchForSequenceNote(note)); }

  void emitLevel() const {
    const u8 envelope = track.volumeEnvelope.index == 0 ? 0xff : track.volumeEnvelope.value;
    out.level(math::volumeGain(track.volume, envelope), ValueQuantization{.levels = 128});
  }

  void emitPan() const {
    const StereoBalance gains = math::panGains(static_cast<u8>(track.pan), track.stereoPhase);
    out.stereoBalance(gains.leftGain, gains.rightGain);
  }

  void emitPitch() const {
    const u16 pitch = basePitch(track.rawNote);
    out.pitchBend(math::pitchSemitones(pitch, track.tuning + static_cast<s8>(track.vibrato.value)),
                  kTuningAndVibratoLayer);
  }

  void setAdsr(u8 index) {
    track.adsr = index;
    track.gainEnvelope.reset(0);
    if (index < 0x80) {
      const u16 pair = data().adsr(index);
      out.replaceEnvelope(driverEnvelope(static_cast<u8>(pair), static_cast<u8>(pair >> 8)),
                          VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
      return;
    }
    if (index == 0x80) {
      out.replaceEnvelope(driverEnvelope(0, 0, 0x7f), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
      return;
    }
    track.gainEnvelope.reset(index & 0x7f);
    out.replaceEnvelope(driverEnvelope(0, 0, 0), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void program(u8 srcn) {
    track.program = srcn;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = srcn},
                   InstrumentEnvelopeMode::PreserveDynamicOverride);
  }

  void volume(u8 value) {
    track.volume = std::min<u8>(value, 31);
    track.volumeSweepActive = false;
    emitLevel();
  }

  void volumeAdd(s8 delta) { volume(static_cast<u8>(std::clamp<int>(track.volume + delta, 0, 31))); }

  void volumeEnvelope(u8 index) {
    track.volumeEnvelope.reset(index, 31);
    emitLevel();
  }

  void vibrato(u8 index) {
    track.vibrato.reset(index, 0);
    emitPitch();
  }

  void pan(s8 value) {
    track.panEnvelopeActive = false;
    track.pan = track.stereoEnabled ? value : 0;
    emitPan();
  }

  void panEnvelope(u8 index, s8 direct) {
    if (index == 0) {
      pan(direct);
      return;
    }
    if (!track.stereoEnabled) {
      track.panEnvelopeActive = false;
      track.pan = 0;
      emitPan();
      return;
    }
    const auto curve = data().curve(Curve::Pan, index);
    track.panEnvelopeIndex = index;
    track.panPosition = 0;
    track.panAccumulator = 0x80;
    track.panEnvelopeActive = !curve.empty();
    if (!curve.empty()) {
      track.pan = static_cast<s8>(curve[0]);
      emitPan();
    }
  }

  void tempo(u8 value) {
    track.tempo = value;
    out.tempo(math::tempoMicrosecondsPerQuarter(value));
  }

  void tuning(s16 value) {
    if (value == 0) {
      track.tuning = 0;
    } else if (track.early) {
      track.tuning = static_cast<s8>(static_cast<u8>(track.tuning + value));
    } else {
      track.tuning = static_cast<s16>(track.tuning + value);
    }
    emitPitch();
  }

  void flags(u8 value) {
    track.headerFlags = value;
    slur((value & 0x10) != 0);
  }
  void stereoPhase(u8 value) {
    if (!track.stereoEnabled) {
      return;
    }
    track.stereoPhase = value & 3;
    emitPan();
  }
  void portamento(u8 divisor) {
    track.portamentoDivisor = divisor;
    if (divisor != 0) {
      // The driver clears $0540+x here. The first following note therefore
      // keys on normally; only later notes glide from the preceding pitch.
      track.retriggerNextNote = true;
    }
    out.portamentoEnable(divisor != 0);
  }
  void slur(bool enabled) {
    track.slur = enabled;
    out.legatoPedal(enabled);
  }
  void toggleSlur() { slur(!track.slur); }

  [[nodiscard]] Effects note(u8 raw, u32 duration, u8 gate, bool updateDuration) {
    if (updateDuration) {
      track.duration = duration;
      track.gate = gate;
    }
    const u32 length = track.duration;
    if (raw == 0) {
      track.rawNote = 0;
      track.activeUntil = vm.tick();
      track.lastNote = {};
      track.currentPitch = 0;
      return Effects::wait(length);
    }
    track.rawNote = raw;
    track.volumeEnvelope.counter = 1;
    track.volumeEnvelope.position = -1;
    track.vibrato.counter = 1;
    track.vibrato.position = -1;
    track.gainEnvelope.counter = 1;
    track.gainEnvelope.position = -1;
    const bool startsFresh = std::exchange(track.retriggerNextNote, false);
    const u16 targetPitch = basePitch(raw);
    const double key = keyForPitch(targetPitch);
    if (key == 0.0) {
      track.currentPitch = 0;
      return Effects::wait(length);
    }
    const u32 sounding = math::gateTicks(length, track.gate);
    track.activeUntil = vm.tick() + sounding;
    const PerformanceNoteId previous = track.lastNote;
    const bool continuesVoice = track.slur && previous.valid() && !startsFresh;
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = 1.0,
        .durationTicks = sounding,
        .restartsEnvelope = !continuesVoice,
        .restartsLfoPhase = true,
    };
    if (continuesVoice) {
      static_cast<void>(out.setNoteEnd(previous, vm.tick()));
      track.lastNote = out.continueVoice(previous, event);
    } else {
      if (startsFresh && previous.valid()) {
        static_cast<void>(out.setNoteEnd(previous, vm.tick()));
      }
      track.lastNote = out.note(std::move(event));
    }
    if (track.portamentoDivisor != 0 && continuesVoice) {
      track.currentPitch = math::advancePortamento(track.currentPitch, targetPitch, track.portamentoDivisor);
      u16 endPitch = track.currentPitch;
      u32 frames = 0;
      while (endPitch != targetPitch) {
        endPitch = math::advancePortamento(endPitch, targetPitch, track.portamentoDivisor);
        ++frames;
      }
      const auto timing =
          PitchSlideTiming::fixedDuration((frames * math::ticks(track.tempo) + 0xffu) >> 8, frames * 16.0);
      out.pitchSlide(track.lastNote, keyForPitch(track.currentPitch), key, timing)
          .continueFrom(previous)
          .preferPortamento();
    } else {
      track.currentPitch = targetPitch;
    }
    // Notes are the only commands that advance sequence time. Preserve the
    // pitch just before the next carry frame, whose update follows its commands.
    for (u32 frame = 1; frame < math::driverFrames(length, track.tempo, track.tempoAccumulator); ++frame) {
      track.currentPitch = math::advancePortamento(track.currentPitch, targetPitch, track.portamentoDivisor);
    }
    if (track.pitchSweep != 0) {
      const int step = static_cast<u8>(track.pitchSweep) & 0x7f;
      const u32 frames = math::driverFrames(length, track.tempo, track.tempoAccumulator);
      const int targetNote = sequenceNote(raw) + (track.pitchSweep < 0 ? step : -step) * static_cast<int>(frames);
      if (targetNote > 0 && targetNote < 0x79) {
        const double targetKey = keyForSequenceNote(static_cast<u8>(targetNote));
        if (targetKey != 0.0) {
          auto slide = out.pitchSlide(track.lastNote, key, targetKey, length);
          slide.preferPitchBend();
          if (continuesVoice) {
            slide.continueFrom(previous);
          }
        }
      }
    }
    emitPitch();
    return Effects::wait(length);
  }

  [[nodiscard]] Effects percussion(u8 index, u8 raw, u32 duration, u8 gate, bool updateDuration) {
    if (index >= 30) {
      return note(raw, duration, gate, updateDuration);
    }
    const auto [first, second] = data().percussion(index);
    program(static_cast<u8>(first));
    setAdsr(static_cast<u8>(first >> 8));
    volumeEnvelope(static_cast<u8>(first >> 16));
    const u8 pitchControl = static_cast<u8>(first >> 24);
    const u8 percussionFlags = static_cast<u8>(second);
    if ((percussionFlags & 0x80) != 0) {
      track.pitchSweep = static_cast<s8>(pitchControl);
    } else {
      vibrato(pitchControl);
    }
    if (!track.percussionPanLocked) {
      pan(static_cast<s8>((second >> 8) & 0xff));
    }
    return note(raw == 0 ? static_cast<u8>(second >> 24) : raw, duration, gate, updateDuration);
  }

  void loopSet(u8 slot, u8 count) { track.loops[slot] = count; }
  [[nodiscard]] Effects loopEnd(u8 slot, Address destination) {
    --track.loops[slot];
    return track.loops[slot] != 0 ? vm.finiteBranch(destination) : Effects{};
  }
  [[nodiscard]] Effects loopBreak(u8 slot, Address destination) {
    --track.loops[slot];
    return track.loops[slot] == 0 ? vm.finiteBranch(destination) : Effects{};
  }
  [[nodiscard]] Effects conditionalBranch(u8 value, Address destination) {
    return value == track.branchId ? vm.finiteBranch(destination) : Effects{};
  }
  [[nodiscard]] Effects conditionalDo(u8 value, Address skip) {
    return value != track.branchId ? vm.finiteBranch(skip) : Effects{};
  }
  [[nodiscard]] Effects volumeTargetBranch(Address destination) {
    return track.volume != track.volumeTarget ? vm.finiteBranch(destination) : Effects{};
  }

  void volumeSweep(u8 targetAndDirection, u8 speed) {
    track.volumeTarget = targetAndDirection & 0x1f;
    track.volumeSweepDown = (targetAndDirection & 0x80) != 0;
    track.volumeSweepSpeed = speed;
    track.volumeSweepActive = speed != 0 && track.volume != track.volumeTarget;
  }

  void echoPreset(u8 index) {
    if (!programState.echoCapable) {
      return;
    }
    const auto preset = data().curve(Curve::Echo, index);
    if (preset.size() < 16) {
      return;
    }
    programState.echo.delayMilliseconds = (preset[1] & 0x0f) * 16.0;
    const StereoBalance pan = math::panGains(preset[3], preset[7]);
    programState.echo.leftGain = preset[2] / 127.0 * pan.leftGain;
    programState.echo.rightGain = preset[2] / 127.0 * pan.rightGain;
    programState.echo.feedback = static_cast<s8>(preset[4]) / 128.0;
    programState.echo.filterIndex = index;
    const u8 oldMask = programState.echo.voiceMask.value_or(0);
    programState.echo.voiceMask = static_cast<u8>((oldMask | preset[6]) & preset[5]);
    echoVoice(true);
  }

  void echoVoice(bool enabled) {
    if (!programState.echoCapable || track.channel >= 8) {
      return;
    }
    const u8 bit = static_cast<u8>(1u << track.channel);
    const u8 old = programState.echo.voiceMask.value_or(0);
    programState.echo.voiceMask = enabled ? static_cast<u8>(old | bit) : static_cast<u8>(old & ~bit);
    emitEcho();
  }

  void echoGlobal(u8 enabled) {
    programState.echoGloballyEnabled = enabled != 0;
    emitEcho();
  }

  void emitEcho() const {
    const double left = std::abs(programState.echo.leftGain.value_or(0));
    const double right = std::abs(programState.echo.rightGain.value_or(0));
    programState.echo.send = programState.echoGloballyEnabled ? std::max(left, right) : 0.0;
    out.reverb(programState.echo);
  }

  void advancePanFrame() {
    if (!track.panEnvelopeActive) {
      return;
    }
    const auto curve = data().curve(Curve::Pan, track.panEnvelopeIndex);
    const size_t p = track.panPosition;
    if (p + 7 >= curve.size()) {
      track.panEnvelopeActive = false;
      return;
    }
    const s8 target = static_cast<s8>(curve[p + 1]);
    const u16 sum = track.panAccumulator + static_cast<u8>(curve[p + 2]);
    track.panAccumulator = static_cast<u8>(sum);
    if (sum < 0x100) {
      return;
    }
    const s8 delta = static_cast<s8>(curve[p + 3]);
    const int next = track.pan + delta;
    const bool reached = delta >= 0 ? next >= target : next <= target;
    track.pan = reached ? target : static_cast<s8>(next);
    if (!reached) {
      return;
    }
    const u8 nextPan = static_cast<u8>(curve[p + 4]);
    if (nextPan != static_cast<u8>(curve[p + 5])) {
      track.panPosition = static_cast<u16>(p + 4);
      track.pan = static_cast<s8>(nextPan);
      return;
    }
    const u16 destination = static_cast<u16>(curve[p + 6] | (curve[p + 7] << 8));
    if (destination == 0 || destination < curve.address() ||
        static_cast<size_t>(destination - curve.address()) >= curve.size()) {
      track.panEnvelopeActive = false;
      return;
    }
    track.panPosition = static_cast<u16>(destination - curve.address());
    track.pan = static_cast<s8>(curve[track.panPosition]);
  }

  void tick() {
    u32 frames = 0;
    if (track.tempo == 0) {
      frames = 1;
    } else {
      do {
        ++frames;
        const u16 sum = track.tempoAccumulator + track.tempo;
        track.tempoAccumulator = static_cast<u8>(sum);
        if (sum >= 0x100) {
          break;
        }
      } while (frames < 256);
    }

    bool levelChanged = false;
    bool pitchChanged = false;
    bool panChanged = false;
    bool gainChanged = false;
    for (u32 frame = 0; frame < frames; ++frame) {
      const bool active = track.rawNote != 0 && vm.tick() < track.activeUntil;
      levelChanged |= track.volumeEnvelope.advance(data().curve(Curve::Volume, track.volumeEnvelope.index), active);
      pitchChanged |= track.vibrato.advance(data().curve(Curve::Vibrato, track.vibrato.index), active);
      gainChanged |= track.gainEnvelope.advance(data().curve(Curve::Gain, track.gainEnvelope.index), active);
      const s8 oldPan = track.pan;
      advancePanFrame();
      panChanged |= oldPan != track.pan;
      if (track.volumeSweepActive) {
        const u16 sum = track.volumeSweepAccumulator + track.volumeSweepSpeed;
        track.volumeSweepAccumulator = static_cast<u8>(sum);
        if (sum >= 0x100) {
          const int delta = track.volumeSweepDown ? -1 : 1;
          track.volume = static_cast<u8>(std::clamp<int>(track.volume + delta, 0, 31));
          levelChanged = true;
          track.volumeSweepActive = track.volume != track.volumeTarget;
        }
      }
    }
    if (levelChanged) {
      emitLevel();
    }
    if (pitchChanged && track.rawNote != 0) {
      emitPitch();
    }
    if (panChanged) {
      emitPan();
    }
    if (gainChanged) {
      out.replaceEnvelope(driverEnvelope(0, 0, track.gainEnvelope.value),
                          VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

struct DurationValue {
  u32 ticks = 1;
  u8 gate = 0;
  bool present = false;
};

[[nodiscard]] u32 commandSize(ByteReader reader, u32 begin, Version version) {
  if (!reader.has(begin, 1)) {
    return 1;
  }
  const u8 op = reader.u8At(begin);
  if (op <= 0x7f || (op >= 0xc0 && op <= 0xdd)) {
    u32 size = 1;
    if (op >= 0xc0 && reader.has(begin + 1, 1) && reader.u8At(begin + 1) < 0x80) {
      ++size;
    }
    if (reader.has(begin + size, 1) && reader.u8At(begin + size) >= 0xde) {
      const u8 suffix = reader.u8At(begin + size);
      size += suffix == 0xde ? 2 : suffix == 0xef ? 3 : suffix >= 0xf0 ? 2 : 1;
    }
    return size;
  }
  if (op >= 0xde) {
    return op == 0xde ? 2 : op == 0xef ? 3 : op >= 0xf0 ? 2 : 1;
  }
  switch (op) {
    case 0x80:
    case 0x9a:
      return 3;
    case 0x81:
    case 0x85:
    case 0xad:
      return 4;
    case 0x82:
    case 0x86:
    case 0x8c:
    case 0x99:
    case 0x9b:
    case 0xa1:
    case 0xa2:
    case 0xa6:
    case 0xa7:
    case 0xa9:
    case 0xaa:
    case 0xae:
    case 0xaf:
      return 1;
    case 0x8b:
    case 0x8d:
    case 0x93:
    case 0x95:
    case 0x9c:
    case 0x9e:
      return 3;
    case 0x97:
      return (version == Version::Aleste || version == Version::JakiCrush) ? 2 : 3;
    case 0xa3:
      return reader.has(begin + 1, 1) && reader.u8At(begin + 1) == 0 ? 3 : 2;
    case 0xa4:
      return version == Version::Aleste ? 1 : 2;
    case 0xa5:
      return version == Version::Aleste ? 1 : 4;
    case 0xa8:
    case 0xb0:
      return version == Version::Aleste || version == Version::SuperPuyo ? 2 : 1;
    case 0xb1:
      return 2;
    default:
      return 2;
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, const Layout& layout,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   std::set<u8>* programs = nullptr) {
  Cursor cursor(reader, begin, "compile-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  const auto parseDuration = [&](Cursor::Event& event, std::optional<u8> own = std::nullopt) {
    std::optional<u8> suffix = own;
    if (!suffix) {
      const auto next = event.peekU8();
      if (!next || *next < 0xde) {
        return DurationValue{};
      }
      suffix = event.u8("duration_command", SourceValueDisplay::Hex);
    }
    DurationValue value{.present = true};
    if (*suffix == 0xde || *suffix == 0xef) {
      value.ticks = math::ticks(event.u8("duration", SemanticOperandRole::Duration));
    } else {
      const u8 index = static_cast<u8>(*suffix < 0xf0 ? *suffix - 0xde : *suffix - 0xef);
      const u8 raw =
          reader.has(layout.durationTableAddress + index, 1) ? reader.u8At(layout.durationTableAddress + index) : 1;
      value.ticks = math::ticks(raw);
      event.derived("duration", value.ticks, SemanticOperandRole::Duration);
    }
    if (*suffix >= 0xef) {
      value.gate = event.u8("gate", SourceValueDisplay::Hex, SemanticOperandRole::Duration);
    }
    return value;
  };

  if (opcode <= 0x7f) {
    auto event =
        cursor.command(opcode == 0 ? "Rest" : "Note", opcode == 0 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    event.opcodeValue("note", opcode, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const DurationValue duration = parseDuration(event);
    return event.invoke<&Playback::note>(opcode, duration.ticks, duration.gate, duration.present);
  }
  if (opcode >= 0xc0 && opcode <= 0xdd) {
    auto event = cursor.command("Percussion Note", SequenceSemantic::Note);
    const u8 index = event.opcodeValue("percussion", static_cast<u8>(opcode - 0xc0));
    u8 raw = 0;
    const u32 row = layout.percussionTableAddress + index * 8u;
    if (reader.has(row + 7, 1) && reader.u8At(row + 7) == 0) {
      const auto next = event.peekU8();
      if (next && *next < 0x80) {
        raw = event.u8("note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      }
    }
    const DurationValue duration = parseDuration(event);
    if (programs != nullptr && reader.has(row, 8)) {
      programs->insert(reader.u8At(row));
    }
    return event.invoke<&Playback::percussion>(index, raw, duration.ticks, duration.gate, duration.present);
  }
  if (opcode >= 0xde) {
    auto event = cursor.command("Duration / Repeat Note", SequenceSemantic::Note);
    const DurationValue duration = parseDuration(event, opcode);
    return event.invoke([duration](Playback& playback) {
      return playback.note(playback.track.rawNote, duration.ticks, duration.gate, true);
    });
  }

  switch (opcode) {
    case 0x80: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.loopCandidate(event.addressLe("destination", SemanticOperandRole::JumpTarget));
    }
    case 0x81: {
      auto event = cursor.command("Loop End", SequenceSemantic::Repeat);
      const u8 slot = event.u8("slot");
      const Address destination = event.addressLe("destination", SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::loopEnd>(slot, destination).mayBranchTo(destination);
    }
    case 0x82:
    case 0x86:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0x83: {
      auto event = cursor.command("Vibrato Curve", SequenceSemantic::Modulation);
      const u8 index = event.u8("curve");
      return event.invoke<&Playback::vibrato>(index);
    }
    case 0x84: {
      auto event = cursor.command("Portamento Rate", SequenceSemantic::Portamento);
      return event.invoke<&Playback::portamento>(event.u8("divisor", SemanticOperandRole::State));
    }
    case 0x85: {
      auto event = cursor.sourceOnly("Main CPU Command", "cpu-command");
      static_cast<void>(event.rawBytes("arguments", 3));
      return event;
    }
    case 0x87: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0x88: {
      auto event = cursor.command("Volume Envelope / Tremolo", SequenceSemantic::Modulation);
      const u8 index = event.u8("curve");
      return event.invoke<&Playback::volumeEnvelope>(index);
    }
    case 0x89: {
      auto event = cursor.command("Transpose Add", SequenceSemantic::Pitch);
      return event.add<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0x8a: {
      auto event = cursor.command("Volume Add", SequenceSemantic::Level);
      return event.invoke<&Playback::volumeAdd>(event.s8("delta", SemanticOperandRole::Level));
    }
    case 0x8b: {
      auto event = cursor.sourceOnly("Voice DSP Control", "voice-dsp-control");
      static_cast<void>(event.rawBytes("arguments", 2));
      return event;
    }
    case 0x8c:
      return cursor.sourceOnly("NOP", "nop");
    case 0x8d: {
      auto event = cursor.command("Loop Count", SequenceSemantic::Repeat);
      const u8 slot = event.u8("slot");
      return event.invoke<&Playback::loopSet>(slot, event.u8("count", SemanticOperandRole::Count));
    }
    case 0x8e:
    case 0x8f: {
      auto event = cursor.sourceOnly(opcode == 0x8e ? "Noise Clock Envelope" : "Noise Clock Add", "noise");
      static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
      return event;
    }
    case 0x90: {
      auto event = cursor.command("Track Flags", SequenceSemantic::State);
      return event.invoke<&Playback::flags>(event.u8("flags", SourceValueDisplay::Hex));
    }
    case 0x91: {
      auto event = cursor.sourceOnly("CPU Flags", "cpu-flags");
      static_cast<void>(event.u8("flags", SourceValueDisplay::Hex));
      return event;
    }
    case 0x92: {
      auto event = cursor.command("Stereo Phase", SequenceSemantic::Pan);
      return event.invoke<&Playback::stereoPhase>(event.u8("phase", SourceValueDisplay::Hex));
    }
    case 0x93: {
      auto event = cursor.command("Volume Sweep", SequenceSemantic::Level);
      const u8 target = event.u8("target_and_direction", SourceValueDisplay::Hex, SemanticOperandRole::Level);
      return event.invoke<&Playback::volumeSweep>(target, event.u8("speed"));
    }
    case 0x94: {
      auto event = cursor.command("Pitch Sweep", SequenceSemantic::Pitch);
      return event.set<&TrackState::pitchSweep>(event.s8("rate_and_direction", SemanticOperandRole::Pitch));
    }
    case 0x95: {
      auto event = cursor.command("Jump Until Volume Target", SequenceSemantic::Jump);
      const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
      return event.invoke<&Playback::volumeTargetBranch>(destination).mayBranchTo(destination);
    }
    case 0x96: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempo>(event.u8("tempo"));
    }
    case 0x97: {
      auto event = cursor.command("Tuning Add", SequenceSemantic::Pitch);
      const s16 value = layout.early()
                            ? event.s8("delta", SemanticOperandRole::Pitch)
                            : event.s16le("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      return event.invoke<&Playback::tuning>(value);
    }
    case 0x98: {
      auto event = cursor.command("Branch ID", SequenceSemantic::State);
      return event.set<&TrackState::branchId>(event.u8("id"));
    }
    case 0x99:
      return cursor.command("Toggle Slur", SequenceSemantic::State).invoke<&Playback::toggleSlur>();
    case 0x9a: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      return event.call(event.addressLe("destination", SemanticOperandRole::JumpTarget));
    }
    case 0x9b:
      return cursor.command("Return", SequenceSemantic::Return).return_();
    case 0x9c: {
      auto event = cursor.sourceOnly("Table Transpose", "table-transpose");
      static_cast<void>(event.rawBytes("arguments", 2));
      return event;
    }
    case 0x9d: {
      auto event = cursor.command("Gate", SequenceSemantic::State);
      return event.set<&TrackState::gate>(event.u8("gate", SourceValueDisplay::Hex, SemanticOperandRole::Duration));
    }
    case 0x9e: {
      auto event = cursor.command("Conditional Jump", SequenceSemantic::Jump);
      const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
      return event.finiteBranch(destination);
    }
    case 0x9f: {
      auto event = cursor.command("ADSR / Dynamic GAIN", SequenceSemantic::Envelope);
      const u8 index = event.u8("pattern");
      return event.invoke<&Playback::setAdsr>(index);
    }
    case 0xa0: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      const u8 srcn = event.u8("srcn", SemanticOperandRole::InstrumentProgram);
      if (programs != nullptr) {
        programs->insert(srcn);
      }
      return event.invoke<&Playback::program>(srcn);
    }
    case 0xa1:
      return cursor.command("Slur On", SequenceSemantic::State).invoke<&Playback::slur>(true);
    case 0xa2:
      return cursor.command("Slur Off", SequenceSemantic::State).invoke<&Playback::slur>(false);
    case 0xa3: {
      auto event = cursor.command("Pan Envelope", SequenceSemantic::Pan);
      const u8 index = event.u8("curve");
      const s8 direct = index == 0 ? event.s8("pan", SemanticOperandRole::Pan) : 0;
      return event.invoke<&Playback::panEnvelope>(index, direct);
    }
    case 0xa4:
      if (layout.version == Version::Aleste) {
        return cursor.sourceOnly("Internal Flag", "internal-flag");
      } else {
        auto event = cursor.command("Conditional Do", SequenceSemantic::Jump);
        const u8 branch = event.u8("branch_id");
        const u32 next = begin + 2;
        const Address skip{next + commandSize(reader, next, layout.version)};
        event.derived("skip_destination", skip, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
        return event.invoke<&Playback::conditionalDo>(branch, skip).mayBranchTo(skip);
      }
    case 0xa5:
      if (layout.version == Version::Aleste) {
        return cursor.sourceOnly("Internal Flag", "internal-flag");
      } else {
        auto event = cursor.command("Branch ID Jump", SequenceSemantic::Jump);
        const u8 branch = event.u8("branch_id");
        const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
        return event.invoke<&Playback::conditionalBranch>(branch, destination).mayBranchTo(destination);
      }
    case 0xa6:
    case 0xa7:
      return cursor.sourceOnly(opcode == 0xa6 ? "Pitch Modulation On" : "Pitch Modulation Off", "pitch-modulation");
    case 0xa8:
      if (layout.hasEchoCommands()) {
        auto event = cursor.command("Echo Preset", SequenceSemantic::State);
        const u8 index = event.u8("preset");
        return event.invoke<&Playback::echoPreset>(index);
      }
      return cursor.sourceOnly("NOP", "nop");
    case 0xa9:
      return layout.hasEchoCommands()
                 ? cursor.command("Echo On", SequenceSemantic::State).invoke<&Playback::echoVoice>(true)
                 : cursor.sourceOnly("NOP", "nop");
    case 0xaa:
      return layout.hasEchoCommands()
                 ? cursor.command("Echo Off", SequenceSemantic::State).invoke<&Playback::echoVoice>(false)
                 : cursor.sourceOnly("NOP", "nop");
    case 0xab: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.s8("pan", SemanticOperandRole::Pan));
    }
    case 0xac: {
      auto event = cursor.sourceOnly("Noise Clock", "noise");
      static_cast<void>(event.u8("clock", SourceValueDisplay::Hex));
      return event;
    }
    case 0xad: {
      auto event = cursor.command("Loop Break", SequenceSemantic::RepeatBreak);
      const u8 slot = event.u8("slot");
      const Address destination = event.addressLe("destination", SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::loopBreak>(slot, destination).mayBranchTo(destination);
    }
    case 0xae:
    case 0xaf:
      return cursor.command(opcode == 0xae ? "Percussion Pan Unlock" : "Percussion Pan Lock", SequenceSemantic::Pan)
          .set<&TrackState::percussionPanLocked>(opcode == 0xaf);
    case 0xb0:
      if (layout.hasEchoCommands()) {
        auto event = cursor.command("Global Echo", SequenceSemantic::State);
        return event.invoke<&Playback::echoGlobal>(event.u8("enabled"));
      }
      return cursor.sourceOnly("NOP", "nop");
    case 0xb1: {
      auto event = cursor.sourceOnly("CPU / SFX Control", "cpu-sfx-control");
      static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
      return event;
    }
    default:
      return cursor.unsupported("Invalid Command").stop();
  }
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config = SequenceProgramConfig{
      .commandKindPrefix = "compile-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kCommandLimit,
              .initialLevel = 1.0,
              .initialReverbSend = 0.0,
              .initialMonoModeChannels = 0,
              .initialPitchBendRangeSemitones = 12,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(0x80),
          },
  };
  return config;
}

TrackProgram decodeSourceTrack(ByteReader reader, const Layout& layout, u32 trackNumber, u32 startAddress,
                               std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = kCommandLimit};
  return tracks.decode(trackNumber, startAddress,
                       [&](u32 offset) { return decodeCommand(reader, offset, layout, diagnostics); });
}

SequenceParse decodeSequence(RetainedSource source, const Layout& layout, AssetId sequenceId,
                             SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const ByteReader reader = source.reader();
  const u8 count = reader.u8At(layout.songHeaderAddress);
  const SourceRange header = reader.range(layout.songHeaderAddress, 1 + count * 14u);
  std::set<u8> programs;
  SequenceDecodeSession sequence{reader, sequenceConfig(), sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  for (u32 track = 0; track < count; ++track) {
    const u32 item = layout.songHeaderAddress + 1 + track * 14u;
    const u16 start = reader.le16(item + 8);
    programs.insert(reader.u8At(item + 10));
    sequence.addTrack(
        track, reader.range(item, 14), start,
        [&](u32 offset) { return decodeCommand(reader, offset, layout, diagnostics, &programs); }, start);
  }

  SequenceProgram program =
      sequence.finish(makeCompiledRuntime<Cursor, ProgramState>(DriverData{std::move(source), layout}));
  return SequenceParse{.program = std::move(program), .programs = std::move(programs), .headerRange = header};
}

}  // namespace vgmtrans::formats::compile_snes
