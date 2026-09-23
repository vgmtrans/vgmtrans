/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnesPlaylist.h"
#include "value/formats/NinSnes/NinSnesQuest.h"
#include "value/formats/NinSnes/NinSnesSequencePrivate.h"

#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nin_snes {

using namespace core;

namespace sequence {

constexpr u32 kMaxTrackCommands = 32768;
constexpr u16 kNoPercussionSourceNote = 0x100;

[[nodiscard]] constexpr u32 drumInstrumentKey(u8 program) {
  return (0x7fu << 7) | program;
}

namespace math {

constexpr std::array<u8, 16> kVolumeEarlier{
    0x08, 0x12, 0x1b, 0x24, 0x2c, 0x35, 0x3e, 0x47, 0x51, 0x5a, 0x62, 0x6b, 0x7d, 0x8f, 0xa1, 0xb3,
};
constexpr std::array<u8, 8> kDurationEarlier{0x33, 0x66, 0x80, 0x99, 0xb3, 0xcc, 0xe6, 0xff};
constexpr std::array<u8, 16> kVolumeStandard{
    0x19, 0x33, 0x4c, 0x66, 0x72, 0x7f, 0x8c, 0x99, 0xa5, 0xb2, 0xbf, 0xcc, 0xd8, 0xe5, 0xf2, 0xfc,
};
constexpr std::array<u8, 8> kDurationStandard{0x33, 0x66, 0x7f, 0x99, 0xb2, 0xcc, 0xe5, 0xfc};
constexpr std::array<u8, 21> kPan{
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
    0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
};
[[nodiscard]] constexpr double vibratoDepthCents(u8 depth) {
  if (depth <= 0xf0) {
    return ((0xffu * depth) >> 8) * (100.0 / 256.0);
  }
  return (0xffu * (depth & 0x0fu)) * (100.0 / 256.0);
}

[[nodiscard]] double tremoloDepthDecibels(BaseProfile base, u8 depth) {
  int trough = 255;
  if (base == BaseProfile::Earlier) {
    // The earlier driver applies two rounded 8-bit multiplies before
    // subtracting the tremolo attenuation from the per-note velocity.
    const int attenuation = (255 * depth) >> 8;
    trough -= (255 * attenuation) >> 8;
  } else {
    // Later N-SPC drivers compensate the falling half of the triangle so its
    // deepest point subtracts the raw depth directly.
    trough = std::max(1, 255 - static_cast<int>(depth));
  }

  // N-SPC squares the combined volume after tremolo. A subtractive bipolar
  // output spans 2D dB, so D is 20*log10(peak/trough).
  return 20.0 * std::log10(255.0 / trough);
}

[[nodiscard]] constexpr u32 tempoMicrosecondsPerQuarter(u8 tempo, u8 timerTarget = kStandardTimerTarget) {
  // Each sequence tick occurs after timerTarget * 125 microseconds. The
  // driver's 8-bit tempo accumulator overflows after 256 / tempo ticks.
  return tempo == 0
             ? 60'000'000
             : static_cast<u32>(std::lround(kPpqn * (125.0 * timerTarget) * 256.0 / tempo));
}

// Convert the driver's 8-bit level control to linear gain using its square law.
// Renderers handle destination encoding and quantization.
[[nodiscard]] constexpr double levelGain(u8 raw) {
  const double normalized = raw / 255.0;
  return normalized * normalized;
}

struct PanGains {
  double left = 1.0;
  double right = 1.0;
};

[[nodiscard]] u8 panTableValue(std::span<const u8> table, u16 pan) {
  if (table.empty()) {
    return 0;
  }
  u8 index = static_cast<u8>(pan >> 8);
  u8 fraction = static_cast<u8>(pan);
  const u8 maximum = static_cast<u8>(table.size() - 1);
  if (index > maximum) {
    index = maximum;
    fraction = 0;
  }
  const u8 current = table[index];
  const u8 next = index < maximum ? table[index + 1] : current;
  return static_cast<u8>(current + (((next - current) * fraction) >> 8));
}

[[nodiscard]] PanGains panGains(const Profile& selected, std::span<const u8> table, u8 rawPan) {
  if (selected.pan == PanModel::ToseLinear) {
    if (rawPan <= 10) {
      return PanGains{
          .left = (255 - 25 * (10 - rawPan)) / 256.0,
          .right = 1.0,
      };
    }
    return PanGains{
        .left = 1.0,
        .right = (255 - 25 * (rawPan - 10)) / 256.0,
    };
  }

  const u8 index = std::min<u8>(rawPan & 0x1f, static_cast<u8>(table.size() - 1));
  const u16 pan = static_cast<u16>(index) << 8;
  const u16 maximum = static_cast<u16>(table.size() - 1) << 8;
  PanGains gains{
      .left = panTableValue(table, pan) / 128.0,
      .right = panTableValue(table, maximum - pan) / 128.0,
  };
  if (selected.pan == PanModel::HalTable) {
    std::swap(gains.left, gains.right);
  }
  return gains;
}

[[nodiscard]] double stereoPosition(PanGains gains) {
  if (gains.left == 0.0 && gains.right == 0.0) {
    return 0.0;
  }
  constexpr double kPiOverTwo = 1.57079632679489661923;
  return std::clamp((std::atan2(gains.right, gains.left) / kPiOverTwo) * 2.0 - 1.0, -1.0, 1.0);
}

}  // namespace math

void loadStandardCommands(std::array<EventType, 256>& events, u8 first) {
  constexpr std::array<EventType, 27> commands{
      EventType::Program,
      EventType::Pan,
      EventType::PanFade,
      EventType::VibratoOn,
      EventType::VibratoOff,
      EventType::MasterVolume,
      EventType::MasterVolumeFade,
      EventType::Tempo,
      EventType::TempoFade,
      EventType::GlobalTranspose,
      EventType::Transpose,
      EventType::TremoloOn,
      EventType::TremoloOff,
      EventType::Volume,
      EventType::VolumeFade,
      EventType::Call,
      EventType::VibratoFade,
      EventType::PitchEnvelopeTo,
      EventType::PitchEnvelopeFrom,
      EventType::PitchEnvelopeOff,
      EventType::Tuning,
      EventType::EchoOn,
      EventType::EchoOff,
      EventType::EchoParameter,
      EventType::EchoVolumeFade,
      EventType::PitchSlide,
      EventType::PercussionBase,
  };
  for (u8 index = 0; index < commands.size(); ++index) {
    events[static_cast<u8>(first + index)] = commands[index];
  }
}

[[nodiscard]] Definition makeDefinition(const Layout& layout) {
  const Profile& selected = profile(layout.profile);
  Definition definition;
  definition.events.fill(EventType::Unknown0);
  definition.volume = layout.volumeTable;
  definition.duration = layout.durationRateTable;
  definition.intelliDuration = layout.intelliDurationRateTable;
  definition.intelliVolume = layout.intelliVolumeTable;

  if (selected.base == BaseProfile::Earlier) {
    definition.status = Status{.noteMin = 0x80, .noteMax = 0xc5, .percussionMin = 0xd0, .percussionMax = 0xd9};
  }
  definition.events[0] = EventType::End;
  const EventType noteParameters = selected.noteParameters == NoteParameterModel::IntelliTable
                                       ? EventType::IntelliNoteParameter
                                   : selected.noteParameters == NoteParameterModel::Lemmings
                                       ? EventType::LemmingsNoteParameter
                                       : EventType::NoteParameter;
  for (u16 opcode = 1; opcode < definition.status.noteMin; ++opcode) {
    definition.events[opcode] = noteParameters;
  }
  for (u16 opcode = definition.status.noteMin; opcode <= definition.status.noteMax; ++opcode) {
    definition.events[opcode] = EventType::Note;
  }
  definition.events[definition.status.noteMax + 1] = EventType::Tie;
  definition.events[definition.status.noteMax + 2] = EventType::Rest;
  for (u16 opcode = definition.status.percussionMin; opcode <= definition.status.percussionMax; ++opcode) {
    definition.events[opcode] = EventType::Percussion;
  }

  if (selected.base == BaseProfile::Earlier) {
    constexpr std::array<EventType, 25> earlier{
        EventType::Program,
        EventType::Pan,
        EventType::PanFade,
        EventType::PitchSlide,
        EventType::VibratoOn,
        EventType::VibratoOff,
        EventType::MasterVolume,
        EventType::MasterVolumeFade,
        EventType::Tempo,
        EventType::TempoFade,
        EventType::GlobalTranspose,
        EventType::TremoloOn,
        EventType::TremoloOff,
        EventType::Volume,
        EventType::VolumeFade,
        EventType::Call,
        EventType::VibratoFade,
        EventType::PitchEnvelopeTo,
        EventType::PitchEnvelopeFrom,
        EventType::Unknown0,
        EventType::Tuning,
        EventType::EchoOn,
        EventType::EchoOff,
        EventType::EchoParameter,
        EventType::EchoVolumeFade,
    };
    for (u8 index = 0; index < earlier.size(); ++index) {
      definition.events[0xda + index] = earlier[index];
    }
    useDefault(definition.volume, math::kVolumeEarlier);
    useDefault(definition.duration, math::kDurationEarlier);
  } else if (selected.intelli != IntelliMode::None) {
    configureIntelligentCommands(definition, selected.intelli);
  } else {
    loadStandardCommands(definition.events, 0xe0);
    useDefault(definition.volume, math::kVolumeStandard);
    useDefault(definition.duration, math::kDurationStandard);
  }

  switch (selected.id) {
    case ProfileId::SunsoftEarlier:
    case ProfileId::Sunsoft:
      definition.events[0xfb] = EventType::ChannelEchoOn;
      definition.events[0xfc] = EventType::ChannelEchoOff;
      definition.events[0xfd] = EventType::Adsr;
      definition.events[0xfe] = selected.id == ProfileId::SunsoftEarlier ? EventType::Nop2 : EventType::VolumeMultiplier;
      break;
    case ProfileId::Rd1:
      definition.events[0xfb] = EventType::Unknown2;
      definition.events[0xfc] = EventType::Unknown0;
      definition.events[0xfd] = EventType::Unknown0;
      definition.events[0xfe] = EventType::Unknown0;
      break;
    case ProfileId::Rd2:
      definition.events[0xfb] = EventType::Rd2ProgramAndAdsr;
      definition.events[0xfd] = EventType::Program;
      break;
    case ProfileId::Konami:
      definition.events[0xe4] = EventType::Unknown2;
      definition.events[0xe5] = EventType::KonamiLoopStart;
      definition.events[0xe6] = EventType::KonamiLoopEnd;
      definition.events[0xe8] = EventType::Nop;
      definition.events[0xe9] = EventType::Nop;
      definition.events[0xf5] = EventType::Unknown0;
      definition.events[0xf6] = EventType::Unknown0;
      definition.events[0xf7] = EventType::Unknown0;
      definition.events[0xf8] = EventType::Unknown0;
      definition.events[0xfa] = EventType::Nop;
      definition.events[0xfb] = EventType::KonamiAdsrGain;
      definition.events[0xfc] = EventType::Nop;
      definition.events[0xfd] = EventType::Nop;
      definition.events[0xfe] = EventType::Nop;
      break;
    case ProfileId::Lemmings:
      definition.events[0xe5] = EventType::Unknown1;
      definition.events[0xe6] = EventType::Unknown2;
      definition.events[0xfb] = EventType::Nop1;
      definition.events[0xfc] = EventType::Unknown0;
      definition.events[0xfd] = EventType::Unknown0;
      definition.events[0xfe] = EventType::Unknown0;
      break;
    case ProfileId::QuintetIog:
    case ProfileId::QuintetTs:
      definition.events[0xf4] = EventType::QuintetTuning;
      definition.events[0xff] = EventType::QuintetAdsr;
      break;
    default:
      break;
  }
  return definition;
}

[[nodiscard]] LfoPerformanceContext tremoloLfoContext() {
  return LfoPerformanceContext{
      .shape = LfoShape{.waveform = LfoWaveform::Triangle},
      // N-SPC starts at nominal gain and initially moves toward attenuation.
      .initialPhaseCycles = 0.25,
      .tremoloGainMode = TremoloGainMode::NoBoost,
  };
}

void EchoState::reset() {
  event = ReverbPerformanceEvent{.voiceMask = 0};
  setVolume(0, 0);
}

ReverbPerformanceEvent EchoState::current() const {
  ReverbPerformanceEvent result = event;
  const double left = gain(leftVolume.currentFixed());
  const double right = gain(rightVolume.currentFixed());
  result.send = std::max(std::abs(left), std::abs(right));
  result.leftGain = left;
  result.rightGain = right;
  return result;
}

void EchoState::set(u8 mask, u8 left, u8 right) {
  event.voiceMask = mask;
  setVolume(left, right);
}

void EchoState::setVolume(u8 left, u8 right) {
  leftVolume.reset(static_cast<s8>(left));
  rightVolume.reset(static_cast<s8>(right));
  lastAdvanceTick.reset();
}

void EchoState::disable() { event.voiceMask = 0; }

void EchoState::channel(u8 bit, bool enabled) {
  event.voiceMask = enabled ? (*event.voiceMask | bit) : (*event.voiceMask & static_cast<u8>(~bit));
}

void EchoState::setParameters(u8 delay, s8 feedback, u8 filter) {
  event.delayMilliseconds = static_cast<double>(delay & 0x0f) * 16.0;
  event.feedback = feedback / 128.0;
  event.filterIndex = filter;
}

bool EchoState::beginFade(u8 length, u8 left, u8 right) {
  if (length == 0) {
    setVolume(left, right);
    return true;
  }
  leftVolume.begin(leftVolume.toRawTarget(static_cast<s8>(left), length));
  rightVolume.begin(rightVolume.toRawTarget(static_cast<s8>(right), length));
  lastAdvanceTick.reset();
  return false;
}

bool EchoState::advanceFade(u64 tick) {
  // Echo is global, but every active track calls this; advance its fade only once per sequence tick.
  if (lastAdvanceTick == tick) {
    return false;
  }
  lastAdvanceTick = tick;
  const bool rightChanged = rightVolume.tick().shouldApply();
  return leftVolume.tick().shouldApply() || rightChanged;
}

double EchoState::gain(s32 fixedVolume) {
  return std::clamp(static_cast<double>(fixedVolume) / (127.0 * 256.0), -1.0, 1.0);
}

ProgramState::ProgramState(const RuntimeConfig& config)
    : selected(profile(config.profile)), tempoTimerTarget(config.tempoTimerTarget),
      fixedPercussionBase(config.fixedPercussionBase), intelligent(config.intelligent),
      baseEnvelopes(config.instrumentEnvelopes) {
  for (u32 encoded = 0; encoded < basePrograms.size(); ++encoded) {
    basePrograms[encoded] = encoded < config.programMap.size() ? config.programMap[encoded] : encoded;
  }
  resetRuntime();
}

void ProgramState::resetRuntime() {
  tempo = kDefaultTempo;
  globalTranspose = 0;
  percussionBase = fixedPercussionBase.value_or(0);
  intelligent.reset();
  programs = basePrograms;
  instrumentEnvelopes = baseEnvelopes;
  tempoState.reset(kDefaultTempo);
  tempoState.clearAutomation();
  tempoAutomationTrack.reset();
  masterVolume = selected.initialMasterVolume;
  volumeMultiplier = 0xff;
  masterVolumeState.reset(masterVolume);
  masterVolumeState.clearAutomation();
  masterVolumeAutomationTrack.reset();
  echo.reset();
}

u32 ProgramState::resolveProgram(u8 encoded, u8 percussionMinimum, u8* logical) const {
  u8 index = encoded;
  if (selected.programs != ProgramResolver::Direct && encoded >= 0x80) {
    index = static_cast<u8>((encoded - percussionMinimum) + percussionBase);
  }
  if (logical != nullptr) {
    // Quintet applies its base/lookup before exposing the logical instrument
    // number. Keep that distinction from the encoded table index: percussion
    // key assignment depends on the resolved logical number, while the
    // program map still needs the encoded index.
    *logical =
        (selected.programs == ProgramResolver::QuintetActRBase || selected.programs == ProgramResolver::QuintetLookup)
            ? static_cast<u8>(basePrograms[index])
            : index;
  }
  return programs[index];
}

u8 ProgramState::commandTempo(u8 encoded) const {
  return static_cast<u8>(encoded * selected.tempoCommandMultiplier);
}

void ProgramState::rememberStandardDrum(u8 logicalProgram, u32 sourceProgram, u8 key, s8 transpose, u16 sourceNote) {
  if (!collecting) {
    return;
  }
  const u8 resolvedSourceNote = sourceNote <= 0xff ? static_cast<u8>(sourceNote) : u8{0x24};
  standardDrums[logicalProgram] = DrumSlot{
      .key = key,
      .sourceProgram = sourceProgram,
      .sourceKey = static_cast<s16>((resolvedSourceNote & 0x7f) + kMelodicKeyCorrection + transpose),
  };
}

void ProgramState::finishPrepass() {
  if (!standardDrums.empty()) {
    DrumKit kit{
        .program = 0,
    };
    for (const auto& [_, slot] : standardDrums) {
      kit.slots.push_back(slot);
    }
    recipes.drumKits.push_back(std::move(kit));
  }
  collecting = false;
  resetRuntime();
}

TrackState::TrackState(TrackStateContext track, const RuntimeConfig& config) : trackNumber(track.sourceTrackNumber) {
  if (const auto initial = config.instrumentEnvelopes.find(0); initial != config.instrumentEnvelopes.end()) {
    envelope = initial->second;
  }
}

void TrackState::beginSection() {
  inPattern = false;
  patternRemaining = 0;
  // Instruments, legato, volume, pan, pitch, and modulation carry across
  // section boundaries; the driver only clears its pattern/fade counters.
}

u8 Playback::soundingDuration() const {
  if (track.legato || (program.selected.id == ProfileId::Konami && track.durationRate == 0)) {
    return track.noteLength;
  }
  const u8 scaled = static_cast<u8>(((track.noteLength * track.durationRate) >> 8) + program.selected.noteGateBias);
  const u8 maximum = std::max<u8>(1, static_cast<u8>(track.noteLength - 2));
  return std::min(std::max<u8>(scaled, 1), maximum);
}

void Playback::updateVoiceHold() {
  const bool held = track.legato || (program.selected.id == ProfileId::Konami && track.durationRate == 0);
  if (held != track.voiceHeld) {
    out.legatoPedal(held);
  }
  track.voiceHeld = held;
}

void Playback::legato(bool enabled) {
  track.legato = enabled;
  updateVoiceHold();
}

void Playback::emitVoiceNote(double key, u32 duration) {
  const bool continuesPreviousVoice = track.voiceHeld && track.lastNote.valid() && track.lastKey.has_value();
  const bool extendsPrevious = continuesPreviousVoice && std::abs(*track.lastKey - key) < 0.0001;
  const PerformanceNoteId note = out.note(NotePerformanceEvent{
      .key = key,
      .linearVelocity = math::levelGain(track.velocity),
      .durationTicks = duration,
      .extendsPrevious = extendsPrevious,
      .restartsLfoPhase = !continuesPreviousVoice,
  });
  if (continuesPreviousVoice && !extendsPrevious) {
    out.pitchSlide(note, *track.lastKey, key, PitchSlideTiming::fromTicks(0))
        .continueFrom(track.lastNote)
        .preferPitchBend();
  }
  track.lastNote = note;
  track.lastKey = key;
  updateVoiceHold();
}

void Playback::standardParameters(u8 duration, bool hasPacked, u8 durationValue, u8 velocityValue) {
  track.noteLength = duration;
  if (hasPacked) {
    track.durationRate = durationValue;
    track.velocity = program.selected.id == ProfileId::Konami
                         ? static_cast<u8>(velocityValue + track.konamiLoop.volumeDelta)
                         : velocityValue;
  }
}

void Playback::lemmingsParameters(u8 duration, bool hasDuration, u8 durationValue, bool hasVelocity, u8 velocityValue) {
  track.noteLength = duration;
  if (hasDuration) {
    track.durationRate = durationValue;
  }
  if (hasVelocity) {
    track.velocity = velocityValue;
  }
}

void Playback::switchToMelodicProgram() {
  if (track.lastWasPercussion) {
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.melodicProgram});
    track.lastWasPercussion = false;
  }
}

