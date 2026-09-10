/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnesQuest.h"

#include "value/formats/NinSnes/NinSnesPatterns.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

// Quest's N-SPC variant used by Tactics Ogre: Let Us Cling Together.
//
// This driver shares N-SPC's section playlists and six-byte instruments, but
// changes note gates, command layouts, and timing enough to need its own player.
// The playlist parser, VM, instrument recipes, and sound-bank builder are shared.
//
// Track bytes (hex): 01-7D set length; 7E/7F mean 144/192 ticks; DE sets a
// 16-bit length. An optional nonzero byte below 80 packs a three-bit gate index
// above a four-bit velocity index.
// 80-C7 are notes, C8 a tie, C9 a rest, CA-D7 percussion, and D8-FF commands.
// 00 ends a track or returns/repeats a pattern. Variable command operands are
// documented in decodeCommand(); embedded zero bytes remain part of the command.

namespace vgmtrans::formats::nin_snes::quest {

using namespace core;

namespace {

// Recognize Tactics Ogre's command dispatch and BGM loader. Absolute operands
// remain relocatable; direct-page operands identify this driver revision.
const Pattern kDispatch("\x68\xc8\x90\xb6\x68\xd8\xb0\x06\x68\xca\xb0\xc5\x2f\xda"
                        "\x1c\x80\xa8\xb0\x5d\xe8\x0b\x2d\xe8\x0e\x2d\x1f\x88\x0b",
                        "xxx?xxxxxxx?x?xxxxxx?xx?xx??", 28);
const Pattern kSongList("\xe4\xbc\x9c\x1c\xfd\x8f\x81\x5d\xe5\xfa\x05\xe9\xfb\x05"
                        "\xc4\x0c\xd8\x0d\xf7\x0c\xc4\x40\xfc\xf7\x0c\xc4\x41",
                        "xxxxxxxxx??x??xxxxxxxxxxxxx", 27);
const Pattern kInstrument("\x08\x80\x8d\x06\xcf\xda\x0c\x8d\x00\xf4\x28\x28\xd7", "xxxxxxxxxxxxx", 13);
const Pattern kGate("\xf5\xfb\x20\x68\x07\xf0\x2b\x60\x84\x58\xfd\xf6\x7b\x21", "x??xxx?xxxxx??", 14);
const Pattern kVelocity("\x28\x0f\x04\x59\xfd\x80\xb6\x8b\x21\x48\xff\xfd\xcf\xdd", "xxxxxxx??xxxxx", 14);
const Pattern kPan("\xfd\xf6\xe0\x1e\xc4\x05\xf6\xcb\x1e\xeb\x04\xcf\xdb\x93", "xx??xxx??xxxxx", 14);
const Pattern kDspInit("\xf5\xd8\x19\x30\x0b\xc4\xf2\xf5\xd9\x19\xc4\xf3\x3d\x3d\x2f\xf0", "x??xxxxx??xxxxxx", 16);

[[nodiscard]] std::vector<u8> table(ByteReader reader, u32 address, u32 count) {
  if (!reader.has(address, count)) {
    return {};
  }
  const auto bytes = reader.slice(address, count);
  return {bytes.begin(), bytes.end()};
}

// E7 divides 0x1036 by the tempo operand and writes the quotient to timer 1.
// One BGM tick lasts 125 us per timer unit; a zero timer target means 256.
// The 48-PPQN export timebase does not make the operand itself the exported BPM.
[[nodiscard]] u8 tempoDivisor(u8 tempo, bool clampOverflow = true) {
  if (clampOverflow && tempo < 0x11) {
    return 0xff;
  }
  // E8 omits E7's overflow clamp, including SPC700 DIV's alternate result
  // when the dividend high byte is at least twice the divisor.
  return tempo >= 9 ? static_cast<u8>(0x1036 / tempo) : static_cast<u8>(255 - (0x1036 - tempo * 512) / (256 - tempo));
}

[[nodiscard]] u32 tempoUs(u8 divisor) {
  return kPpqn * 125u * (divisor == 0 ? 256u : divisor);
}

// The 128 instrument rows each hold SRCN, ADSR1, ADSR2, GAIN, then a
// big-endian pitch multiplier.
using InstrumentBytes = std::array<u8, 6>;

struct Config {
  std::vector<u8> duration;
  std::vector<u8> pan;
  std::array<InstrumentBytes, 128> instruments{};
  u8 condition = 0;
};

struct CallFrame {
  Address start;
  bool infinite = false;
};

struct TrackState {
  explicit TrackState(const TrackProgram& track) : number(track.sourceTrackNumber) {}
  void beginSection() {
    calls.clear();
    percussionNote = 0;
    legato = false;
  }

  u32 number = 0;
  u16 length = 1;
  u8 gate = 0;
  u8 velocity = 0;
  u8 logicalProgram = 0;
  u32 activeProgram = 0;
  u8 percussionNote = 0;
  InstrumentBytes instrument{};
  s8 transpose = 0;
  u8 tuning = 0;
  bool legato = false;
  bool initialized = false;
  std::optional<double> lastKey;
  PerformanceNoteId lastNote;
  std::vector<CallFrame> calls;
  Address loopStart;
  SequenceFixedPointAutomation<s32> volume;
  u8 vibratoDelay = 0;
  u8 vibratoRate = 0;
  u8 vibratoDepth = 0;
  u8 vibratoGrowthTicks = 0;
  u8 vibratoGrowth = 0;
  u32 vibratoElapsedUs = 0;
  u32 vibratoGrowthApplied = 0;
  u8 envelopeDelay = 0;
  u8 envelopeLength = 0;
  s16 envelopeDelta = 0;
  bool pitchEnvelope = false;
  u8 releaseDelay = 0;
  u8 releaseAdsr1 = 0;
  u8 releaseAdsr2 = 0;
  u8 releaseVolume = 0;
  bool retainVoice = false;
  std::optional<u64> releaseTick;
  u64 noteEndTick = 0;
  bool releaseApplied = false;
  bool releaseGainActive = false;
  double releaseGainElapsed = 0;
  // DSP pitch slides interpolate the pitch register, not semitone numbers.
  double pitchScale = 1.0;
  double pitchOrigin = 1.0;
  SequenceFixedPointAutomation<s32> pitch;
  u32 pitchDelay = 0;
  u32 pitchRemaining = 0;
  s16 pitchDelta = 0;
  bool pitchIsDelta = false;
};

struct ProgramState {
  explicit ProgramState(const Config& config) : config(config), instruments(config.instruments) {
    for (u32 i = 0; i < programs.size(); ++i) {
      programs[i] = i;
    }
    master.reset(0);
    tempo.reset(0x80);
  }

