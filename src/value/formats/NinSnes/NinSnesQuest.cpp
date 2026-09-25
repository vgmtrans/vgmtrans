/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnesQuest.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

// Quest's N-SPC variants used by Ogre Battle and Tactics Ogre.
//
// This driver shares N-SPC's section playlists and six-byte instruments, but
// changes note gates, command layouts, and timing enough to need its own player.
// The playlist parser, VM, instrument recipes, and sound-bank builder are shared.
//
// Tactics Ogre track bytes (hex): 01-7D set length; 7E/7F mean 144/192 ticks; DE sets a
// 16-bit length. An optional nonzero byte below 80 packs a three-bit gate index
// above a four-bit velocity index.
// 80-C7 are notes, C8 a tie, C9 a rest, CA-D7 percussion, and D8-FF commands.
// 00 ends a track or returns/repeats a pattern. The shared and variant command
// decoders document operand layouts; embedded zero bytes belong to the command.
// Ogre Battle doubles all short lengths, accepts a zero packed parameter, and
// uses CA-DF for percussion. Its command differences are in decodeOgreBattleCommand.
// It squares channel/master levels and uses tick-driven triangle modulation;
// Tactics Ogre uses linear levels and an independently clocked sine vibrato.

namespace vgmtrans::formats::nin_snes::quest {

using namespace core;

namespace {

// E7 divides a clock constant by the tempo operand: 0x082a in Ogre Battle,
// 0x1036 in Tactics Ogre. Preserve SPC700 DIV's overflow result, including zero.
[[nodiscard]] u8 tempoDivisor(u8 tempo, u16 dividend) {
  return (dividend >> 8) < tempo * 2
      ? static_cast<u8>(dividend / tempo)
      : static_cast<u8>(255 - (dividend - tempo * 512) / (256 - tempo));
}

// One tick lasts 125 us per timer unit; a zero timer target means 256.
// The 48-PPQN export timebase does not make the operand itself the exported BPM.
[[nodiscard]] u32 tempoUs(u8 divisor) {
  return kPpqn * 125u * (divisor == 0 ? 256u : divisor);
}

// SRCN, ADSR1, ADSR2, GAIN, then a pitch multiplier normalized to big-endian.
using InstrumentBytes = std::array<u8, 6>;

struct Config {
  bool ogreBattle = false;
  bool sfx = false;
  std::vector<u8> duration;
  std::vector<u8> pan;
  std::array<InstrumentBytes, 256> instruments{};
  std::array<SourceRange, 256> instrumentSources{};
  std::array<u8, 2> conditions{};
  u8 condition = 0;
};

// Ogre Battle accumulates integer pitch steps (or 8.8 amplitude steps).
// A triangle reverses at quarter-cycle boundaries; a slide simply runs out.
// Note attacks reset the counters but retain the current step direction.
struct OgreBattleMotion {
  bool enabled = false;
  bool triangle = false;
  u8 delay = 0;
  u8 period = 0;
  u8 waiting = 0;
  u8 remaining = 0;
  u8 phase = 0;
  s16 step = 0;
  u16 value = 0;

  void restart(u16 initial) {
    waiting = delay;
    remaining = period;
    phase = 0;
    value = initial;
  }

  bool tick() {
    if (!enabled) {
      return false;
    }
    if (waiting != 0) {
      --waiting;
      return false;
    }
    if (triangle && remaining == 0) {
      remaining = period;
      if (++phase & 1) {
        step = static_cast<s16>(-step);
      }
    } else if (!triangle && remaining == 0) {
      return false;
    }
    if (triangle || remaining != 0xff) {
      --remaining;
    }
    value = static_cast<u16>(value + step);
    return true;
  }
};

struct CallFrame {
  Address start;
  bool infinite = false;
};

struct TrackState {
  explicit TrackState(TrackStateContext track) : number(track.sourceTrackNumber) {}