void Playback::loadInstrumentEnvelope(u32 sourceProgram) {
  if (const auto envelope = program.instrumentEnvelopes.find(sourceProgram);
      envelope != program.instrumentEnvelopes.end()) {
    track.envelope = envelope->second;
  }
}

void Playback::melodicProgram(u8 encoded, u8 percussionMinimum) {
  track.melodicProgram = program.resolveProgram(encoded, percussionMinimum);
  loadInstrumentEnvelope(track.melodicProgram);
  if (!track.lastWasPercussion) {
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.melodicProgram});
  }
}

void Playback::switchToDrumProgram(u8 drumProgram) {
  if (!track.lastWasPercussion || track.percussionProgram != drumProgram) {
    out.instrument(InstrumentIdentity{
        .domain = std::string(kInstrumentDomain),
        .key = drumInstrumentKey(drumProgram),
    });
    track.percussionProgram = drumProgram;
  }
  track.lastWasPercussion = true;
}

void Playback::emitPitchBend(s16 bend) {
  if (track.pitch.bend && *track.pitch.bend == bend) {
    return;
  }
  track.pitch.bend = bend;
  out.pitchBend((static_cast<double>(bend) / 8192.0) * (track.pitch.rangeCents / 100.0));
}

s16 Playback::currentPitchBend() const {
  if (!track.pitch.baseValid) {
    return 0;
  }
  const double cents = (track.pitch.motion.current() - track.pitch.base) * (100.0 / 256.0);
  return static_cast<s16>(
      std::clamp<s32>(static_cast<s32>(std::lround((cents / track.pitch.rangeCents) * 8192.0)), -8192, 8191));
}