  // RAM rows are mutable, but exported notes must keep the instrument they
  // selected. Versions give each distinct definition an immutable recipe;
  // repeated writes reuse it. Explicit noise is independent of the SRCN byte.
  [[nodiscard]] u32 instrumentVersion(u8 logical, const InstrumentBytes& bytes, SourceRange source,
                                      bool noise = false) {
    const auto key = std::tuple{logical, bytes, noise};
    if (const auto it = versions.find(key); it != versions.end()) {
      return it->second;
    }
    const u32 id = 0x80 + static_cast<u32>(versions.size());
    versions.emplace(key, id);
    recipes.overrides.push_back(InstrumentOverride{.program = id,
                                                   .tuningProgram = logical,
                                                   .srcn = bytes[0],
                                                   .adsr1 = bytes[1],
                                                   .adsr2 = bytes[2],
                                                   .gain = bytes[3],
                                                   .pitchHigh = bytes[4],
                                                   .pitchLow = bytes[5],
                                                   .source = source,
                                                   .noise = noise});
    return id;
  }

  Config config;
  std::array<InstrumentBytes, 128> instruments;
  std::array<u32, 128> programs{};
  std::map<std::tuple<u8, InstrumentBytes, bool>, u32> versions;
  SequenceRecipes recipes;
  s8 transpose = 0;
  u8 percussionBase = 0;
  SequenceFixedPointAutomation<s32> master;
  SequenceFixedPointAutomation<s32> tempo;
  std::optional<u64> lastTick;
  ReverbPerformanceEvent reverb{.voiceMask = 0};
};

struct TieSegment {
  std::optional<u16> length;
  std::optional<u8> gate;
};

struct Slide {
  u8 delay = 0;
  u8 length = 0;
  u8 note = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void beforeCommand() {
    if (!track.initialized) {
      track.initialized = true;
      track.volume.reset(0);
      out.level(0);
      pan(10);
    }
  }

  void parameters(u16 length, std::optional<u8> packed, u8 velocity) {
    track.length = length;
    if (packed) {
      track.gate = (*packed >> 4) & 7;
      track.velocity = velocity;
    }
  }

  // Gating applies to the combined note/tie length. Gate 7 subtracts one tick.
  // Other gates use floor(floor(length / divisor) * rate / 256) * divisor,
  // with divisor = highByte + 1. Preserve both truncations for long notes.
  [[nodiscard]] u32 duration(u16 length) const {
    const u32 fullLength = length == 0 ? 0x10000u : length;
    if (track.legato) {
      return fullLength;
    }
    if (track.gate == 7) {
      return static_cast<u16>(length - 1);
    }
    const u8 rate = program.config.duration[track.gate];
    const u8 divisor = static_cast<u8>((length >> 8) + 1);
    if (divisor == 0) {
      return fullLength;
    }
    const u32 gate = ((length / divisor * rate) >> 8) * divisor;
    // A zero gate counter underflows instead of keying off at tick zero.
    return gate == 0 ? fullLength : gate;
  }

  void loadProgram(u8 logical) {
    logical &= 0x7f;
    track.logicalProgram = logical;
    track.instrument = program.instruments[logical];
    track.activeProgram = program.programs[logical];
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.activeProgram});
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void instrument(u8 encoded) {
    track.percussionNote = 0xff;
    loadProgram(encoded & 0x7f);
  }

  void noise(u8 clock, SourceRange source) {
    track.percussionNote = 0xff;
    inlineInstrument({static_cast<u8>(clock & 0x1f), 0x8f, 0xe0, 0, 1, 0}, source, true);
  }