  u32 number = 0;
  u16 length = 1;
  u8 gate = 0;
  u8 velocity = 0;
  u8 tieVelocity = 0;
  u16 tiedLength = 0;
  bool tieEligible = false;
  u64 noteStartTick = 0;
  u8 logicalProgram = 0;
  u32 activeProgram = 0;
  u8 percussionNote = 0;
  InstrumentBytes instrument{};
  s8 transpose = 0;
  u8 tuning = 0;
  bool legato = false;
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
  OgreBattleMotion ogreBattlePitch;
  OgreBattleMotion ogreBattleTremolo;
};

struct ProgramState {
  explicit ProgramState(const Config& config) : config(config), instruments(config.instruments) {
    for (u32 i = 0; i < programs.size(); ++i) {
      programs[i] = i;
    }
    if (config.sfx) {
      master.reset(0xe6);
      tempo.reset(0x43);
    }
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
    const u32 id = config.ogreBattle ? logical : 0x80 + static_cast<u32>(versions.size());
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
  std::array<InstrumentBytes, 256> instruments;
  std::array<u32, 256> programs{};
  std::map<std::tuple<u8, InstrumentBytes, bool>, u32> versions;
  SequenceRecipes recipes;
  s8 transpose = 0;
  u8 percussionBase = 0;
  SequenceFixedPointAutomation<s32> master;
  SequenceFixedPointAutomation<s32> tempo{0x80};
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

struct Playback : SequencePlayback<TrackState> {
  ProgramState& program;

  void beginSection(bool first) {
    track.calls.clear();
    track.percussionNote = 0;
    track.legato = false;
    track.tieEligible = false;
    if (first) {
      volume(program.config.ogreBattle ? (program.config.sfx ? 0xdc : 0xff) : 0);
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

  // Gating applies to the combined note/tie length. Tactics Ogre gate 7
  // subtracts one tick (Ogre Battle subtracts two). Other Tactics Ogre gates use
  // floor(floor(length / divisor) * rate / 256) * divisor, with divisor =
  // highByte + 1. Preserve both truncations for long notes.
  [[nodiscard]] u32 duration(u16 length) const {
    const u32 fullLength = length == 0 ? 0x10000u : length;
    if (track.legato) {
      return fullLength;
    }
    if (track.gate == 7) {
      return static_cast<u16>(length - (program.config.ogreBattle ? 2 : 1));
    }
    const u8 rate = program.config.duration[track.gate];
    if (program.config.ogreBattle) {
      return (length * rate) >> 8;
    }
    const u8 divisor = static_cast<u8>((length >> 8) + 1);
    if (divisor == 0) {
      return fullLength;
    }
    const u32 gate = ((length / divisor * rate) >> 8) * divisor;
    // A zero gate counter underflows instead of keying off at tick zero.
    return gate == 0 ? fullLength : gate;
  }

  void loadProgram(u8 logical) {
    track.logicalProgram = logical;
    track.instrument = program.instruments[logical];
    const u8 srcn = track.instrument[0];
    track.activeProgram = program.config.ogreBattle
        ? program.instrumentVersion(logical, track.instrument, program.config.instrumentSources[logical],
                                    srcn >= 0x80 && srcn < 0xc0)
        : program.programs[logical & 0x7f];
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.activeProgram});
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void instrument(u8 encoded) {
    track.percussionNote = 0xff;
    loadProgram(program.config.ogreBattle && encoded >= 0xfc ? encoded : encoded & 0x7f);
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
    if (program.config.ogreBattle) {
      return note < 72 ? static_cast<u16>((pitch * scale) >> 8) >> (5 - note / 12) : 0;
    }
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
      static_cast<void>(
          track.pitch.begin(track.pitch.toRawTarget(static_cast<s32>(pitchRegister(target)), slide.length)));
    }
    vibratoOff();
  }

  void tickTacticsOgrePitch() {
    if (track.pitchRemaining == 0) {
      return;
    }
    if (track.pitchDelay != 0) {
      --track.pitchDelay;
      return;
    }
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

  void pitchEnvelope(u8 delay, u8 length, s16 delta) {
    if (program.config.ogreBattle) {
      track.ogreBattlePitch = {.enabled = true, .delay = delay, .period = length, .step = delta};
      return;
    }
    track.pitchEnvelope = true;
    track.envelopeDelay = delay;
    track.envelopeLength = length;
    track.envelopeDelta = delta;
    vibratoOff();
  }

  void pitchOff() {
    track.ogreBattlePitch.enabled = false;
    track.pitchEnvelope = false;
    track.pitchRemaining = 0;
  }

  void vibrato(u8 delay, u8 rate, u8 depth) {
    if (program.config.ogreBattle) {
      ogreBattleModulation(track.ogreBattlePitch, delay, rate, depth, false);
      return;
    }
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
    if (program.config.ogreBattle) {
      return;
    }
    // Tactics Ogre's sine LFO runs on the independent 10 ms timer-0 clock. Its
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

  void tickVibratoGrowth() {
    if (track.vibratoDepth == 0 || track.vibratoGrowthTicks == 0) {
      return;
    }
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

  void ogreBattleModulation(OgreBattleMotion& motion, u8 delay, u8 rate, u8 depth, bool tremolo) {
    const u8 period = static_cast<u8>(-rate);
    const s16 step = period == 0 ? 0xff : (tremolo ? depth * 256 : depth) / period;
    motion = {.enabled = !tremolo || period != 0, .triangle = true, .delay = delay, .period = period, .step = step};
  }

  void emitOgreBattlePitch() {
    const u32 pitch = static_cast<u32>(track.pitchScale) & 0x3fff;
    const u32 origin = static_cast<u32>(track.pitchOrigin) & 0x3fff;
    out.pitchBend(12.0 * std::log2(std::max(1u, pitch) / static_cast<double>(std::max(1u, origin))));
  }

  void tickOgreBattleModulation() {
    if (track.ogreBattlePitch.tick()) {
      track.pitchScale = track.ogreBattlePitch.value;
      emitOgreBattlePitch();
    }
    if (track.ogreBattleTremolo.tick()) {
      out.expression((track.ogreBattleTremolo.value >> 8) / 128.0);
    }
  }

  // Ogre Battle ties cross pattern calls and returns. Extend the original
  // event as each tie executes, preserving the velocity of the initial note.
  [[nodiscard]] Effects extendOgreBattleTie() {
    if (!track.tieEligible) {
      return {};
    }
    track.velocity = track.tieVelocity;
    track.tiedLength = static_cast<u16>(track.tiedLength + track.length);
    out.setNoteEnd(track.lastNote, track.noteStartTick + duration(track.tiedLength));
    return Effects::wait(track.length);
  }

  void restartModulation(u8 note, bool repeatedDrum) {
    track.pitchOrigin = pitchRegister(note);
    if (!program.config.ogreBattle || !repeatedDrum) {
      track.pitchScale = track.pitchOrigin;
    }
    track.ogreBattlePitch.restart(static_cast<u16>(track.pitchScale));
    track.ogreBattleTremolo.restart(0x8000);
    if (program.config.ogreBattle) {
      out.expression(1.0);
      emitOgreBattlePitch();
    } else {
      out.pitchBend(0);
    }
    track.pitchRemaining = 0;
    out.tuning(track.tuning * (100.0 / 256.0));
    emitVibrato();
    track.vibratoElapsedUs = 0;
    track.vibratoGrowthApplied = 0;
  }

  [[nodiscard]] Effects note(u8 opcode, const std::vector<TieSegment>& ties, std::optional<Slide> slide) {
    if (program.config.ogreBattle) {
      if (opcode == 0xc8) {
        return extendOgreBattleTie();
      }
      track.tieEligible = true;
      track.tieVelocity = track.velocity;
      track.tiedLength = track.length;
      track.noteStartTick = vm.tick();
      if (opcode == 0xc9) {
        track.lastNote = {};
      }
    }
    // Tactics Ogre lookahead carries the last tie's length and gate into subsequent notes,
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
    const bool repeatedDrum = drum && track.percussionNote == opcode;
    // Repeated percussion retains its active instrument, even after a table
    // write. E0 and section changes invalidate this cache. A following melodic
    // note also keeps the percussion instrument until another program is loaded.
    if (drum && track.percussionNote != opcode) {
      track.percussionNote = opcode;
      const u8 patch = static_cast<u8>(opcode - 0xca + program.percussionBase + (program.config.sfx ? 0x30 : 0));
      loadProgram(program.config.ogreBattle ? patch : patch & 0x7f);
    }
    // Melodic notes carry overflow from global transpose into the channel
    // addition. Percussion clears carry and applies only channel transpose.
    const u16 globalSum = (opcode & 0x7f) + static_cast<u8>(program.transpose);
    u8 raw = static_cast<u8>((drum ? 0x24 : globalSum + (globalSum > 0xff)) + static_cast<u8>(track.transpose));
    if (program.config.ogreBattle) {
      raw = repeatedDrum && track.lastKey ? static_cast<u8>(*track.lastKey - 24)
                                         : drum ? raw : std::min<u8>(raw, 0x47);
    }
    const double key = 24.0 + raw;
    program.recipes.usedNotes.emplace(track.activeProgram, static_cast<u8>(std::min(24u + raw, 127u)));
    restoreAttackEnvelope();
    restartModulation(raw, repeatedDrum);
    const bool held = track.legato && track.lastNote.valid();
    const u32 sounding = prepareRelease(duration(total), wait);
    const NotePerformanceEvent event{.key = key,
                                     .linearVelocity = level(track.velocity),
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
    return Effects::wait(wait);
  }

  void legato(bool enabled) {
    track.legato = enabled;
    out.legatoPedal(enabled);
    if (!enabled && track.lastNote.valid()) {
      out.setNoteEnd(track.lastNote, vm.tick());
      track.lastNote = {};
    }
  }

  // Ogre Battle has one pattern frame; Tactics Ogre has four. Repeat slot 0
  // belongs to Tactics Ogre's EB/EC loop; other slots follow the pattern stack.
  // The VM owns return addresses.
  [[nodiscard]] Effects call(u8 count, Address destination) {
    const bool ogreBattle = program.config.ogreBattle;
    if (ogreBattle ? count == 0 : count == 0xff) {
      if (ogreBattle) {
        track.calls.clear();
      }
      return vm.jump(destination);
    }
    if (track.calls.size() == (ogreBattle ? 1 : 4)) {
      vm.diagnostic(Diagnostic{.severity = Severity::Warning, .message = "Quest subroutine stack overflow"});
      return vm.end();
    }
    // Tactics Ogre repeats 00 and 80-FE forever; Ogre Battle repeats only FF.
    const bool infinite = ogreBattle ? count == 0xff : count == 0 || count >= 0x80;
    if (!infinite) {
      vm.repeatCounter(static_cast<u8>(track.calls.size() + 1)).start(count);
    }
    track.calls.push_back({destination, infinite});
    return vm.call(destination);
  }

  [[nodiscard]] Effects end() {
    if (track.calls.empty()) {
      return program.config.sfx ? vm.end() : vm.endSection();
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

  // E1 selects one of 21 positions. Tactics Ogre adds left/right phase inversion
  // in bits 7/6; the gains also determine channel loudness.
  void pan(u8 value) {
    if (program.config.ogreBattle) {
      value = std::min<u8>(value, 20);
    }
    const u8 index = std::min<u8>(value & 0x3f, 20);
    const auto& panTable = program.config.pan;
    out.stereoBalance((panTable[index] / 128.0) * ((value & 0x80) ? -1 : 1),
                      (panTable[21 + index] / 128.0) * ((value & 0x40) ? -1 : 1));
  }

  [[nodiscard]] double level(double value) const {
    const double gain = value / 255.0;
    return program.config.ogreBattle ? gain * gain : gain;
  }

  void volume(u8 value) {
    track.volume.reset(value);
    out.level(level(value));
  }

  void volumeFade(u8 length, u8 target) {
    if (length != 0) {
      static_cast<void>(track.volume.begin(track.volume.toRawTarget(target, length)));
    }
  }

  void master(u8 value) {
    program.master.reset(value);
    out.masterLevel(level(value));
  }

  void masterFade(u8 length, u8 target) {
    if (length != 0) {
      static_cast<void>(program.master.begin(program.master.toRawTarget(target, length)));
    }
  }

  void tempo(u8 value) {
    if (program.config.sfx) {
      return;
    }
    // Only Tactics Ogre's immediate tempo command clamps overflowing results.
    const u8 divisor = !program.config.ogreBattle && value < 0x11
        ? 0xff : tempoDivisor(value, program.config.ogreBattle ? 0x082a : 0x1036);
    program.tempo.reset(divisor);
    out.tempo(tempoUs(divisor));
  }

  void tempoFade(u8 length, u8 target) {
    // E8 interpolates timer divisors, so interpolating BPM would change the fade.
    if (length != 0) {
      static_cast<void>(program.tempo.begin(program.tempo.toRawTarget(tempoDivisor(target, 0x1036), length)));
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

  // Tactics Ogre can switch envelopes during a note. Restore the attack
  // envelope for the next note before scheduling its release.
  void restoreAttackEnvelope() {
    if (track.releaseApplied) {
      out.replaceEnvelope(snesDspEnvelope(track.instrument[1], track.instrument[2], track.instrument[3]),
                          VoiceEnvelopeScope::FutureAttacks);
      out.expression(1.0);
      track.releaseApplied = false;
    }
    track.releaseGainActive = false;
  }

  [[nodiscard]] u32 prepareRelease(u32 gate, u32 wait) {
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
    track.noteEndTick = vm.tick() + wait;
    track.releaseTick.reset();
    if (track.releaseDelay != 0 && !fallingGain && releaseDelay < wait) {
      track.releaseTick = vm.tick() + releaseDelay;
    }
    return sounding;
  }

  void tickRelease() {
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

  void tick() {
    if (program.config.ogreBattle) {
      tickOgreBattleModulation();
    } else {
      tickVibratoGrowth();
    }
    if (program.lastTick != vm.tick()) {
      program.lastTick = vm.tick();
      if (program.master.tick().shouldApply()) {
        out.masterLevel(level(program.master.currentRaw()));
      }
      if (program.tempo.tick().shouldApply()) {
        out.tempo(tempoUs(static_cast<u8>(program.tempo.currentRaw())));
      }
    }
    if (track.volume.tick().shouldApply()) {
      out.level(level(track.volume.currentRaw()));
    }
    if (!program.config.ogreBattle) {
      tickTacticsOgrePitch();
      tickRelease();
    }
  }
};

using Cursor = CompilerCursor<Playback>;

[[nodiscard]] u16 shortLength(u8 value) {
  return value == 0x7e ? 0x90 : value == 0x7f ? 0xc0 : value;
}

[[nodiscard]] std::optional<u8> packedParameter(Cursor& cursor, bool ogreBattle = false) {
  const auto next = cursor.peekU8();
  // Tactics Ogre reserves zero for the end command; Ogre Battle accepts it here.
  if (next && (ogreBattle || *next != 0) && *next < 0x80) {
    return cursor.u8("quantize_velocity", SourceValueDisplay::Hex);
  }
  return std::nullopt;
}

[[nodiscard]] Slide readSlide(Cursor& cursor) {
  Slide slide;
  slide.delay = cursor.u8("delay");
  slide.length = cursor.u8("length");
  slide.note = cursor.u8("target_note", SourceValueDisplay::MidiNote);
  return slide;
}

// Ties are scanned before key-on so a gate applies to the whole chain. Consume
// optional parameters only after confirming the following C8; otherwise those
// bytes belong to the next command. F9 immediately after a note is read first.
[[nodiscard]] std::vector<TieSegment> readTies(Cursor& cursor, ByteReader reader) {
  std::vector<TieSegment> ties;
  while (ties.size() < 256) {
    u32 address = static_cast<u32>(cursor.nextAddress().value);
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
      cursor.u8(fmt::format("tie_{}_{}", ties.size(), i), SourceValueDisplay::Hex);
    }
    ties.push_back(tie);
  }
  return ties;
}

[[nodiscard]] InstrumentBytes readInstrument(Cursor& cursor) {
  InstrumentBytes bytes;
  constexpr std::array names{"srcn", "adsr1", "adsr2", "gain", "pitch_high", "pitch_low"};
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = cursor.u8(names[i], SourceValueDisplay::Hex);
  }
  return bytes;
}

// These commands have the same operand layout and purpose in both variants.
// Playback handles differences in their arithmetic and repeat-count semantics.
[[nodiscard]] std::optional<DecodedBytecodeCommand> decodeSharedCommand(Cursor& cursor, bool ogreBattle) {
  switch (cursor.opcode()) {
    case 0:
      return cursor.command("Section End / Pattern Return", SequenceSemantic::End)
          .invoke<&Playback::end>()
          .return_();
    case 0xe1:
      return cursor.command("Pan", SequenceSemantic::Pan).invoke<&Playback::pan>({cursor.u8("pan")});
    case 0xe3:
      return cursor.command("Vibrato", SequenceSemantic::Modulation)
          .invoke<&Playback::vibrato>({cursor.u8("delay"), cursor.u8("rate"), cursor.u8("depth")});
    case 0xe5:
      return cursor.command("Master Volume", SequenceSemantic::Level).invoke<&Playback::master>({cursor.u8("volume")});
    case 0xe6:
      return cursor.command("Master Volume Fade", SequenceSemantic::Level)
          .invoke<&Playback::masterFade>({cursor.u8("length"), cursor.u8("volume")});
    case 0xe7:
      return cursor.command("Tempo", SequenceSemantic::Tempo).invoke<&Playback::tempo>({cursor.u8("bpm")});
    case 0xe9:
      return cursor.command("Global Transpose", SequenceSemantic::Pitch)
          .invoke<&Playback::transpose>({cursor.s8("semitones")});
    case 0xea:
      return cursor.command("Transpose", SequenceSemantic::Pitch).set<&TrackState::transpose>(cursor.s8("semitones"));
    case 0xed:
      return cursor.command("Volume", SequenceSemantic::Level).invoke<&Playback::volume>({cursor.u8("volume")});
    case 0xee:
      return cursor.command("Volume Fade", SequenceSemantic::Level)
          .invoke<&Playback::volumeFade>({cursor.u8("length"), cursor.u8("volume")});
    case 0xef: {
      auto event = cursor.command("Pattern Play", SequenceSemantic::Call);
      const Address destination{
          cursor.u16le("destination", SourceValueDisplay::Address, SemanticOperandRole::CallTarget)};
      const u8 count = cursor.u8("count");
      event.invoke<&Playback::call>({count, destination});
      return (ogreBattle ? count == 0 : count == 0xff) ? event.jump(destination) : event.call(destination);
    }
    default:
      return std::nullopt;
  }
}

// Ogre Battle-specific commands. Echo placeholders consume fixed operands;
// CPU handshakes consume none.
[[nodiscard]] DecodedBytecodeCommand decodeOgreBattleCommand(Cursor& cursor) {
  const u8 opcode = cursor.opcode();
  switch (opcode) {
    case 0xe0:
      return cursor.command("Program", SequenceSemantic::Program)
          .invoke<&Playback::instrument>({cursor.u8("program", SemanticOperandRole::Instrument)});
    case 0xe2:
    case 0xe8:
    case 0xf0:
    case 0xf2:
    case 0xf5:
    case 0xf7:
    case 0xf9: {
      auto event = cursor.sourceOnly("Unused Command");
      const u8 count = opcode == 0xf5 || opcode == 0xf7 ? 3 : opcode == 0xe2 || opcode == 0xe8 ? 2
                                                                                          : opcode == 0xf0 ? 1 : 0;
      for (u8 i = 0; i < count; ++i) {
        cursor.u8(fmt::format("unused_{}", i), SourceValueDisplay::Hex);
      }
      return event;
    }
    case 0xe4:
    case 0xf3:
      return cursor.command("Pitch Modulation Off", SequenceSemantic::Modulation).invoke<&Playback::pitchOff>();
    case 0xeb: {
      auto event = cursor.command("Tremolo", SequenceSemantic::Modulation);
      const u8 delay = cursor.u8("delay");
      const u8 rate = cursor.u8("rate");
      const u8 depth = cursor.u8("depth");
      return event.invokeFlow([delay, rate, depth](Playback& p) {
        p.ogreBattleModulation(p.track.ogreBattleTremolo, delay, rate, depth, true);
        return Effects{};
      });
    }
    case 0xec:
      return cursor.command("Tremolo Off", SequenceSemantic::Modulation).invokeFlow([](Playback& p) {
        p.track.ogreBattleTremolo.enabled = false;
        p.out.expression(1.0);
        return Effects{};
      });
    case 0xf1:
      return cursor.command("Pitch Envelope", SequenceSemantic::Pitch)
          .invoke<&Playback::pitchEnvelope>(
              {cursor.u8("delay"), cursor.u8("length"), static_cast<s16>(cursor.s8("pitch_step"))});
    case 0xf4:
    case 0xfa:
      return cursor.command("Percussion Base", SequenceSemantic::State)
          .invoke<&Playback::percussionBase>({cursor.u8("base")});
    case 0xf6:
    case 0xf8:
    case 0xfc:
      return cursor.sourceOnly("CPU Handshake");
    case 0xfb: {
      auto event = cursor.command("Conditional Pan", SequenceSemantic::Pan);
      const u8 first = cursor.u8("pan_if_set");
      const u8 second = cursor.u8("pan_if_clear");
      return event.invokeFlow([first, second](Playback& p) {
        p.pan(p.program.config.conditions[p.track.number / 4] ? first : second);
        return Effects{};
      });
    }
    case 0xfd: {
      auto event = cursor.command("Conditional Pattern", SequenceSemantic::Call);
      const Address first{cursor.u16le("pattern_if_set", SourceValueDisplay::Address, SemanticOperandRole::CallTarget)};
      const Address second{
          cursor.u16le("pattern_if_clear", SourceValueDisplay::Address, SemanticOperandRole::CallTarget)};
      return event.invokeFlow([first, second](Playback& p) {
        return p.call(1, p.program.config.conditions[p.track.number / 4] ? first : second);
      }).call(first).discoverTarget(second);
    }
    case 0xfe: {
      auto event = cursor.sourceOnly("Write Driver Byte");
      const u8 address = cursor.u8("address", SourceValueDisplay::Address);
      const u8 value = cursor.u8("value", SourceValueDisplay::Hex);
      if (address == 0xa5 || address == 0xa6) {
        return event.invokeFlow([address, value](Playback& p) {
          p.program.config.conditions[address - 0xa5] = value;
          return Effects{};
        });
      }
      return event;
    }
    case 0xff:
      return cursor.command("Mute Music", SequenceSemantic::Level).invokeFlow([](Playback& p) {
        if (!p.program.config.sfx) {
          p.master(0);
        }
        return Effects{};
      });
    default:
      return cursor.sourceOnly("Unknown Quest Command").stop();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommandImpl(ByteReader reader, const Layout& layout, u32 begin,
                                                       std::vector<Diagnostic>* diagnostics, u8 forwardingDepth);

[[nodiscard]] DecodedBytecodeCommand decodeTacticsOgreCommand(
    Cursor& cursor, ByteReader reader, const Layout& layout, u32 begin,
    std::vector<Diagnostic>* diagnostics, u8 forwardingDepth) {
  const u8 opcode = cursor.opcode();
  switch (opcode) {
    case 0xd8:
      return cursor.command("Legato", SequenceSemantic::State).invoke<&Playback::legato>({cursor.u8("off") == 0});
    case 0xd9: {
      auto event = cursor.sourceOnly("Remote Voice Command");
      const u8 target = cursor.u8("track", SemanticOperandRole::Channel);
      if (forwardingDepth >= 4 || cursor.peekU8() < 0xd8) {
        cursor.warning("Invalid Quest remote voice command");
        return event.stop();
      }
      // Parse the embedded command with its actual variable operand layout.
      // A same-channel command can execute directly. Cross-channel commands
      // are SFX controls; keep their bytes and label in the source model.
      auto nested = decodeCommandImpl(reader, layout, static_cast<u32>(cursor.nextAddress().value), diagnostics,
                                      forwardingDepth + 1);
      auto prefix = static_cast<DecodedBytecodeCommand>(event);
      nested.range = reader.range(begin, nested.range.endOffset() - begin);
      nested.opcode = opcode;
      nested.presentation.label = fmt::format("Voice {}: {}", target + 1, nested.presentation.label);
      nested.fields.insert(nested.fields.begin(), prefix.fields.begin(), prefix.fields.end());
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
          cursor.u16le("destination", SourceValueDisplay::Address, SemanticOperandRole::JumpTarget)};
      return event.invokeFlow<&Playback::conditionalJump>({destination}).discoverTarget(destination);
    }
    case 0xdb:
    case 0xdc:
    case 0xdd: {
      auto event = cursor.command(opcode == 0xdb   ? "GAIN And Wait"
                                  : opcode == 0xdc ? "Pitch And Wait"
                                                   : "Wait",
                                  SequenceSemantic::State);
      if (opcode == 0xdb) {
        const u8 gain = cursor.u8("gain", SourceValueDisplay::Hex);
        event.invokeFlow([gain](Playback& p) {
          p.envelope(0, p.track.instrument[2], gain);
          return Effects{};
        });
      } else if (opcode == 0xdc) {
        const u16 pitch = cursor.u16le("pitch", SourceValueDisplay::Hex);
        event.invokeFlow([pitch](Playback& p) {
          if (p.track.lastNote.valid()) {
            p.out.pitchBend(12.0 * std::log2(std::max<u16>(1, pitch) / p.track.pitchOrigin));
          }
          return Effects{};
        });
      } else {
        cursor.u8("unused", SourceValueDisplay::Hex);
      }
      return event.invoke<&Playback::note>({u8{0xc9}, readTies(cursor, reader), std::optional<Slide>{}});
    }
    case 0xdf: {
      auto event = cursor.command("Random Pattern", SequenceSemantic::Call);
      const u8 count = cursor.u8("count_and_mode", SourceValueDisplay::Hex) & 0x7f;
      if (count == 0) {
        cursor.warning("Quest random pattern has no destinations");
        return event.stop();
      }
      Address first;
      for (u8 i = 0; i < count; ++i) {
        const Address target{cursor.u16le(fmt::format("destination_{}", i), SourceValueDisplay::Address,
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
      const u8 patch = cursor.u8("program", SemanticOperandRole::Instrument);
      if (patch == 0xff) {
        return event.invoke<&Playback::noise>({cursor.u8("noise_clock"), reader.range(begin, 3)});
      }
      return event.invoke<&Playback::instrument>({patch});
    }
    case 0xe2:
      return cursor.command("Global Transpose", SequenceSemantic::Pitch)
          .invoke<&Playback::transpose>({cursor.s8("semitones")});
    case 0xe4:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case 0xe8:
      return cursor.command("Tempo Fade", SequenceSemantic::Tempo)
          .invoke<&Playback::tempoFade>({cursor.u8("length"), cursor.u8("bpm")});
    case 0xeb:
      return cursor.command("Loop Start", SequenceSemantic::Loop)
          .invoke<&Playback::loopStart>({cursor.u8("count"), cursor.nextAddress()});
    case 0xec:
      return cursor.command("Loop End", SequenceSemantic::Repeat).invoke<&Playback::loopEnd>();
    case 0xf0:
      return cursor.command("Vibrato Growth", SequenceSemantic::Modulation)
          .invoke<&Playback::growVibrato>({cursor.u8("length"), cursor.u8("depth_step")});
    case 0xf1:
    case 0xf2:
      return cursor.command("Pitch Envelope", SequenceSemantic::Pitch)
          .invoke<&Playback::pitchEnvelope>({cursor.u8("delay"), cursor.u8("length"), cursor.s16le("pitch_step")});
    case 0xf3:
      return cursor.command("Pitch Envelope Off", SequenceSemantic::Pitch).invoke<&Playback::pitchOff>();
    case 0xf4:
      return cursor.command("Tuning", SequenceSemantic::Pitch).set<&TrackState::tuning>(cursor.u8("tuning"));
    case 0xf5:
      return cursor.command("Echo Volume", SequenceSemantic::State)
          .invoke<&Playback::echo>({cursor.u8("volume"), cursor.u8("left_phase", SourceValueDisplay::Hex),
                                    cursor.u8("right_phase", SourceValueDisplay::Hex)});
    case 0xf6: {
      // F6 00 cancels the release timer. Otherwise read ADSR1 and ADSR2/GAIN,
      // plus volume only for nonzero ADSR1. FF uses the note gate as the delay.
      auto event = cursor.command("Release Envelope", SequenceSemantic::Envelope);
      const u8 delay = cursor.u8("delay");
      u8 adsr1 = 0, adsr2 = 0, volume = 0;
      if (delay != 0) {
        adsr1 = cursor.u8("adsr1", SourceValueDisplay::Hex);
        adsr2 = cursor.u8("adsr2_or_gain", SourceValueDisplay::Hex);
        if (adsr1 != 0) {
          volume = cursor.u8("volume");
        }
      }
      return event.invoke<&Playback::release>({delay, adsr1, adsr2, volume});
    }
    case 0xf7: {
      // A negative first byte selects feedback-only mode. Otherwise FIR 4
      // carries eight signed coefficients, retained as source fields; playback
      // records the filter index rather than emulating the custom DSP filter.
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u8 delay = cursor.u8("delay_or_feedback_mode", SourceValueDisplay::Hex);
      const s8 feedback = cursor.s8("feedback");
      if (delay >= 0x80) {
        return event.invoke<&Playback::echoFeedback>({feedback});
      }
      const u8 filter = cursor.u8("filter");
      if (filter == 4) {
        for (u8 i = 0; i < 8; ++i) {
          cursor.s8(fmt::format("fir_{}", i));
        }
      }
      return event.invoke<&Playback::echoParameters>({delay, feedback, filter});
    }
    case 0xf8:
    case 0xfc:
      return cursor.command("Channel Echo", SequenceSemantic::State)
          .invoke<&Playback::channelEcho>({cursor.u8("off") == 0});
    case 0xf9:
      return cursor.command("Pitch Slide", SequenceSemantic::Pitch).invoke<&Playback::startSlide>({readSlide(cursor)});
    case 0xfa: {
      auto event = cursor.command("Percussion Base / Random Byte", SequenceSemantic::State);
      const u8 value = cursor.u8("base_or_random", SourceValueDisplay::Hex);
      if (value < 0x80) {
        return event.invoke<&Playback::percussionBase>({value});
      }
      // Random memory writes retain their operands but have no exported effect.
      cursor.u16le("address", SourceValueDisplay::Address);
      cursor.u8("minimum");
      cursor.u8("maximum");
      return event.ignore();
    }
    case 0xfb:
      return cursor.sourceOnly("Enable Global Pan Offset");
    case 0xfd: {
      // 00-FB write a six-byte table row (index masked to 7 bits).
      // FF loads six inline bytes; FE edits SRCN+ADSR+GAIN; FD edits ADSR+GAIN;
      // FC adds a signed 16-bit delta to the active pitch multiplier.
      auto event = cursor.command("Instrument Data", SequenceSemantic::Program);
      const u8 mode = cursor.u8("instrument_or_mode", SourceValueDisplay::Hex);
      if (mode < 0xfc || mode == 0xff) {
        const auto bytes = readInstrument(cursor);
        const auto source = reader.range(begin, cursor.nextAddress().value - begin);
        if (mode == 0xff) {
          return event.invoke<&Playback::inlineInstrument>({bytes, source, false});
        }
        return event.invoke<&Playback::writeInstrument>({mode, bytes, source});
      }
      if (mode == 0xfc) {
        return event.invoke<&Playback::instrumentTuning>({cursor.s16le("pitch_delta"), reader.range(begin, 4)});
      }
      const auto srcn = mode == 0xfe ? std::optional<u8>{cursor.u8("srcn")} : std::nullopt;
      const u8 adsr1 = cursor.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = cursor.u8("adsr2", SourceValueDisplay::Hex);
      const u8 gain = cursor.u8("gain", SourceValueDisplay::Hex);
      return event.invoke<&Playback::partialInstrument>(
          {srcn, adsr1, adsr2, gain, reader.range(begin, cursor.nextAddress().value - begin)});
    }
    case 0xfe: {
      // Model only the DA branch condition. Other driver RAM writes are kept
      // as source fields, as are the SFX global-pan controls in FB.
      auto event = cursor.sourceOnly("Write Driver Byte");
      const u8 address = cursor.u8("address", SourceValueDisplay::Address);
      const u8 value = cursor.u8("value", SourceValueDisplay::Hex);
      if (address == 0xdf) {
        return event.invokeFlow([value](Playback& p) {
          p.program.config.condition = value;
          return Effects{};
        });
      }
      return event;
    }
    case 0xff:
      return cursor.command("Retain Voice After Note", SequenceSemantic::State)
          .set<&TrackState::retainVoice>(cursor.u8("off") == 0);
    default:
      return cursor.sourceOnly("Unknown Quest Command").stop();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommandImpl(ByteReader reader, const Layout& layout, u32 begin,
                                                   std::vector<Diagnostic>* diagnostics, u8 forwardingDepth = 0) {
  Cursor cursor(reader, begin, "nin-snes-quest", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  const bool ogreBattle = layout.profile == ProfileId::QuestOgreBattle;
  if ((opcode > 0 && opcode < 0x80) || (!ogreBattle && opcode == 0xde)) {
    auto event = cursor.command("Note Parameters", SequenceSemantic::State);
    const u16 length = opcode == 0xde ? cursor.u16le("length", SourceValueDisplay::Decimal)
                                      : cursor.opcodeValue("length", ogreBattle ? opcode * 2 : shortLength(opcode));
    const auto packed = packedParameter(cursor, ogreBattle);
    u8 velocity = 0;
    if (packed) {
      const u8 index = *packed & 15;
      // Tactics Ogre includes the index, then squares the adjusted byte. Master and channel volume are linear multipliers.
      const u8 raw = static_cast<u8>(layout.volumeTable[index] - index - 1);
      velocity = ogreBattle ? layout.volumeTable[index] : (raw * raw) >> 8;
      cursor.derived("velocity", velocity);
    }
    return event.invoke<&Playback::parameters>({length, packed, velocity});
  }
  if (opcode >= 0x80 && opcode < (ogreBattle ? 0xe0 : 0xd8)) {
    auto event = cursor.command(opcode == 0xc9   ? "Rest"
                                : opcode == 0xc8 ? "Tie"
                                                 : "Note",
                                opcode == 0xc9 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    std::optional<Slide> slide;
    if (!ogreBattle && opcode != 0xc8 && opcode != 0xc9 && cursor.peekU8() == 0xf9) {
      cursor.u8("pitch_slide", SourceValueDisplay::Hex);
      slide = readSlide(cursor);
    }
    const auto ties = ogreBattle ? std::vector<TieSegment>{} : readTies(cursor, reader);
    return event.invoke<&Playback::note>({opcode, ties, slide});
  }

  if (auto command = decodeSharedCommand(cursor, ogreBattle)) {
    return std::move(*command);
  }
  return ogreBattle ? decodeOgreBattleCommand(cursor)
                   : decodeTacticsOgreCommand(cursor, reader, layout, begin, diagnostics, forwardingDepth);
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, const Layout& layout, u32 begin,
                                                   std::vector<Diagnostic>* diagnostics) {
  auto command = decodeCommandImpl(reader, layout, begin, diagnostics);
  if (layout.profile == ProfileId::QuestOgreBattle && command.opcode >= 0xe0 && command.opcode != 0xef) {
    const auto body = std::move(command.execution.body);
    command.execution.body = [body](void* state) {
      static_cast<Playback*>(state)->track.tieEligible = false;
      return body ? body(state) : Effects{};
    };
  }
  return command;
}

}  // namespace

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, SectionPlaylist playlist, AssetId sequenceId,
                             std::optional<SourceAnnotationId> parent, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  Config config{.ogreBattle = layout.profile == ProfileId::QuestOgreBattle,
                .sfx = layout.questSfx != 0,
                .duration = layout.durationRateTable, .pan = layout.questPanTable,
                .conditions = {reader.u8At(layout.questSfx ? 0xa4 + layout.questSfx : 0xa5), reader.u8At(0xa6)},
                .condition = reader.u8At(0xdf)};
  // A manually supplied layout can use the driver's documented defaults.
  if (config.duration.size() != 8) {
    config.duration = config.ogreBattle ? std::vector<u8>{0x32, 0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xff}
                                       : std::vector<u8>{0x23, 0x46, 0x69, 0x8c, 0xaf, 0xd2, 0xf5, 0xff};
  }
  if (config.pan.size() != 42) {
    config.pan = config.ogreBattle
        ? std::vector<u8>{0, 2, 5, 11, 19, 30, 43, 60, 75, 94, 115, 117, 120, 121, 122, 123, 124, 125, 126, 127, 127}
        : std::vector<u8>{0, 8, 17, 26, 35, 44, 55, 67, 80, 95, 104, 110, 114, 117, 119, 121, 123, 124, 125, 126, 127};
    const auto left = config.pan;
    config.pan.insert(config.pan.end(), left.rbegin(), left.rend());
  }
  Layout decodeLayout = layout;
  if (decodeLayout.volumeTable.size() != 16) {
    decodeLayout.volumeTable = config.ogreBattle
        ? std::vector<u8>{0x28, 0x3c, 0x4c, 0x65, 0x72, 0x7f, 0x8c, 0x98, 0xa5, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc}
        : std::vector<u8>{0x19, 0x28, 0x37, 0x46, 0x55, 0x64, 0x73, 0x82, 0x91, 0xa0, 0xaf, 0xbe, 0xcd, 0xdc, 0xeb, 0xff};
  }
  if (layout.instrumentTableAddress) {
    for (u32 i = 0; i < (config.ogreBattle ? 256u : 128u); ++i) {
      const bool builtin = config.ogreBattle && i >= 0xfc;
      // Built-in row selection wraps the low address byte without a carry.
      const u32 address = builtin
          ? (layout.questBuiltinInstruments & 0xff00) | static_cast<u8>(layout.questBuiltinInstruments + (i - 0xfc) * 6)
          : *layout.instrumentTableAddress + i * 6;
      if (reader.has(address, 6)) {
        auto& bytes = config.instruments[i];
        std::ranges::copy(reader.slice(address, 6), bytes.begin());
        config.instrumentSources[i] = reader.range(address, 6);
        if (config.ogreBattle) {
          // Normalize little-endian pitch and banked SRCNs for shared recipes.
          // 80-BF select noise; C0-FF address resident samples in reverse order.
          std::swap(bytes[4], bytes[5]);
          if (bytes[0] >= 0xc0) {
            bytes[0] = (bytes[0] ^ 0xff) + 0x3c;
          } else if (bytes[0] < 0x80) {
            bytes[0] += config.sfx && !builtin ? 0x40 : 0;
            if (bytes[0] >= 0x70) {
              bytes[0] -= 0x70;
            }
          }
        }
      }
    }
  }
  SequenceProgram program = sequenceConfig().makeProgram();
  program.behavior.initialTempoMicrosecondsPerQuarter = tempoUs(config.sfx ? 0x43 : 0x80);
  program.behavior.initialMasterLevel = config.sfx ? (0xe6 / 255.0) * (0xe6 / 255.0) : 0;
  program.behavior.initialPitchBendRangeSemitones = 24;
  if (config.sfx && sourceMap) {
    parent = sourceMap->header("SFX Section", reader.range(layout.playlistAddress, layout.trackCount() * 2))
                 .owner(ObjectRefs::sequence(sequenceId)).id();
  }
  const TrackDecodeScope scope{.reader = reader,
                               .maxCommands = 32768,
                               .sequenceAsset = sequenceId,
                               .parentAnnotation = parent,
                               .sourceMap = sourceMap};
  for (u8 track = 0; track < layout.trackCount(); ++track) {
    std::vector<Address> starts;
    // SFX streams start directly from a single section's track pointers.
    if (config.sfx) {
      const u16 address = reader.le16(layout.playlistAddress + track * 2);
      if (address < 0x100) {
        break;
      }
      starts.push_back(Address{address});
    }
    for (const auto& section : playlist.commands) {
      if (section.kind == PlaylistCommandKind::PlaySection && track < section.streamStarts.size() &&
          section.streamStarts[track]) {
        starts.push_back(*section.streamStarts[track]);
      }
    }
    program.tracks.push_back(scope.decode(
        track, starts, [&](u32 address) { return decodeCommand(reader, decodeLayout, address, diagnostics); }));
  }
  // Playlists contain words: zero ends, 01-FE count additional plays, FF
  // repeats forever (Ogre Battle also repeats FE forever). A repeat's next word
  // is its playlist destination; larger words address sections of eight track
  // pointers. A zero high byte disables a track. All active tracks must finish
  // before advancing to the next section; tracks may also loop independently.
  if (!config.sfx) {
    playlist.waitForAllTracks = true;
    program.sectionPlaylist = std::move(playlist);
  }
  program.runtime = makeCompiledRuntime<Playback, ProgramState>(std::move(config));
  auto recipes = analyzeCompiledProgram<ProgramState>(program, &ProgramState::recipes, diagnostics);
  return SequenceParse{.program = std::move(program), .recipes = std::move(recipes)};
}

}  // namespace vgmtrans::formats::nin_snes::quest