void Playback::applyCurrentPitchBend() { emitPitchBend(currentPitchBend()); }

// Pitch values use 256 units per semitone. Apply their offset from the
// original note to the emitted key, which already includes mapping and transpose.
double Playback::pitchKey(s32 pitch, double noteKey) const {
  return noteKey + static_cast<double>(pitch - track.pitch.base) / 256.0;
}

void Playback::setPitchBendRange(u16 cents) {
  if (cents == 0 || cents == track.pitch.rangeCents) {
    return;
  }
  track.pitch.rangeCents = cents;
  out.pitchBendRange(PitchBendRangePerformanceEvent{.cents = cents});
  if (track.pitch.baseValid) {
    applyCurrentPitchBend();
  }
}

void Playback::resetPitchForNote() {
  track.pitch.transition.interrupt(out);
  track.pitch.motion.clear();
  track.pitch.baseValid = false;
  if (track.pitch.rangeCents == PitchState::kDefaultRangeCents && track.pitch.bend == 0) {
    return;
  }
  if (track.pitchEnvelope.mode != PitchEnvelope::Mode::None && track.pitchEnvelope.length != 0) {
    emitPitchBend(0);
    return;
  }
  setPitchBendRange(PitchState::kDefaultRangeCents);
  emitPitchBend(0);
}