  // Inline edits affect this channel immediately. Table writes below affect
  // the shared row and take effect only when a channel next loads that program.
  void inlineInstrument(InstrumentBytes bytes, SourceRange source, bool noise = false) {
    track.instrument = bytes;
    track.activeProgram = program.instrumentVersion(track.logicalProgram, bytes, source, noise);
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.activeProgram});
  }

  void writeInstrument(u8 logical, InstrumentBytes bytes, SourceRange source) {
    logical &= 0x7f;
    program.instruments[logical] = bytes;
    program.programs[logical] = program.instrumentVersion(logical, bytes, source);
  }

  void envelope(u8 adsr1, u8 adsr2, u8 gain) {
    track.instrument[1] = adsr1;
    track.instrument[2] = adsr2;
    track.instrument[3] = gain;
    out.replaceEnvelope(snesDspEnvelope(adsr1, adsr2, gain), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void partialInstrument(std::optional<u8> srcn, u8 adsr1, u8 adsr2, u8 gain, SourceRange source) {
    if (srcn) {
      auto bytes = track.instrument;
      bytes[0] = *srcn;
      bytes[1] = adsr1;
      bytes[2] = adsr2;
      bytes[3] = gain;
      inlineInstrument(bytes, source);
    } else {
      envelope(adsr1, adsr2, gain);
    }
  }

  void instrumentTuning(s16 delta, SourceRange source) {
    auto bytes = track.instrument;
    const u16 pitch = static_cast<u16>((bytes[4] << 8 | bytes[5]) + delta);
    bytes[4] = pitch >> 8;
    bytes[5] = pitch;
    inlineInstrument(bytes, source);
  }

  [[nodiscard]] double pitchRegister(u8 note) const {
    constexpr std::array<u16, 12> pitches{0x085f, 0x08de, 0x0965, 0x09f4, 0x0a8c, 0x0b2c,
                                          0x0bd6, 0x0c8b, 0x0d4a, 0x0e14, 0x0eea, 0x0fcd};
    constexpr std::array<u8, 12> steps{0x7f, 0x87, 0x8f, 0x98, 0xa0, 0xaa, 0xb5, 0xbf, 0xca, 0xd6, 0xe3, 0xf1};
    const u32 pitch = pitches[note % 12] + ((steps[note % 12] * track.tuning) >> 8);
    const u16 scale = (track.instrument[4] << 8) | track.instrument[5];
    const u32 tuned = ((pitch * scale) >> 8) & ~0x0fu;
    return std::max(1.0, std::ldexp(static_cast<double>(tuned), static_cast<int>(note / 12) - 5));
  }

  void startSlide(Slide slide) {
    const u8 target = static_cast<u8>((slide.note & 0x7f) + track.transpose);
    track.pitchDelay = slide.delay;
    track.pitchRemaining = slide.length;
    track.pitchIsDelta = false;
    track.pitch.reset(static_cast<s32>(track.pitchScale));
    if (slide.length != 0) {
      static_cast<void>(track.pitch.begin(
          SequenceFixedPointMotion<s32>::toRawTarget(static_cast<s32>(pitchRegister(target)), slide.length)));
    }
    vibratoOff();
  }

  void pitchEnvelope(u8 delay, u8 length, s16 delta) {
    track.pitchEnvelope = true;
    track.envelopeDelay = delay;
    track.envelopeLength = length;
    track.envelopeDelta = delta;
    vibratoOff();
  }

  void pitchOff() {
    track.pitchEnvelope = false;
    track.pitchRemaining = 0;
  }

  void vibrato(u8 delay, u8 rate, u8 depth) {
    track.vibratoDelay = delay;
    track.vibratoRate = rate;
    track.vibratoDepth = depth;
    track.vibratoGrowthTicks = 0;
    track.vibratoGrowth = 0;
    track.pitchEnvelope = false;
    emitVibrato();
  }

  void vibratoOff() {
    track.vibratoDepth = 0;
    out.vibratoDepth(0);
  }

  void emitVibrato() {
    // Quest's sine LFO runs on the independent 10 ms timer-0 clock. Its
    // phase increment is rate*64 in a 16-bit cycle; depth scales DSP pitch.
    const LfoPerformanceContext context{.shape = LfoShape{.waveform = LfoWaveform::Sine}};
    out.vibratoRate(track.vibratoRate * (100.0 / 1024.0), context);
    out.vibratoDelayPhysical(0, track.vibratoDelay * 10.0);
    out.vibratoDepth(12.0 * std::log2(1.0 + track.vibratoDepth / 256.0), context);
  }

  void growVibrato(u8 ticks, u8 depth) {
    track.vibratoGrowthTicks = ticks;
    track.vibratoGrowth = depth;
  }

  [[nodiscard]] Effects note(u8 opcode, const std::vector<TieSegment>& ties, std::optional<Slide> slide) {
    // Lookahead carries the last tie's length and gate into subsequent notes,
    // while leaving velocity alone. The combined wait wraps as a 16-bit value.
    u16 total = track.length;
    for (const auto& tie : ties) {
      track.length = tie.length.value_or(track.length);
      total = static_cast<u16>(total + track.length);
      if (tie.gate) {
        track.gate = *tie.gate;
      }
    }
    const u32 wait = total == 0 ? 0x10000u : total;
    if (opcode == 0xc9 || opcode == 0xc8) {
      return Effects::wait(wait);
    }
    const bool drum = opcode >= 0xca;
    // Repeated percussion retains its active instrument, even after a table
    // write. E0 and section changes invalidate this cache. A following melodic
    // note also keeps the percussion instrument until another program is loaded.
    if (drum && track.percussionNote != opcode) {
      track.percussionNote = opcode;
      loadProgram(static_cast<u8>(opcode - 0xca + program.percussionBase));
    }
    // Melodic notes carry overflow from global transpose into the channel
    // addition. Percussion clears carry and applies only channel transpose.
    const u16 globalSum = (opcode & 0x7f) + static_cast<u8>(program.transpose);
    const u8 raw = static_cast<u8>((drum ? 0x24 : globalSum + (globalSum > 0xff)) + static_cast<u8>(track.transpose));
    const double key = 24.0 + raw;
    program.recipes.usedNotes.emplace(track.activeProgram, static_cast<u8>(std::min(24u + raw, 127u)));
    if (track.releaseApplied) {
      out.replaceEnvelope(snesDspEnvelope(track.instrument[1], track.instrument[2], track.instrument[3]),
                          VoiceEnvelopeScope::FutureAttacks);
      out.expression(1.0);
      track.releaseApplied = false;
    }
    track.releaseGainActive = false;
    track.pitchScale = pitchRegister(raw);
    track.pitchOrigin = track.pitchScale;
    track.pitchRemaining = 0;
    out.pitchBend(0);
    out.tuning(track.tuning * (100.0 / 256.0));
    emitVibrato();
    track.vibratoElapsedUs = 0;
    track.vibratoGrowthApplied = 0;
    const bool held = track.legato && track.lastNote.valid();
    const u32 gate = duration(total);
    // F6 FF moves the gate counter to the release-envelope timer and disables
    // ordinary key-off. A retained voice does the same for an explicit delay.
    u32 sounding = track.releaseDelay == 0xff || (track.releaseDelay != 0 && track.retainVoice) ? wait : gate;
    const u32 releaseDelay = track.releaseDelay == 0xff ? gate : track.releaseDelay;
    const bool fallingGain = track.releaseDelay != 0 && track.releaseAdsr1 == 0 && (track.releaseAdsr2 & 0xc0) == 0x80;
    if (fallingGain) {
      // Switching to decreasing GAIN is a note release. Encode that release
      // before the attack so MIDI/SF2 can select a suitable envelope variant.
      out.updateEnvelope(Envelope{.releaseSeconds = snesDspGainEnvelopeSeconds(track.releaseAdsr2, 0x7ff, 0)},
                         EnvelopeFields::Release);
      track.releaseApplied = true;
      sounding = std::min(sounding, releaseDelay);
    }
    if (track.releaseDelay != 0 && track.releaseAdsr1 != 0) {
      sounding = std::min(sounding, releaseDelay > 1 ? releaseDelay - 1 : 1u);
    }
    const NotePerformanceEvent event{.key = key,
                                     .linearVelocity = track.velocity / 255.0,
                                     .durationTicks = std::max(1u, sounding),
                                     .restartsLfoPhase = !held};
    track.lastNote = held ? out.continueVoice(track.lastNote, event) : out.note(event);
    track.lastKey = key;
    if (slide) {
      startSlide(*slide);
    } else if (track.pitchEnvelope) {
      track.pitchDelay = track.envelopeDelay;
      track.pitchRemaining = track.envelopeLength;
      track.pitchDelta = track.envelopeDelta;
      track.pitchIsDelta = true;
    }
    track.noteEndTick = vm.tick() + wait;
    track.releaseTick.reset();
    if (track.releaseDelay != 0 && !fallingGain && releaseDelay < wait) {
      track.releaseTick = vm.tick() + releaseDelay;
    }
    return Effects::wait(wait);
  }

  void legato(bool enabled) {
    track.legato = enabled;
    out.legatoPedal(enabled);
    if (!enabled && track.lastNote.valid()) {
      static_cast<void>(out.setNoteEnd(track.lastNote, vm.tick()));
      track.lastNote = {};
    }
  }

  // EF patterns nest four deep. Repeat slot 0 belongs to the independent
  // EB/EC loop; slots 1-4 follow the pattern stack. The VM owns return addresses.
  [[nodiscard]] Effects call(u8 count, Address destination) {
    if (count == 0xff) {
      return vm.jump(destination);
    }
    if (track.calls.size() == 4) {
      vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = "Quest subroutine stack overflow"});
      return vm.end();
    }
    // 00 and 80-FE repeat forever; FF is a jump without a return frame.
    const bool infinite = count == 0 || count >= 0x80;
    if (!infinite) {
      vm.repeatCounter(static_cast<u8>(track.calls.size() + 1)).start(count);
    }
    track.calls.push_back({destination, infinite});
    return vm.call(destination);
  }

  [[nodiscard]] Effects end() {
    if (track.calls.empty()) {
      return vm.endSection();
    }
    auto& frame = track.calls.back();
    if (frame.infinite) {
      return vm.declaredLoop(frame.start);
    }
    auto counter = vm.repeatCounter(static_cast<u8>(track.calls.size()));
    if (counter.consumeReplay()) {
      return vm.finiteBranch(frame.start);
    }
    counter.finish();
    track.calls.pop_back();
    return vm.return_();
  }

  void loopStart(u8 count, Address start) {
    track.loopStart = start;
    vm.repeatCounter(0).start(count == 0 || (count >= 0x80 && count != 0xff) ? 256 : count);
  }

  [[nodiscard]] Effects loopEnd() {
    auto counter = vm.repeatCounter(0);
    if (counter.consumeReplay()) {
      return vm.finiteBranch(track.loopStart);
    }
    counter.finish();
    return {};
  }

  [[nodiscard]] Effects conditionalJump(Address destination) {
    return program.config.condition == 0 ? vm.jump(destination) : Effects{};
  }

  // E1 selects one of 21 positions from each pan table. Bits 7/6 invert the
  // left/right phase, respectively; the gains also determine channel loudness.
  void pan(u8 value) {
    const u8 index = std::min<u8>(value & 0x3f, 20);
    const auto& panTable = program.config.pan;
    out.stereoBalance((panTable[index] / 128.0) * ((value & 0x80) ? -1 : 1),
                      (panTable[21 + index] / 128.0) * ((value & 0x40) ? -1 : 1));
  }

  void volume(u8 value) {
    track.volume.reset(value);
    out.level(value / 255.0);
  }

  void volumeFade(u8 length, u8 target) {
    if (length != 0) {
      static_cast<void>(track.volume.begin(SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
    }
  }

  void master(u8 value) {
    program.master.reset(value);
    out.masterLevel(value / 255.0);
  }

  void masterFade(u8 length, u8 target) {
    if (length != 0) {
      static_cast<void>(program.master.begin(SequenceFixedPointMotion<s32>::toRawTarget(target, length)));
    }
  }

  void tempo(u8 value) {
    program.tempo.reset(tempoDivisor(value));
    out.tempo(tempoUs(tempoDivisor(value)));
  }

  void tempoFade(u8 length, u8 target) {
    // E8 interpolates timer divisors, so interpolating BPM would change the fade.
    if (length != 0) {
      static_cast<void>(
          program.tempo.begin(SequenceFixedPointMotion<s32>::toRawTarget(tempoDivisor(target, false), length)));
    }
  }

  void transpose(s8 semitones) { program.transpose = semitones; }
  void percussionBase(u8 base) { program.percussionBase = base; }

  // F8/FC edit the shared DSP voice mask. F5 changes signed echo output levels
  // without changing that mask; instrument changes preserve both.
  void channelEcho(bool enabled) {
    const u8 bit = static_cast<u8>(1u << track.number);
    const u8 mask = program.reverb.voiceMask.value_or(0);
    program.reverb.voiceMask = enabled ? mask | bit : mask & ~bit;
    out.reverb(program.reverb);
  }

  void echo(u8 volume, u8 leftPhase, u8 rightPhase) {
    program.reverb.leftGain = static_cast<s8>(volume ^ leftPhase) / 128.0;
    program.reverb.rightGain = static_cast<s8>(volume ^ rightPhase) / 128.0;
    program.reverb.send = std::max(std::abs(*program.reverb.leftGain), std::abs(*program.reverb.rightGain));
    out.reverb(program.reverb);
  }

  void echoParameters(u8 delay, s8 feedback, u8 filter) {
    program.reverb.delayMilliseconds = std::min<u8>(delay, 8) * 16.0;
    program.reverb.feedback = feedback / 128.0;
    program.reverb.filterIndex = filter;
    out.reverb(program.reverb);
  }

  void echoFeedback(s8 feedback) {
    program.reverb.feedback = feedback / 128.0;
    out.reverb(program.reverb);
  }

  void release(u8 delay, u8 adsr1, u8 adsr2, u8 volume) {
    track.releaseDelay = delay;
    track.releaseAdsr1 = adsr1;
    track.releaseAdsr2 = adsr2;
    track.releaseVolume = volume;
    if (delay == 0) {
      track.releaseTick.reset();
    }
  }

  void tick() {
    if (track.vibratoDepth != 0 && track.vibratoGrowthTicks != 0) {
      const u32 divisor = static_cast<u8>(program.tempo.currentRaw());
      track.vibratoElapsedUs += (divisor == 0 ? 256 : divisor) * 125;
      const u32 clocks = track.vibratoElapsedUs / 10000;
      const u32 growth =
          std::min<u32>(track.vibratoGrowthTicks, clocks > track.vibratoDelay ? clocks - track.vibratoDelay : 0);
      if (growth != track.vibratoGrowthApplied) {
        track.vibratoGrowthApplied = growth;
        const double depth = track.vibratoDepth / 256.0 + growth * (track.vibratoDepth * track.vibratoGrowth / 65536.0);
        out.vibratoDepth(12.0 * std::log2(1.0 + depth),
                         LfoPerformanceContext{.shape = LfoShape{.waveform = LfoWaveform::Sine}});
      }
    }
    if (program.lastTick != vm.tick()) {
      program.lastTick = vm.tick();
      if (program.master.tick().shouldApply()) {
        out.masterLevel(program.master.currentRaw() / 255.0);
      }
      if (program.tempo.tick().shouldApply()) {
        out.tempo(tempoUs(static_cast<u8>(program.tempo.currentRaw())));
      }
    }
    if (track.volume.tick().shouldApply()) {
      out.level(track.volume.currentRaw() / 255.0);
    }
    if (track.pitchRemaining != 0) {
      if (track.pitchDelay != 0) {
        --track.pitchDelay;
      } else {
        if (track.pitchIsDelta) {
          if (track.pitchRemaining != 0xff) {
            --track.pitchRemaining;
          }
          if (track.pitchRemaining != 0) {
            track.pitchScale += track.pitchDelta;
          }
        } else {
          static_cast<void>(track.pitch.tick());
          track.pitchScale = track.pitch.currentRaw();
          --track.pitchRemaining;
        }
        out.pitchBend(12.0 * std::log2(std::max(1.0, track.pitchScale) / track.pitchOrigin));
      }
    }
    if (track.releaseTick && vm.tick() >= *track.releaseTick) {
      track.releaseTick.reset();
      track.releaseApplied = true;
      if (track.releaseAdsr1 != 0 && track.lastKey && vm.tick() < track.noteEndTick) {
        // Nonzero release ADSR1 retriggers after the key-off gap scheduled by
        // note(). Direct/increasing GAIN instead changes expression below.
        out.replaceEnvelope(snesDspEnvelope(track.releaseAdsr1, track.releaseAdsr2, 0));
        out.expression(track.releaseVolume == 0 ? 1.0 : track.releaseVolume / 256.0);
        track.lastNote =
            out.note(*track.lastKey, track.velocity / 255.0, static_cast<u32>(track.noteEndTick - vm.tick()));
      } else {
        track.releaseGainActive = true;
        track.releaseGainElapsed = 0;
      }
    }
    if (track.releaseGainActive) {
      // Direct/increasing GAIN controls amplitude without a second attack.
      // The full-scale starting ENVX is the same approximation used by the
      // shared SNES envelope converter for a static instrument.
      const s16 level = snesDspGainEnvelopeValue(track.releaseAdsr2, 0x7ff, track.releaseGainElapsed);
      out.expression(level / 2047.0);
      const u32 divisor = static_cast<u8>(program.tempo.currentRaw());
      track.releaseGainElapsed += (divisor == 0 ? 256 : divisor) * 0.000125;
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] u16 shortLength(u8 value) {
  return value == 0x7e ? 0x90 : value == 0x7f ? 0xc0 : value;
}

[[nodiscard]] std::optional<u8> packedParameter(Cursor::Event& event) {
  const auto next = event.peekU8();
  // Unlike standard N-SPC, zero belongs to the end command.
  if (next && *next != 0 && *next < 0x80) {
    return event.u8("quantize_velocity", SourceValueDisplay::Hex);
  }
  return std::nullopt;
}

[[nodiscard]] Slide readSlide(Cursor::Event& event) {
  Slide slide;
  slide.delay = event.u8("delay");
  slide.length = event.u8("length");
  slide.note = event.u8("target_note", SourceValueDisplay::MidiNote);
  return slide;
}

// Ties are scanned before key-on so a gate applies to the whole chain. Consume
// optional parameters only after confirming the following C8; otherwise those
// bytes belong to the next command. F9 immediately after a note is read first.
[[nodiscard]] std::vector<TieSegment> readTies(Cursor::Event& event, ByteReader reader) {
  std::vector<TieSegment> ties;
  while (ties.size() < 256) {
    u32 address = static_cast<u32>(event.nextAddress().value);
    if (!reader.has(address, 1)) {
      break;
    }
    TieSegment tie;
    u32 count = 0;
    const u8 first = reader.u8At(address);
    if (first != 0 && first < 0x80) {
      tie.length = shortLength(first);
      ++count;
      if (reader.has(address + count, 1)) {
        const u8 packed = reader.u8At(address + count);
        if (packed != 0 && packed < 0x80) {
          tie.gate = (packed >> 4) & 7;
          ++count;
        }
      }
    }
    if (!reader.has(address + count, 1) || reader.u8At(address + count) != 0xc8) {
      break;
    }
    for (u32 i = 0; i <= count; ++i) {
      event.u8(fmt::format("tie_{}_{}", ties.size(), i), SourceValueDisplay::Hex);
    }
    ties.push_back(tie);
  }
  return ties;
}

[[nodiscard]] InstrumentBytes readInstrument(Cursor::Event& event) {
  InstrumentBytes bytes;
  constexpr std::array names{"srcn", "adsr1", "adsr2", "gain", "pitch_high", "pitch_low"};
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = event.u8(names[i], SourceValueDisplay::Hex);
  }
  return bytes;
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, const Layout& layout, u32 begin,
                                                   std::vector<Diagnostic>* diagnostics, u8 forwardingDepth = 0) {
  Cursor cursor(reader, begin, "nin-snes-quest", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if ((opcode > 0 && opcode < 0x80) || opcode == 0xde) {
    auto event = cursor.command("Note Parameters", SequenceSemantic::State);
    const u16 length = opcode == 0xde
                           ? event.u16le("length", SourceValueDisplay::Decimal)
                           : event.opcodeValue("length", shortLength(opcode));
    const auto packed = packedParameter(event);
    u8 velocity = 0;
    if (packed) {
      const u8 index = *packed & 15;
      // The table includes the index, then the driver squares the
      // adjusted byte. Master and channel volume are linear multipliers.
      const u8 raw = static_cast<u8>(layout.volumeTable[index] - index - 1);
      velocity = (raw * raw) >> 8;
      event.derived("velocity", velocity);
    }
    return event.invoke<&Playback::parameters>(length, packed, velocity);
  }
  if (opcode >= 0x80 && opcode < 0xd8) {
    auto event = cursor.command(opcode == 0xc9   ? "Rest"
                                : opcode == 0xc8 ? "Tie"
                                                 : "Note",
                                opcode == 0xc9 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    std::optional<Slide> slide;
    if (opcode != 0xc8 && opcode != 0xc9 && event.peekU8() == 0xf9) {
      event.u8("pitch_slide", SourceValueDisplay::Hex);
      slide = readSlide(event);
    }
    const auto ties = readTies(event, reader);
    return event.invoke<&Playback::note>(opcode, ties, slide);
  }

  switch (opcode) {
    case 0: {
      return cursor.command("Section End / Pattern Return", SequenceSemantic::End)
          .invoke<&Playback::end>()
          .return_();
    }
    case 0xd8: {
      auto event = cursor.command("Legato", SequenceSemantic::State);
      return event.invoke<&Playback::legato>(event.u8("off") == 0);
    }
    case 0xd9: {
      auto event = cursor.sourceOnly("Remote Voice Command");
      const u8 target = event.u8("track", SemanticOperandRole::Channel);
      if (forwardingDepth >= 4 || event.peekU8() < 0xd8) {
        event.warning("Invalid Quest remote voice command");
        return event.stop();
      }
      // Parse the embedded command with its actual variable operand layout.
      // A same-channel command can execute directly. Cross-channel commands
      // are SFX controls; keep their bytes and label in the source model.
      auto nested =
          decodeCommand(reader, layout, static_cast<u32>(event.nextAddress().value), diagnostics, forwardingDepth + 1);
      auto prefix = static_cast<DecodedBytecodeCommand>(event);
      nested.range = reader.range(begin, nested.range.endOffset() - begin);
      nested.opcode = opcode;
      nested.presentation.label = fmt::format("Voice {}: {}", target + 1, nested.presentation.label);
      nested.operands.insert(nested.operands.begin(), prefix.operands.begin(), prefix.operands.end());
      const auto body = nested.execution.body;
      nested.execution.body = [target, body](void* state) {
        auto& playback = *static_cast<Playback*>(state);
        return target == playback.track.number && body ? body(state) : playback.vm.fallthrough();
      };
      return nested;
    }
    case 0xda: {
      auto event = cursor.command("Conditional Jump", SequenceSemantic::Jump);
      const Address destination{
          event.u16le("destination", SourceValueDisplay::Address, SemanticOperandRole::JumpTarget)};
      return event.invokeFlow<&Playback::conditionalJump>(destination).discoverTarget(destination);
    }
    case 0xdb:
    case 0xdc:
    case 0xdd: {
      auto event = cursor.command(opcode == 0xdb   ? "GAIN And Wait"
                                  : opcode == 0xdc ? "Pitch And Wait"
                                                   : "Wait",
                                  SequenceSemantic::State);
      if (opcode == 0xdb) {
        const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
        event.invokeFlow([gain](Playback& p) {
          p.envelope(0, p.track.instrument[2], gain);
          return Effects{};
        });
      } else if (opcode == 0xdc) {
        const u16 pitch = event.u16le("pitch", SourceValueDisplay::Hex);
        event.invokeFlow([pitch](Playback& p) {
          if (p.track.lastNote.valid()) {
            p.out.pitchBend(12.0 * std::log2(std::max<u16>(1, pitch) / p.track.pitchOrigin));
          }
          return Effects{};
        });
      } else {
        event.u8("unused", SourceValueDisplay::Hex);
      }
      return event.invoke<&Playback::note>(u8{0xc9}, readTies(event, reader), std::optional<Slide>{});
    }
    case 0xdf: {
      auto event = cursor.command("Random Pattern", SequenceSemantic::Call);
      const u8 count = event.u8("count_and_mode", SourceValueDisplay::Hex) & 0x7f;
      if (count == 0) {
        event.warning("Quest random pattern has no destinations");
        return event.stop();
      }
      Address first;
      for (u8 i = 0; i < count; ++i) {
        const Address target{event.u16le(fmt::format("destination_{}", i), SourceValueDisplay::Address,
                                         SemanticOperandRole::CallTarget)};
        if (i == 0) {
          first = target;
        }
        event.discoverTarget(target);
      }
      // A deterministic export chooses the first alternative. The driver
      // replaces its entire call stack with one return after the table.
      event.invokeFlow([first](Playback& p) {
        p.track.calls.clear();
        return p.call(1, first);
      });
      return event.call(first);
    }
    case 0xe0: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      const u8 patch = event.u8("program", SemanticOperandRole::Instrument);
      if (patch == 0xff) {
        const u8 clock = event.u8("noise_clock");
        return event.invoke<&Playback::noise>(clock, reader.range(begin, 3));
      }
      return event.invoke<&Playback::instrument>(patch);
    }
    case 0xe1: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan"));
    }
    case 0xe2:
    case 0xe9: {
      auto event = cursor.command("Global Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transpose>(event.s8("semitones"));
    }
    case 0xe3: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay");
      const u8 rate = event.u8("rate");
      const u8 depth = event.u8("depth");
      return event.invoke<&Playback::vibrato>(delay, rate, depth);
    }
    case 0xe4:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case 0xe5: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::master>(event.u8("volume"));
    }
    case 0xe6: {
      auto event = cursor.command("Master Volume Fade", SequenceSemantic::Level);
      const u8 length = event.u8("length");
      const u8 value = event.u8("volume");
      return event.invoke<&Playback::masterFade>(length, value);
    }
    case 0xe7: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempo>(event.u8("bpm"));
    }
    case 0xe8: {
      auto event = cursor.command("Tempo Fade", SequenceSemantic::Tempo);
      const u8 length = event.u8("length");
      const u8 value = event.u8("bpm");
      return event.invoke<&Playback::tempoFade>(length, value);
    }
    case 0xea: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones"));
    }
    case 0xeb: {
      auto event = cursor.command("Loop Start", SequenceSemantic::Loop);
      const u8 count = event.u8("count");
      return event.invoke<&Playback::loopStart>(count, event.nextAddress());
    }
    case 0xec:
      return cursor.command("Loop End", SequenceSemantic::Repeat).invoke<&Playback::loopEnd>();
    case 0xed: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume"));
    }
    case 0xee: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u8 length = event.u8("length");
      const u8 value = event.u8("volume");
      return event.invoke<&Playback::volumeFade>(length, value);
    }
    case 0xef: {
      auto event = cursor.command("Pattern Play", SequenceSemantic::Call);
      const Address destination{
          event.u16le("destination", SourceValueDisplay::Address, SemanticOperandRole::CallTarget)};
      const u8 count = event.u8("count");
      event.invoke<&Playback::call>(count, destination);
      return count == 0xff ? event.jump(destination) : event.call(destination);
    }
    case 0xf0: {
      auto event = cursor.command("Vibrato Growth", SequenceSemantic::Modulation);
      const u8 length = event.u8("length");
      const u8 depth = event.u8("depth_step");
      return event.invoke<&Playback::growVibrato>(length, depth);
    }
    case 0xf1:
    case 0xf2: {
      auto event = cursor.command("Pitch Envelope", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay");
      const u8 length = event.u8("length");
      const s16 delta = event.s16le("pitch_step");
      return event.invoke<&Playback::pitchEnvelope>(delay, length, delta);
    }
    case 0xf3:
      return cursor.command("Pitch Envelope Off", SequenceSemantic::Pitch).invoke<&Playback::pitchOff>();
    case 0xf4: {
      auto event = cursor.command("Tuning", SequenceSemantic::Pitch);
      return event.set<&TrackState::tuning>(event.u8("tuning"));
    }
    case 0xf5: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::State);
      const u8 volume = event.u8("volume");
      const u8 left = event.u8("left_phase", SourceValueDisplay::Hex);
      const u8 right = event.u8("right_phase", SourceValueDisplay::Hex);
      return event.invoke<&Playback::echo>(volume, left, right);
    }
    case 0xf6: {
      // F6 00 cancels the release timer. Otherwise read ADSR1 and ADSR2/GAIN,
      // plus volume only for nonzero ADSR1. FF uses the note gate as the delay.
      auto event = cursor.command("Release Envelope", SequenceSemantic::Envelope);
      const u8 delay = event.u8("delay");
      u8 adsr1 = 0, adsr2 = 0, volume = 0;
      if (delay != 0) {
        adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
        adsr2 = event.u8("adsr2_or_gain", SourceValueDisplay::Hex);
        if (adsr1 != 0) {
          volume = event.u8("volume");
        }
      }
      return event.invoke<&Playback::release>(delay, adsr1, adsr2, volume);
    }
    case 0xf7: {
      // A negative first byte selects feedback-only mode. Otherwise FIR 4
      // carries eight signed coefficients, retained as source fields; playback
      // records the filter index rather than emulating the custom DSP filter.
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u8 delay = event.u8("delay_or_feedback_mode", SourceValueDisplay::Hex);
      const s8 feedback = event.s8("feedback");
      if (delay >= 0x80) {
        return event.invoke<&Playback::echoFeedback>(feedback);
      }
      const u8 filter = event.u8("filter");
      if (filter == 4) {
        for (u8 i = 0; i < 8; ++i) {
          event.s8(fmt::format("fir_{}", i));
        }
      }
      return event.invoke<&Playback::echoParameters>(delay, feedback, filter);
    }
    case 0xf8:
    case 0xfc: {
      auto event = cursor.command("Channel Echo", SequenceSemantic::State);
      return event.invoke<&Playback::channelEcho>(event.u8("off") == 0);
    }
    case 0xf9: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Pitch);
      return event.invoke<&Playback::startSlide>(readSlide(event));
    }
    case 0xfa: {
      auto event = cursor.command("Percussion Base / Random Byte", SequenceSemantic::State);
      const u8 value = event.u8("base_or_random", SourceValueDisplay::Hex);
      if (value < 0x80) {
        return event.invoke<&Playback::percussionBase>(value);
      }
      // Random memory writes retain their operands but have no exported effect.
      event.u16le("address", SourceValueDisplay::Address);
      event.u8("minimum");
      event.u8("maximum");
      return event.ignore();
    }
    case 0xfb:
      return cursor.sourceOnly("Enable Global Pan Offset");
    case 0xfd: {
      // 00-FB write a six-byte table row (index masked to 7 bits).
      // FF loads six inline bytes; FE edits SRCN+ADSR+GAIN; FD edits ADSR+GAIN;
      // FC adds a signed 16-bit delta to the active pitch multiplier.
      auto event = cursor.command("Instrument Data", SequenceSemantic::Program);
      const u8 mode = event.u8("instrument_or_mode", SourceValueDisplay::Hex);
      if (mode < 0xfc || mode == 0xff) {
        const auto bytes = readInstrument(event);
        const auto source = reader.range(begin, event.nextAddress().value - begin);
        if (mode == 0xff) {
          return event.invoke<&Playback::inlineInstrument>(bytes, source, false);
        }
        return event.invoke<&Playback::writeInstrument>(mode, bytes, source);
      }
      if (mode == 0xfc) {
        const s16 delta = event.s16le("pitch_delta");
        return event.invoke<&Playback::instrumentTuning>(delta, reader.range(begin, 4));
      }
      const auto srcn = mode == 0xfe ? std::optional<u8>{event.u8("srcn")} : std::nullopt;
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
      return event.invoke<&Playback::partialInstrument>(srcn, adsr1, adsr2, gain,
                                                        reader.range(begin, event.nextAddress().value - begin));
    }
    case 0xfe: {
      // Model only the DA branch condition. Other driver RAM writes are kept
      // as source fields, as are the SFX global-pan controls in FB.
      auto event = cursor.sourceOnly("Write Driver Byte");
      const u8 address = event.u8("address", SourceValueDisplay::Address);
      const u8 value = event.u8("value", SourceValueDisplay::Hex);
      if (address == 0xdf) {
        return event.invokeFlow([value](Playback& p) {
          p.program.config.condition = value;
          return Effects{};
        });
      }
      return event;
    }
    case 0xff: {
      auto event = cursor.command("Retain Voice After Note", SequenceSemantic::State);
      return event.set<&TrackState::retainVoice>(event.u8("off") == 0);
    }
    default:
      return cursor.sourceOnly("Unknown Quest Command").stop();
  }
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (!kDispatch.find(reader) || !kInstrument.find(reader)) {
    return std::nullopt;
  }
  const auto song = kSongList.find(reader);
  const auto gate = kGate.find(reader);
  const auto velocity = kVelocity.find(reader);
  const auto pan = kPan.find(reader);
  const auto dsp = kDspInit.find(reader);
  if (!song || !gate || !velocity || !pan || !dsp) {
    return std::nullopt;
  }
  const u16 songPointer = reader.le16(*song + 9);
  if (!reader.has(songPointer, 2) || reader.le16(*song + 12) != songPointer + 1) {
    return std::nullopt;
  }
  Layout layout{.signature = Signature::Quest,
                .profile = ProfileId::Quest,
                .songListAddress = reader.le16(songPointer),
                .sectionPointerAddress = 0x40,
                .instrumentTableAddress = 0x300};
  // The driver copies separate BGM/SFX tables. Read the BGM tables referenced by
  // the gate and velocity routines, not the adjacent SFX copies.
  layout.durationRateTable = table(reader, reader.le16(*gate + 12), 8);
  layout.volumeTable = table(reader, reader.le16(*velocity + 7), 16);
  layout.questPanTable = table(reader, reader.le16(*pan + 7), 21);
  const auto right = table(reader, reader.le16(*pan + 2), 21);
  layout.questPanTable.insert(layout.questPanTable.end(), right.begin(), right.end());
  // DSP registers are initialized from register/value pairs. Read DIR from
  // that list to locate the sample directory.
  const u16 registers = reader.le16(*dsp + 1);
  if (reader.le16(*dsp + 8) != registers + 1) {
    return std::nullopt;
  }
  for (u32 i = 0; i < 0x80 && reader.has(registers + i, 2); i += 2) {
    const u8 reg = reader.u8At(registers + i);
    if (reg >= 0x80) {
      break;
    }
    if (reg == 0x5d) {
      layout.spcDirAddress = static_cast<u16>(reader.u8At(registers + i + 1) << 8);
    }
  }
  if (layout.durationRateTable.size() != 8 || layout.volumeTable.size() != 16 || layout.questPanTable.size() != 42 ||
      !layout.spcDirAddress) {
    return std::nullopt;
  }
  // Requests 1-15 select (song-1)*2. Try the pending request before the current
  // song; raw input ports also contain the driver's handshake/control traffic.
  std::vector<u8> candidates;
  for (u8 index : {reader.u8At(0xb9), reader.u8At(0xbc), reader.u8At(0xf4)}) {
    if (index > 0 && index < 0x10) {
      candidates.push_back(index);
    }
  }
  for (u8 index = 1; index < 0x10; ++index) {
    candidates.push_back(index);
  }
  for (const u8 index : candidates) {
    const u32 entry = layout.songListAddress + (index - 1) * 2;
    if (!reader.has(entry, 2)) {
      continue;
    }
    layout.songIndex = index;
    layout.playlistAddress = reader.le16(entry);
    if (layout.playlistAddress >= 0x100 && isValidPlaylist(reader, layout)) {
      return layout;
    }
  }
  return std::nullopt;
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, SectionPlaylist playlist, AssetId sequenceId,
                             std::optional<SourceAnnotationId> parent, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  Config config{.duration = layout.durationRateTable, .pan = layout.questPanTable, .condition = reader.u8At(0xdf)};
  // A manually supplied layout can use the driver's documented defaults.
  if (config.duration.size() != 8) {
    config.duration = {0x23, 0x46, 0x69, 0x8c, 0xaf, 0xd2, 0xf5, 0xff};
  }
  if (config.pan.size() != 42) {
    config.pan = {0, 8, 17, 26, 35, 44, 55, 67, 80, 95, 104, 110, 114, 117, 119, 121, 123, 124, 125, 126, 127};
    const auto left = config.pan;
    config.pan.insert(config.pan.end(), left.rbegin(), left.rend());
  }
  Layout decodeLayout = layout;
  if (decodeLayout.volumeTable.size() != 16) {
    decodeLayout.volumeTable = {0x19, 0x28, 0x37, 0x46, 0x55, 0x64, 0x73, 0x82,
                                0x91, 0xa0, 0xaf, 0xbe, 0xcd, 0xdc, 0xeb, 0xff};
  }
  if (layout.instrumentTableAddress) {
    for (u32 i = 0; i < config.instruments.size(); ++i) {
      const u32 address = *layout.instrumentTableAddress + i * 6;
      if (reader.has(address, 6)) {
        std::ranges::copy(reader.slice(address, 6), config.instruments[i].begin());
      }
    }
  }
  SequenceProgram program = sequenceConfig().makeProgram();
  program.behavior.initialTempoMicrosecondsPerQuarter = tempoUs(0x80);
  program.behavior.initialMasterLevel = 0;
  program.behavior.initialPitchBendRangeSemitones = 24;
  program.sectionPlaylist = std::move(playlist);
  // Playlists contain words: zero ends, 01-FE count additional plays, FF
  // repeats forever. A repeat's next word is its playlist destination; larger
  // command words address sections of eight little-endian track pointers.
  // A pointer with a zero high byte disables that track. The playlist advances
  // only when every active track ends; tracks may also loop independently.
  program.sectionPlaylist->waitForAllTracks = true;
  const TrackDecodeScope scope{.reader = reader,
                               .maxCommands = 32768,
                               .sequenceAsset = sequenceId,
                               .parentAnnotation = parent,
                               .sourceMap = sourceMap};
  for (u8 track = 0; track < kTrackCount; ++track) {
    std::vector<Address> starts;
    for (const auto& section : program.sectionPlaylist->commands) {
      if (section.kind == PlaylistCommandKind::PlaySection && track < section.trackStarts.size() &&
          section.trackStarts[track]) {
        starts.push_back(*section.trackStarts[track]);
      }
    }
    program.tracks.push_back(scope.decode(
        track, starts, [&](u32 address) { return decodeCommand(reader, decodeLayout, address, diagnostics); }));
  }
  program.runtime = makeCompiledRuntime<Cursor, ProgramState>(std::move(config));
  auto recipes = analyzeCompiledProgram<ProgramState>(program, &ProgramState::recipes, diagnostics);
  return SequenceParse{.program = std::move(program), .recipes = std::move(recipes)};
}

}  // namespace vgmtrans::formats::nin_snes::quest