// Start a delayed pitch change for direct channel-pitch-bend output. It moves
// from the current pitch to the target over the requested length, expanding
// the bend range when necessary to avoid clipping.
void Playback::beginPitchBendMotion(u8 delay, u8 length, s32 target) {
  track.pitch.motion.clear();
  if (!track.pitch.baseValid || length == 0) {
    setPitchBendRange(PitchState::kDefaultRangeCents);
    return;
  }

  const s32 current = track.pitch.motion.current();
  const double largestDeviation = std::max(std::abs(static_cast<double>(current - track.pitch.base)),
                                           std::abs(static_cast<double>(target - track.pitch.base)));
  const u16 range =
      std::max<u16>(PitchState::kDefaultRangeCents, static_cast<u16>(std::ceil(largestDeviation * (100.0 / 256.0))));
  setPitchBendRange(range);
  track.pitch.motion.begin(SequenceMotionPlan<s32>::targetOverTicks(target, length, delay));
  applyCurrentPitchBend();
}

void Playback::pitchSlide(u8 delay, u8 length, u8 targetNote) {
  const s32 target = static_cast<s32>(targetNote & 0x7f) * 256;

  // A new F9 stops any F9 slide still in progress.
  track.pitch.transition.interrupt(out);

  // Portamento needs a nonzero length and a note to slide. Use pitch bends otherwise.
  if (!track.pitch.baseValid || length == 0 || !track.lastNote.valid() || !track.lastKey) {
    beginPitchBendMotion(delay, length, target);
    return;
  }

  // Stop the previous calculation without resetting its current pitch, so
  // the replacement starts from the value already reached.
  track.pitch.motion.clear();
  const s32 current = track.pitch.motion.current();
  track.pitch.motion.begin(SequenceMotionPlan<s32>::targetOverTicks(target, length, delay));

  // Calculate intermediate pitches with N-SPC integer math.
  // advancePitchMotion() records each value on the slide created below.
  track.pitch.transitionNoteKey = *track.lastKey;
  track.pitch.transition =
      out.at(vm.tick() + delay)
          .pitchSlide(track.lastNote, pitchKey(current, *track.lastKey), pitchKey(target, *track.lastKey), length);
}

void Playback::beginNotePitch(u8 rawNote) {
  resetPitchForNote();
  track.pitch.baseValid = true;
  track.pitch.base = static_cast<s32>(rawNote & 0x7f) * 256;
  track.pitch.motion.reset(track.pitch.base);

  if (track.pitchEnvelope.mode != PitchEnvelope::Mode::None && track.pitchEnvelope.length != 0) {
    const s32 offset = static_cast<s32>(track.pitchEnvelope.semitones) * 256;
    s32 target = track.pitch.base;
    if (track.pitchEnvelope.mode == PitchEnvelope::Mode::To) {
      target += offset;
    } else {
      track.pitch.motion.reset(track.pitch.base - offset);
    }
    beginPitchBendMotion(track.pitchEnvelope.delay, track.pitchEnvelope.length, target);
  }
  beginNoteVibrato();
}

bool Playback::pitchMotionIdle() const { return !track.pitch.motion.active(); }

void Playback::advancePitchMotion() {
  const auto tick = track.pitch.motion.tick();
  if (tick.status == SequenceMotionStatus::Inactive || tick.status == SequenceMotionStatus::Delayed) {
    return;
  }
  if (track.pitch.transition.valid()) {
    track.pitch.transition.sample(out, pitchKey(tick.current, track.pitch.transitionNoteKey));
    track.pitch.bend.reset();
    if (tick.status == SequenceMotionStatus::Finished) {
      track.pitch.transition.clear();
    }
  } else {
    applyCurrentPitchBend();
  }
}

Effects Playback::note(u8 noteIndex) {
  switchToMelodicProgram();
  const double key =
      kMelodicKeyCorrection + noteIndex + track.transpose + static_cast<double>(track.konamiLoop.pitchDelta) / 256.0;
  if (program.collecting) {
    program.recipes.usedNotes.emplace(
        track.melodicProgram, static_cast<u8>(std::clamp(std::lround(key + program.globalTranspose), 0l, 127l)));
  }
  beginNotePitch(noteIndex);
  emitVoiceNote(key, soundingDuration() + (track.legato ? 1u : 0u));
  return Effects::wait(track.noteLength);
}

Effects Playback::percussion(u8 slot, u8 percussionMinimum, bool intelli, u16 sourceNote) {
  if (intelli) {
    return intelligentPercussion(slot, percussionMinimum);
  }
  const u8 duration = soundingDuration();
  const bool earlier = program.selected.base == BaseProfile::Earlier;
  u8 logical = earlier ? slot : 0;
  const u32 sourceProgram =
      earlier ? kEarlierPercussionProgramBase + slot
              : program.resolveProgram(static_cast<u8>(slot + program.percussionBase), percussionMinimum, &logical);
  const u8 key = static_cast<u8>(0x24 + logical - program.percussionBase);
  program.rememberStandardDrum(logical, sourceProgram, key, program.globalTranspose, sourceNote);
  switchToDrumProgram(0);
  if (isSunsoft(program.selected.id)) {
    // Every percussion note runs the instrument loader, even when the
    // exported drum-kit program stays the same. It replaces any FD ADSR
    // override and supplies the GAIN register for a subsequent FD.
    loadInstrumentEnvelope(sourceProgram);
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }
  const double outputKey = key - program.globalTranspose + static_cast<double>(track.konamiLoop.pitchDelta) / 256.0;
  beginNotePitch(static_cast<u8>(key - program.globalTranspose));
  emitVoiceNote(outputKey, duration);
  return Effects::wait(track.noteLength);
}

Effects Playback::tie() {
  if (track.lastKey) {
    track.lastNote = out.note(*track.lastKey, math::levelGain(track.velocity), soundingDuration(), true);
  }
  updateVoiceHold();
  return Effects::wait(track.noteLength);
}

Effects Playback::rest() {
  if (track.voiceHeld && track.lastNote.valid()) {
    out.setPreviousNoteEnd(vm.tick() + soundingDuration());
  }
  updateVoiceHold();
  if (!track.voiceHeld) {
    // A gated rest ends the preceding note chain; a later tie cannot reach
    // back across that silence.
    track.lastNote = {};
    track.lastKey.reset();
  }
  return Effects::wait(track.noteLength);
}

void Playback::beginPattern(u8 times, Address destination) {
  track.inPattern = true;
  track.patternRemaining = times;
  track.patternStart = destination;
}

Effects Playback::endOrReturn() {
  if (!track.inPattern) {
    return vm.endSection();
  }
  if (track.patternRemaining > 1) {
    --track.patternRemaining;
    return vm.jump(track.patternStart);
  }
  track.inPattern = false;
  track.patternRemaining = 0;
  return vm.return_();
}

void Playback::emitPan(PerformanceEmitter output, u8 value) const {
  const auto gains = math::panGains(program.selected, math::kPan, value);
  output.stereoBalance(gains.left, gains.right);
}

void Playback::pan(u8 value) {
  track.pan.setCurrentAt(vm.tick(), value);
  emitPan(out, value);
}

void Playback::panFade(u8 length, u8 value) {
  if (length == 0) {
    pan(value);
    return;
  }
  // Interpolate the source pan index before applying its non-linear table.
  const auto gains = math::panGains(program.selected, math::kPan, value);
  track.pan.begin(out.fade(PerformanceAutomationTarget::Pan, math::stereoPosition(gains), length),
                  track.pan.toRawTarget(value, length));
}

void Playback::vibratoOn(u8 delay, u8 rate, u8 depth) {
  track.vibrato = VibratoConfig{.delay = delay, .rate = rate, .depth = depth};
  track.vibratoDepth.interruptFadeAutomationAt(vm.tick());
  track.vibratoDepth.resetDepth(depth);
  emitConfiguredVibrato();
}

void Playback::vibratoOff() {
  track.vibrato = {};
  track.vibratoDepth.interruptFadeAutomationAt(vm.tick());
  track.vibratoDepth.resetDepth(0);
  emitConfiguredVibrato();
}

void Playback::konamiEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  out.replaceEnvelope(snesDspEnvelope(adsr1, adsr2, gain), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
}

void Playback::vibratoFade(u8 length) { track.vibrato.fade = length; }

void Playback::emitVibratoDepth(u8 rawDepth, PerformanceEmitter output) {
  const double depthSemitones = math::vibratoDepthCents(rawDepth) / 100.0;
  track.vibratoDepth.emitPhysicalDepth(depthSemitones, [&](double value) { output.vibratoDepth(value); });
}

void Playback::emitVibratoRateAndDelay() {
  const bool active = track.vibrato.active();
  if (active) {
    out.vibratoRateCyclesPerTick(static_cast<double>(track.vibrato.rate) / 256.0);
    out.vibratoDelayTicks(track.vibrato.delay);
  } else {
    out.vibratoRate(0.0);
    out.vibratoDelayTicks(0);
  }
}

void Playback::emitConfiguredVibrato() {
  emitVibratoDepth(track.vibrato.active() ? track.vibrato.depth : 0, out);
  emitVibratoRateAndDelay();
}

void Playback::beginNoteVibrato() {
  if (!track.vibrato.active() || track.vibrato.fade == 0) {
    return;
  }
  track.vibratoDepth.configureLinearFade(track.vibrato.fade);
  static_cast<void>(track.vibratoDepth.restartFade(track.vibrato.delay));
  track.vibratoDepth.bindFade(out.noteEnvelope(PerformanceAutomationTarget::VibratoDepth,
                                               math::vibratoDepthCents(track.vibrato.depth) / 100.0,
                                               track.vibrato.fade, track.vibrato.delay));
  emitVibratoDepth(0, track.vibratoDepth.fadeOutput(out));
}

void Playback::tremoloOn(u8 delay, u8 rate, u8 depth) {
  const LfoPerformanceContext context = tremoloLfoContext();
  out.tremoloDepth(math::tremoloDepthDecibels(program.selected.base, depth), context);
  out.tremoloRateCyclesPerTick(static_cast<double>(rate) / 256.0, context);
  out.tremoloDelayTicks(delay);
}

void Playback::tremoloOff() { out.tremoloDepth(0.0, tremoloLfoContext()); }

void Playback::tempo(u8 value) {
  const u8 driverTempo = program.commandTempo(value);
  program.tempoState.setCurrentAt(vm.tick(), driverTempo);
  program.tempoAutomationTrack.reset();
  program.tempo = driverTempo;
  out.tempo(math::tempoMicrosecondsPerQuarter(driverTempo, program.tempoTimerTarget));
}

void Playback::tempoFade(u8 length, u8 value) {
  if (length == 0) {
    tempo(value);
    return;
  }
  const u8 driverTempo = program.commandTempo(value);
  program.tempoState.reset(program.tempo);
  program.tempoState.begin(
      out.fade(PerformanceAutomationTarget::Tempo,
               static_cast<double>(math::tempoMicrosecondsPerQuarter(driverTempo, program.tempoTimerTarget)), length),
      program.tempoState.toRawTarget(driverTempo, length));
  program.tempoAutomationTrack = track.trackNumber;
  advanceTempoFade();
}

void Playback::volume(u8 value) {
  track.volume.setCurrentAt(vm.tick(), value);
  out.level(math::levelGain(value), ValueQuantization{.levels = 256});
}

void Playback::volumeFade(u8 length, u8 value) {
  if (length == 0) {
    volume(value);
    return;
  }
  track.volume.begin(out.fade(PerformanceAutomationTarget::Level, math::levelGain(value), length),
                     track.volume.toRawTarget(value, length));
}

double Playback::masterGain(u8 value) const {
  // Albert applies FE before squaring; multiplying the squared gains is equivalent.
  return math::levelGain(value) * math::levelGain(program.volumeMultiplier);
}

void Playback::volumeMultiplier(u8 value) {
  // The combined output changes immediately, but E6's source-domain fade
  // keeps running. End its old output binding without clearing that motion.
  program.masterVolumeState.interruptAutomationAt(vm.tick());
  program.volumeMultiplier = value;
  out.masterLevel(masterGain(program.masterVolume));
}

void Playback::masterVolume(u8 value) {
  program.masterVolume = value;
  program.masterVolumeState.setCurrentAt(vm.tick(), value);
  program.masterVolumeAutomationTrack.reset();
  out.masterLevel(masterGain(value));
}

void Playback::masterVolumeFade(u8 length, u8 value) {
  if (length == 0) {
    masterVolume(value);
    return;
  }
  program.masterVolumeState.reset(program.masterVolume);
  program.masterVolumeState.begin(out.fade(PerformanceAutomationTarget::MasterLevel, masterGain(value), length),
                                  program.masterVolumeState.toRawTarget(value, length));
  program.masterVolumeAutomationTrack = track.trackNumber;
}

void Playback::advanceTempoFade() {
  program.tempoState.tickRaw([&](s32 raw) {
    const u8 value = static_cast<u8>(std::clamp<s32>(raw, 0, 0xff));
    program.tempo = value;
    program.tempoState.output(out).tempo(math::tempoMicrosecondsPerQuarter(value, program.tempoTimerTarget));
  });
  if (!program.tempoState.active()) {
    program.tempoAutomationTrack.reset();
  }
}

void Playback::advanceVibratoFade() {
  const auto tick = track.vibratoDepth.tickFade();
  if (!tick.shouldApply()) {
    return;
  }
  emitVibratoDepth(static_cast<u8>(track.vibratoDepth.currentDepth()), track.vibratoDepth.fadeOutput(out));
}

void Playback::advancePanFade() {
  track.pan.tickRaw(
      [&](s32 value) { emitPan(track.pan.output(out), static_cast<u8>(std::clamp<s32>(value, 0, 0xff))); });
}

void Playback::advanceVolumeFade() {
  track.volume.tickRaw([&](s32 value) {
    track.volume.output(out).level(math::levelGain(static_cast<u8>(std::clamp<s32>(value, 0, 0xff))),
                                   ValueQuantization{.levels = 256});
  });
}

void Playback::advanceMasterFade() {
  program.masterVolumeState.tickRaw([&](s32 value) {
    program.masterVolume = static_cast<u8>(std::clamp<s32>(value, 0, 0xff));
    program.masterVolumeState.output(out).masterLevel(masterGain(program.masterVolume));
  });
  if (!program.masterVolumeState.active()) {
    program.masterVolumeAutomationTrack.reset();
  }
}

void Playback::tick() {
  advanceVolumeFade();
  advancePanFade();
  advanceVibratoFade();
  advancePitchMotion();
  if (program.tempoAutomationTrack == track.trackNumber) {
    advanceTempoFade();
  }
  if (program.masterVolumeAutomationTrack == track.trackNumber) {
    advanceMasterFade();
  }
  if (program.echo.advanceFade(vm.tick())) {
    out.reverb(program.echo.current());
  }
}

void Playback::globalTranspose(s8 semitones) {
  program.globalTranspose = semitones;
  out.globalTranspose(semitones);
}

void Playback::echo(u8 channels, u8 volumeLeft, u8 volumeRight) {
  program.echo.set(channels, volumeLeft, volumeRight);
  out.reverb(program.echo.current());
}

void Playback::channelEcho(bool enabled) {
  program.echo.channel(static_cast<u8>(1u << track.trackNumber), enabled);
  out.reverb(program.echo.current());
}

void Playback::echoOff() {
  if (isSunsoft(program.selected.id)) {
    // Sunsoft F6 zeros EVOL but retains the channel mask.
    program.echo.setVolume(0, 0);
  } else {
    program.echo.disable();
  }
  out.reverb(program.echo.current());
}

void Playback::echoParameters(u8 delay, s8 feedback, u8 filter) {
  program.echo.setParameters(delay, feedback, filter);
  out.reverb(program.echo.current());
}

void Playback::echoVolumeFade(u8 length, u8 volumeLeft, u8 volumeRight) {
  if (program.echo.beginFade(length, volumeLeft, volumeRight)) {
    out.reverb(program.echo.current());
  }
}

void Playback::percussionBase(u8 base) {
  if (!program.fixedPercussionBase) {
    program.percussionBase = base;
  }
  if (program.selected.intelli == IntelliMode::Ta) {
    program.intelligent.flags &= static_cast<u8>(~0x40);
  }
}

void Playback::pitchEnvelope(PitchEnvelope::Mode mode, u8 delay, u8 length, s8 semitones) {
  track.pitchEnvelope = PitchEnvelope{mode, delay, length, semitones};
}

void Playback::beginKonamiLoop(Address start) { track.konamiLoop.start = start; }

Effects Playback::konamiLoop(u8 times, s8 volumeDelta, s8 pitchDelta, Address destination) {
  RepeatCounter counter = vm.repeatCounter(0);
  if (counter.firstVisit()) {
    counter.start(times == 0 ? 256 : times);
  }

  if (counter.consumeReplay()) {
    // The driver accumulates these operands only for another pass. Volume is
    // an eight-bit add applied when the packed note parameters are read;
    // pitch uses signed 16-bit units with 1/256 semitone resolution.
    track.konamiLoop.volumeDelta = static_cast<u8>(track.konamiLoop.volumeDelta + static_cast<u8>(volumeDelta));
    const u16 pitchBits =
        static_cast<u16>(track.konamiLoop.pitchDelta) + static_cast<u16>(static_cast<s16>(pitchDelta) * 16);
    track.konamiLoop.pitchDelta =
        pitchBits < 0x8000 ? static_cast<s16>(pitchBits) : static_cast<s16>(static_cast<s32>(pitchBits) - 0x10000);
    return vm.jump(destination);
  }

  counter.finish();
  track.konamiLoop.volumeDelta = 0;
  track.konamiLoop.pitchDelta = 0;
  return Effects{};
}

void Playback::adsr(u8 adsr1, u8 adsr2) {
  track.envelope.adsr1 = adsr1;
  track.envelope.adsr2 = adsr2;
  // Rate-based GAIN depends on the live ENVX value. Preserve the registers
  // but emit only envelopes that the static model can describe faithfully.
  if ((adsr1 & 0x80) != 0 || (track.envelope.gain & 0x80) == 0) {
    out.replaceEnvelope(snesDspEnvelope(adsr1, adsr2, track.envelope.gain),
                        VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }
}

[[nodiscard]] DecodedBytecodeCommand unknownCommand(Cursor& cursor, u8 arguments) {
  auto event = cursor.sourceOnly("Unknown Event", "unknown");
  for (u8 index = 0; index < arguments; ++index) {
    cursor.u8(fmt::format("arg{}", index + 1), SourceValueDisplay::Hex);
  }
  return event;
}

[[nodiscard]] DecodedBytecodeCommand decodeNoteParameters(Cursor& cursor, const DecodeContext& context, EventType type) {
  auto event = cursor.command("Note Parameters", SequenceSemantic::State);
  const u8 duration = cursor.opcodeValue("duration", cursor.opcode(), SourceValueDisplay::Decimal);

  if (type == EventType::LemmingsNoteParameter) {
    bool hasDuration = false;
    bool hasVelocity = false;
    u8 durationRate = 0;
    u8 velocity = 0;
    if (cursor.peekU8() <= 0x7f) {
      hasDuration = true;
      const u8 raw = cursor.u8("duration_rate");
      durationRate = static_cast<u8>((raw << 1) + (raw >> 1) + (raw & 1));
      cursor.derived("resolved_duration_rate", durationRate);
      if (cursor.peekU8() <= 0x7f) {
        hasVelocity = true;
        velocity = static_cast<u8>(cursor.u8("velocity") << 1);
        cursor.derived("resolved_velocity", velocity);
      }
    }
    return event.invoke<&Playback::lemmingsParameters>({duration, hasDuration, durationRate, hasVelocity, velocity});
  }

  bool present = false;
  u8 durationRate = 0;
  u8 velocity = 0;
  if (cursor.peekU8() <= 0x7f) {
    present = true;
    const u8 packed = cursor.u8("quantize_velocity", SourceValueDisplay::Hex);
    durationRate = context.definition.duration[(packed >> 4) & 7];
    velocity = context.definition.volume[packed & 15];
    cursor.derived("duration_rate", durationRate);
    cursor.derived("velocity", velocity);
  }
  return event.invoke<&Playback::standardParameters>({duration, present, durationRate, velocity});
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(const DecodeContext& context, u32 begin) {
  Cursor cursor(context.reader, begin, "nin-snes", context.diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 opcode = cursor.opcode();
  const EventType type = context.definition.events[opcode];
  if (type == EventType::IntelliNoteParameter) {
    return decodeIntelligentNoteParameters(cursor, context, begin);
  }
  if (type == EventType::NoteParameter || type == EventType::LemmingsNoteParameter) {
    return decodeNoteParameters(cursor, context, type);
  }
  if (context.selected.intelli != IntelliMode::None) {
    if (auto command = decodeIntelligentCommand(cursor, context, type)) {
      return std::move(*command);
    }
  }

  switch (type) {
    case EventType::End: {
      auto event = cursor.command("Section End / Pattern Return", SequenceSemantic::End);
      event.invoke<&Playback::endOrReturn>();
      return event.return_();
    }
    case EventType::Note: {
      auto event = cursor.command("Note", SequenceSemantic::Note);
      const u8 key = cursor.opcodeValue("key", static_cast<u8>(opcode - context.definition.status.noteMin),
                                        SourceValueDisplay::MidiNote);
      return event.invoke<&Playback::note>({key});
    }
    case EventType::Tie:
      return cursor.command("Tie", SequenceSemantic::Note).invoke<&Playback::tie>();
    case EventType::Rest:
      return cursor.command("Rest", SequenceSemantic::Rest).invoke<&Playback::rest>();
    case EventType::Percussion: {
      auto event = cursor.command("Percussion Note", SequenceSemantic::Note);
      const u8 slot = cursor.opcodeValue("slot", static_cast<u8>(opcode - context.definition.status.percussionMin),
                                         SourceValueDisplay::Decimal);
      const bool intelli = context.selected.intelli != IntelliMode::None && slot < kIntelliDrumSlots;
      u16 sourceNote = kNoPercussionSourceNote;
      if (context.layout.percussionTableAddress) {
        const u32 address = *context.layout.percussionTableAddress + slot * 6;
        if (context.reader.has(address, 6)) {
          sourceNote = context.reader.u8At(address + 5);
        }
      } else if (context.layout.konamiPercussion && slot < context.layout.konamiPercussion->slotCount) {
        const u32 address = context.layout.konamiPercussion->tableAddress + slot * 3;
        if (context.reader.has(address, 3)) {
          sourceNote = context.reader.u8At(address + 2);
        }
      }
      return event.invoke<&Playback::percussion>({slot, context.definition.status.percussionMin, intelli, sourceNote});
    }
    case EventType::Program:
    case EventType::Rd2ProgramAndAdsr: {
      auto event =
          cursor.command(type == EventType::Program ? "Program" : "Program And ADSR", SequenceSemantic::Program);
      const u8 program = cursor.u8("program", SemanticOperandRole::Instrument);
      if (type == EventType::Rd2ProgramAndAdsr) {
        cursor.u8("adsr1", SourceValueDisplay::Hex);
        cursor.u8("adsr2", SourceValueDisplay::Hex);
      }
      return event.invoke<&Playback::melodicProgram>({program, context.definition.status.percussionMin});
    }
    case EventType::Call: {
      auto event = cursor.command("Pattern Play", SequenceSemantic::Call);
      const u16 stored = cursor.u16le("stored_destination", SourceValueDisplay::Address);
      const Address destination = context.address(stored);
      cursor.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::CallTarget);
      const u8 times = cursor.u8("times");
      event.invoke<&Playback::beginPattern>({times, destination});
      return event.call(destination);
    }
    case EventType::Pan:
      return cursor.command("Pan", SequenceSemantic::Pan).invoke<&Playback::pan>({cursor.u8("pan")});
    case EventType::PanFade:
      return cursor.command("Pan Fade", SequenceSemantic::Pan)
          .invoke<&Playback::panFade>({cursor.u8("length"), cursor.u8("pan")});
    case EventType::VibratoOn:
      return cursor.command("Vibrato", SequenceSemantic::Modulation)
          .invoke<&Playback::vibratoOn>({cursor.u8("delay"), cursor.u8("rate"), cursor.u8("depth")});
    case EventType::VibratoOff:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case EventType::MasterVolume:
      return cursor.command("Master Volume", SequenceSemantic::Level)
          .invoke<&Playback::masterVolume>({cursor.u8("volume")});
    case EventType::VolumeMultiplier:
      return cursor.command("Volume Multiplier", SequenceSemantic::Level)
          .invoke<&Playback::volumeMultiplier>({cursor.u8("volume")});
    case EventType::MasterVolumeFade:
      return cursor.command("Master Volume Fade", SequenceSemantic::Level)
          .invoke<&Playback::masterVolumeFade>({cursor.u8("length"), cursor.u8("volume")});
    case EventType::Tempo:
      return cursor.command("Tempo", SequenceSemantic::Tempo).invoke<&Playback::tempo>({cursor.u8("tempo")});
    case EventType::TempoFade:
      return cursor.command("Tempo Fade", SequenceSemantic::Tempo)
          .invoke<&Playback::tempoFade>({cursor.u8("length"), cursor.u8("tempo")});
    case EventType::GlobalTranspose:
      return cursor.command("Global Transpose", SequenceSemantic::Pitch)
          .invoke<&Playback::globalTranspose>({cursor.s8("semitones", SourceValueDisplay::SignedDecimal)});
    case EventType::Transpose:
      return cursor.command("Transpose", SequenceSemantic::Pitch)
          .set<&TrackState::transpose>(cursor.s8("semitones", SourceValueDisplay::SignedDecimal));
    case EventType::TremoloOn:
      return cursor.command("Tremolo On", SequenceSemantic::Modulation)
          .invoke<&Playback::tremoloOn>({cursor.u8("delay"), cursor.u8("rate"), cursor.u8("depth")});
    case EventType::TremoloOff:
      return cursor.command("Tremolo Off", SequenceSemantic::Modulation).invoke<&Playback::tremoloOff>();
    case EventType::Volume:
      return cursor.command("Volume", SequenceSemantic::Level).invoke<&Playback::volume>({cursor.u8("volume")});
    case EventType::VolumeFade:
      return cursor.command("Volume Fade", SequenceSemantic::Level)
          .invoke<&Playback::volumeFade>({cursor.u8("length"), cursor.u8("volume")});
    case EventType::VibratoFade:
      return cursor.command("Vibrato Fade", SequenceSemantic::Modulation)
          .invoke<&Playback::vibratoFade>({cursor.u8("length")});
    case EventType::PitchEnvelopeTo:
    case EventType::PitchEnvelopeFrom: {
      auto event = cursor.command(type == EventType::PitchEnvelopeTo ? "Pitch Envelope To" : "Pitch Envelope From",
                                  SequenceSemantic::Pitch);
      const u8 delay = cursor.u8("delay");
      const u8 length = cursor.u8("length");
      const s8 semitones = cursor.s8("semitones", SourceValueDisplay::SignedDecimal);
      return event.invoke<&Playback::pitchEnvelope>(
          {type == EventType::PitchEnvelopeTo ? PitchEnvelope::Mode::To : PitchEnvelope::Mode::From, delay, length,
           semitones});
    }
    case EventType::PitchEnvelopeOff:
      return cursor.command("Pitch Envelope Off", SequenceSemantic::Pitch)
          .set<&TrackState::pitchEnvelope>(PitchEnvelope{});
    case EventType::Tuning: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      const u8 tuning = cursor.u8("tuning");
      return event.emitTuning((tuning / 256.0) * 100.0);
    }
    case EventType::EchoOn:
      return cursor.command("Echo", SequenceSemantic::State)
          .invoke<&Playback::echo>(
              {cursor.u8("channels", SourceValueDisplay::Hex), cursor.u8("volume_left"), cursor.u8("volume_right")});
    case EventType::EchoOff:
      return cursor.command("Echo Off", SequenceSemantic::State).invoke<&Playback::echoOff>();
    case EventType::EchoParameter:
      return cursor.command("Echo Parameters", SequenceSemantic::State)
          .invoke<&Playback::echoParameters>(
              {cursor.u8("delay"), cursor.s8("feedback", SourceValueDisplay::SignedDecimal), cursor.u8("fir")});
    case EventType::EchoVolumeFade:
      return cursor.command("Echo Volume Fade", SequenceSemantic::State)
          .invoke<&Playback::echoVolumeFade>(
              {cursor.u8("length"), cursor.u8("volume_left"), cursor.u8("volume_right")});
    case EventType::PitchSlide: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Pitch);
      const u8 delay = cursor.u8("delay");
      const u8 length = cursor.u8("length");
      const u8 target = cursor.u8("target_note", SourceValueDisplay::MidiNote);
      // During a preceding wait, execute F9 early only after the current pitch change ends.
      return event.invoke<&Playback::pitchSlide>({delay, length, target}).duringWaitWhen<&Playback::pitchMotionIdle>();
    }
    case EventType::PercussionBase:
      return cursor.command("Percussion Base", SequenceSemantic::State)
          .invoke<&Playback::percussionBase>({cursor.u8("program", SemanticOperandRole::InstrumentProgram)});
    case EventType::KonamiLoopStart:
      return cursor.command("Loop Start", SequenceSemantic::Loop)
          .invoke<&Playback::beginKonamiLoop>({cursor.derived(
              "destination", cursor.nextAddress(), SourceValueDisplay::Address, SemanticOperandRole::LoopTarget)});
    case EventType::KonamiLoopEnd: {
      auto event = cursor.command("Loop End", SequenceSemantic::Repeat);
      const u8 times = cursor.u8("times");
      const s8 volumeDelta = cursor.s8("volume_delta", SourceValueDisplay::SignedDecimal);
      const s8 pitchDelta = cursor.s8("pitch_delta", SourceValueDisplay::SignedDecimal);
      event.invokeFlow([times, volumeDelta, pitchDelta](Playback& playback) {
        return playback.konamiLoop(times, volumeDelta, pitchDelta, playback.track.konamiLoop.start);
      });
      return event;
    }
    case EventType::KonamiAdsrGain: {
      auto event = cursor.command("ADSR / GAIN", SequenceSemantic::Envelope);
      const u8 attackDecay = cursor.u8("attack_decay_parameter", SourceValueDisplay::Hex);
      const u8 sustain = cursor.u8("sustain_parameter", SourceValueDisplay::Hex);
      const u8 gain = cursor.u8("gain", SourceValueDisplay::Hex);
      const u8 adsr1 = cursor.derived("dsp_adsr1", snesDspKonamiAdsr1(attackDecay), SourceValueDisplay::Hex);
      const u8 adsr2 = cursor.derived("dsp_adsr2", snesDspKonamiAdsr2(sustain), SourceValueDisplay::Hex);
      return event.invoke<&Playback::konamiEnvelope>({adsr1, adsr2, gain});
    }
    case EventType::QuintetTuning: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      const u8 tuning = cursor.u8("tuning");
      return event.emitTuning((tuning / 256.0) * 61.8);
    }
    case EventType::QuintetAdsr: {
      auto event = cursor.sourceOnly("ADSR");
      cursor.u8("adsr1", SourceValueDisplay::Hex);
      cursor.u8("sustain_rate");
      cursor.u8("sustain_level");
      return event;
    }
    case EventType::ChannelEchoOn:
      return cursor.command("Echo On", SequenceSemantic::State).invoke<&Playback::channelEcho>({true});
    case EventType::ChannelEchoOff:
      return cursor.command("Echo Off", SequenceSemantic::State).invoke<&Playback::channelEcho>({false});
    case EventType::Adsr:
      return cursor.command("ADSR", SequenceSemantic::State)
          .invoke<&Playback::adsr>(
              {cursor.u8("adsr1", SourceValueDisplay::Hex), cursor.u8("adsr2", SourceValueDisplay::Hex)});
    case EventType::Nop:
      return cursor.sourceOnly("NOP");
    case EventType::Nop1:
    case EventType::Nop2: {
      auto event = cursor.sourceOnly("NOP");
      cursor.u8("argument", SourceValueDisplay::Hex);
      if (type == EventType::Nop2) {
        cursor.u8("argument_2", SourceValueDisplay::Hex);
      }
      return event;
    }
    case EventType::Unknown1:
      return unknownCommand(cursor, 1);
    case EventType::Unknown2:
      return unknownCommand(cursor, 2);
    case EventType::Unknown3:
      return unknownCommand(cursor, 3);
    case EventType::Unknown4:
      return unknownCommand(cursor, 4);
    case EventType::Unknown0:
    default:
      return unknownCommand(cursor, 0);
  }
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, u32 trackNumber, const std::vector<Address>& starts,
                                       const DecodeContext& context, AssetId sequenceId,
                                       std::optional<SourceAnnotationId> parent, SourceMapBuilder* sourceMap) {
  // A channel can begin at a different address in every section. Discover all
  // roots into one immutable program, then the playlist selects the right root
  // each time that section starts.
  const TrackDecodeScope scope{
      .reader = reader,
      .maxCommands = kMaxTrackCommands,
      .sequenceAsset = sequenceId,
      .parentAnnotation = parent,
      .sourceMap = sourceMap,
  };
  return scope.decode(trackNumber, starts, [&](u32 address) { return decodeCommand(context, address); });
}

[[nodiscard]] std::vector<u8> buildProgramMap(ByteReader reader, const Layout& layout) {
  const Profile& selected = profile(layout.profile);
  std::vector<u8> map(256);
  for (u16 sourceProgram = 0; sourceProgram < map.size(); ++sourceProgram) {
    u8 resolved = static_cast<u8>(sourceProgram);
    if (selected.programs == ProgramResolver::QuintetActRBase) {
      resolved = static_cast<u8>(resolved + layout.quintetBgmInstrumentBase);
    } else if (selected.programs == ProgramResolver::QuintetLookup) {
      const u32 address = layout.quintetInstrumentLookupAddress + resolved;
      if (reader.has(address, 1)) {
        resolved = reader.u8At(address);
      }
    }
    map[sourceProgram] = resolved;
  }
  return map;
}

[[nodiscard]] SequenceProgramConfig makeSequenceConfig() {
  return SequenceProgramConfig{
      .commandKindPrefix = "nin-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
              .panLaw = PanLaw::ConstantSum,
              .initialReverbSend = 0.0,
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(kDefaultTempo),
          },
  };
}

}  // namespace sequence

using namespace sequence;

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config = makeSequenceConfig();
  return config;
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const Profile& selected = profile(layout.profile);
  PlaylistDecode playlist =
      layout.questSfx ? PlaylistDecode{} : decodePlaylist(reader, layout, sequenceId, sourceMap, diagnostics);
  if (selected.id == ProfileId::QuestTacticsOgre || selected.id == ProfileId::QuestOgreBattle) {
    return quest::decodeSequence(reader, layout, std::move(playlist.playlist), sequenceId, playlist.annotation,
                                 sourceMap, diagnostics);
  }
  const Definition definition = makeDefinition(layout);

  SequenceProgram program = sequenceConfig().makeProgram();
  RuntimeConfig runtime{
      .profile = layout.profile,
      .tempoTimerTarget = layout.tempoTimerTarget,
      .fixedPercussionBase = layout.fixedPercussionBase,
  };
  runtime.programMap = buildProgramMap(reader, layout);
  runtime.intelligent = readIntelligentConfig(reader, layout);
  if ((selected.intelli != IntelliMode::None || isSunsoft(selected.id)) && layout.instrumentTableAddress) {
    for (u32 index = 0; index < instrumentSlotCount(selected); ++index) {
      const u32 address = *layout.instrumentTableAddress + index * instrumentHeaderSize(selected);
      if (!reader.has(address, 4)) {
        break;
      }
      runtime.instrumentEnvelopes.emplace(index, EnvelopeRegisters{
          reader.u8At(address + 1), reader.u8At(address + 2), reader.u8At(address + 3)});
    }
  }
  program.behavior.initialTempoMicrosecondsPerQuarter =
      math::tempoMicrosecondsPerQuarter(kDefaultTempo, layout.tempoTimerTarget);
  if (selected.initialMasterVolume != 0xff) {
    program.behavior.initialMasterLevel = math::levelGain(selected.initialMasterVolume);
  }
  const auto initialBalance = math::panGains(selected, math::kPan, 10);
  program.behavior.initialStereoBalance = StereoBalance{initialBalance.left, initialBalance.right};
  program.sectionPlaylist = std::move(playlist.playlist);
  DecodeContext context{
      .reader = reader,
      .layout = layout,
      .selected = selected,
      .definition = definition,
      .diagnostics = diagnostics,
  };

  program.tracks.reserve(layout.trackCount());
  for (u8 track = 0; track < layout.trackCount(); ++track) {
    std::vector<Address> starts;
    for (const PlaylistCommand& command : program.sectionPlaylist->commands) {
      if (command.kind == PlaylistCommandKind::PlaySection && track < command.trackStarts.size() &&
          command.trackStarts[track] &&
          std::ranges::find_if(starts, [&](Address address) {
            return address.value == command.trackStarts[track]->value;
          }) == starts.end()) {
        starts.push_back(*command.trackStarts[track]);
      }
    }
    program.tracks.push_back(decodeTrack(reader, track, starts, context, sequenceId, playlist.annotation, sourceMap));
  }
  program.runtime = makeCompiledRuntime<Playback, ProgramState>(std::move(runtime));

  SequenceRecipes recipes = analyzeCompiledProgram<ProgramState>(program, &ProgramState::recipes, diagnostics);
  return SequenceParse{
      .program = std::move(program),
      .recipes = std::move(recipes),
  };
}

}  // namespace vgmtrans::formats::nin_snes
