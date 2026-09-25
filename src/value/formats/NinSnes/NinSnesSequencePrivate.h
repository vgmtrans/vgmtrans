/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

// Shared playback declarations and Intelligent Systems extensions.
// Public format entry points and scan results remain in NinSnes.h.
#include "value/formats/NinSnes/NinSnes.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceLfo.h"
#include "value/sequence/SequenceMotion.h"

#include <array>
#include <map>
#include <tuple>

namespace vgmtrans::formats::nin_snes::sequence {

using namespace core;

inline constexpr u8 kMelodicKeyCorrection = 24;
inline constexpr u8 kIntelliDrumSlots = 16;
inline constexpr u8 kDefaultTempo = 0x20;

struct PercussionEntry {
  u8 patch = 0;
  u8 note = 0;
  u8 pan = 0;
};

struct EnvelopeRegisters {
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0;
};

struct IntelligentConfig {
  u8 conditionalMask = 0;
  std::vector<u8> transposeTable;
  std::array<PercussionEntry, kIntelliDrumSlots> percussionTable{};
};

struct RuntimeConfig {
  ProfileId profile = ProfileId::Standard;
  u8 tempoTimerTarget = kStandardTimerTarget;
  std::optional<u8> fixedPercussionBase;
  std::vector<u8> programMap;
  IntelligentConfig intelligent;
  std::map<u32, EnvelopeRegisters> instrumentEnvelopes;
};

enum class EventType : u8 {
  Unknown0,
  Unknown1,
  Unknown2,
  Unknown3,
  Unknown4,
  Nop,
  Nop1,
  Nop2,
  End,
  NoteParameter,
  LemmingsNoteParameter,
  IntelliNoteParameter,
  Note,
  Tie,
  Rest,
  Percussion,
  Program,
  Call,
  Pan,
  PanFade,
  VibratoOn,
  VibratoOff,
  MasterVolume,
  MasterVolumeFade,
  VolumeMultiplier,
  Tempo,
  TempoFade,
  GlobalTranspose,
  Transpose,
  TremoloOn,
  TremoloOff,
  Volume,
  VolumeFade,
  VibratoFade,
  PitchEnvelopeTo,
  PitchEnvelopeFrom,
  PitchEnvelopeOff,
  Tuning,
  EchoOn,
  EchoOff,
  EchoParameter,
  EchoVolumeFade,
  PitchSlide,
  PercussionBase,
  Rd2ProgramAndAdsr,
  KonamiLoopStart,
  KonamiLoopEnd,
  KonamiAdsrGain,
  QuintetTuning,
  QuintetAdsr,
  ChannelEchoOn,
  ChannelEchoOff,
  IntelliLegatoOn,
  IntelliLegatoOff,
  IntelliConditionalJump,
  IntelliJump,
  IntelliFe3F5,
  IntelliWritePort,
  IntelliFe3Percussion,
  IntelliDefineVoice,
  IntelliLoadVoice,
  Adsr,
  IntelliGainDurationRate,
  IntelliGainDuration,
  IntelliGain,
  IntelliReleaseGainOff,
  IntelliCustomPercussion,
  IntelliTaSubevent,
  IntelliFe4Subevent,
};

struct Status {
  u8 noteMin = 0x80;
  u8 noteMax = 0xc7;
  u8 percussionMin = 0xca;
  u8 percussionMax = 0xdf;
};

struct Definition {
  Status status;
  std::array<EventType, 256> events{};
  std::vector<u8> volume;
  std::vector<u8> duration;
  std::vector<u8> intelliDuration;
  std::vector<u8> intelliVolume;
};

template <size_t Size>
void useDefault(std::vector<u8>& destination, const std::array<u8, Size>& source) {
  if (destination.empty()) {
    destination.assign(source.begin(), source.end());
  }
}

struct VoiceRecord {
  u8 instrument = 0;
  u8 volume = 0;
  u8 pan = 0;
  u8 tuningTranspose = 0;
};

struct IntelligentState {
  explicit IntelligentState(const IntelligentConfig& config);
  void reset();
  [[nodiscard]] bool usesCustomPercussion(IntelliMode mode) const;

  u8 flags = 0;
  u8 conditionalMask = 0;
  std::vector<u8> transposeTable;
  std::vector<VoiceRecord> voiceTable;
  std::array<PercussionEntry, kIntelliDrumSlots> initialPercussionTable{};
  std::array<PercussionEntry, kIntelliDrumSlots> percussionTable{};
  std::map<std::tuple<u8, u8, u8, u8, u8, u8, u8>, u32> instrumentOverrides;
};

struct VibratoConfig {
  u8 delay = 0;
  u8 rate = 0;
  u8 depth = 0;
  u8 fade = 0;

  [[nodiscard]] bool active() const { return rate != 0 && depth != 0; }
};

struct EchoState {
  void reset();
  [[nodiscard]] ReverbPerformanceEvent current() const;
  void set(u8 mask, u8 left, u8 right);
  void setVolume(u8 left, u8 right);
  void disable();
  void channel(u8 bit, bool enabled);
  void setParameters(u8 delay, s8 feedback, u8 filter);
  [[nodiscard]] bool beginFade(u8 length, u8 left, u8 right);
  [[nodiscard]] bool advanceFade(u64 tick);

private:
  [[nodiscard]] static double gain(s32 fixedVolume);

  ReverbPerformanceEvent event{.voiceMask = 0};
  SequenceFixedPointAutomation<s32> leftVolume;
  SequenceFixedPointAutomation<s32> rightVolume;
  std::optional<u64> lastAdvanceTick;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config);
  void resetRuntime();
  [[nodiscard]] u32 resolveProgram(u8 encoded, u8 percussionMinimum, u8* logical = nullptr) const;
  [[nodiscard]] u8 commandTempo(u8 encoded) const;
  void registerOverride(u8 logical, u8 srcn, u8 adsr1, u8 adsr2, u8 gain, u8 pitchHigh, u8 pitchLow,
                        SourceRange source);
  void rememberStandardDrum(u8 logicalProgram, u32 sourceProgram, u8 key, s8 transpose, u16 sourceNote);
  [[nodiscard]] u32 intelliPercussionProgram(u8 slot, u8 percussionMinimum) const;
  [[nodiscard]] u8 ensureIntelliDrumKit(u8 percussionMinimum, s16 transpose);
  void finishPrepass();

  const Profile& selected;
  u8 tempoTimerTarget = kStandardTimerTarget;
  std::array<u32, 256> basePrograms{};
  std::array<u32, 256> programs{};
  u8 tempo = kDefaultTempo;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> tempoState;
  std::optional<u32> tempoAutomationTrack;
  u8 masterVolume = 0xff;
  u8 volumeMultiplier = 0xff;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> masterVolumeState;
  std::optional<u32> masterVolumeAutomationTrack;
  s8 globalTranspose = 0;
  u8 percussionBase = 0;
  std::optional<u8> fixedPercussionBase;
  IntelligentState intelligent;
  std::map<u32, EnvelopeRegisters> baseEnvelopes;
  std::map<u32, EnvelopeRegisters> instrumentEnvelopes;
  std::map<u8, DrumSlot> standardDrums;
  EchoState echo;
  SequenceRecipes recipes;
  bool collecting = true;
};

struct PitchEnvelope {
  enum class Mode : u8 { None, To, From };
  Mode mode = Mode::None;
  u8 delay = 0;
  u8 length = 0;
  s8 semitones = 0;
};

struct PitchState {
  static constexpr u16 kDefaultRangeCents = 200;

  bool baseValid = false;
  s32 base = 0;
  SequenceLinearMotion<s32> motion;
  PitchSlideBinding transition;
  double transitionNoteKey = 0.0;
  u16 rangeCents = kDefaultRangeCents;
  std::optional<s16> bend = 0;
};

struct KonamiLoopState {
  Address start;
  u8 volumeDelta = 0;
  s16 pitchDelta = 0;
};

struct TrackState {
  TrackState(TrackStateContext track, const RuntimeConfig& config);

  u32 trackNumber = 0;
  u8 noteLength = 1;
  u8 durationRate = 0xfc;
  u8 velocity = 0xfc;
  s8 transpose = 0;
  bool legato = false;
  bool voiceHeld = false;
  EnvelopeRegisters envelope;
  bool inPattern = false;
  u8 patternRemaining = 0;
  Address patternStart;
  KonamiLoopState konamiLoop;
  VibratoConfig vibrato;
  PitchEnvelope pitchEnvelope;
  PitchState pitch;
  u32 melodicProgram = 0;
  bool lastWasPercussion = false;
  u8 percussionProgram = 0;
  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> volume{0xff};
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> pan{10};
  SequenceLfoDepthFadeState vibratoDepth;
};

struct Playback : SequencePlayback<TrackState> {
  ProgramState& program;

  void beginSection(bool first);

  // Notes and instruments.
  [[nodiscard]] u8 soundingDuration() const;
  void updateVoiceHold();
  void legato(bool enabled);
  void emitVoiceNote(double key, u32 duration);
  void standardParameters(u8 duration, bool hasPacked, u8 durationValue, u8 velocityValue);
  void lemmingsParameters(u8 duration, bool hasDuration, u8 durationValue, bool hasVelocity, u8 velocityValue);
  void switchToMelodicProgram();
  void loadInstrumentEnvelope(u32 sourceProgram);
  void melodicProgram(u8 encoded, u8 percussionMinimum);
  void switchToDrumProgram(u8 drumProgram);
  [[nodiscard]] Effects note(u8 noteIndex);
  [[nodiscard]] Effects percussion(u8 slot, u8 percussionMinimum, bool intelli, u16 sourceNote);
  [[nodiscard]] Effects tie();
  [[nodiscard]] Effects rest();

  // Pitch and modulation.
  void emitPitchBend(s16 bend);
  [[nodiscard]] s16 currentPitchBend() const;
  void applyCurrentPitchBend();
  [[nodiscard]] double pitchKey(s32 pitch, double noteKey) const;
  void setPitchBendRange(u16 cents);
  void resetPitchForNote();
  void beginPitchBendMotion(u8 delay, u8 length, s32 target);
  void pitchSlide(u8 delay, u8 length, u8 targetNote);
  void beginNotePitch(u8 rawNote);
  [[nodiscard]] bool pitchMotionIdle() const;
  void advancePitchMotion();
  void pitchEnvelope(PitchEnvelope::Mode mode, u8 delay, u8 length, s8 semitones);
  void vibratoOn(u8 delay, u8 rate, u8 depth);
  void vibratoOff();
  void vibratoFade(u8 length);
  void emitVibratoDepth(u8 rawDepth, PerformanceEmitter output);
  void emitVibratoRateAndDelay();
  void emitConfiguredVibrato();
  void beginNoteVibrato();
  void tremoloOn(u8 delay, u8 rate, u8 depth);
  void tremoloOff();

  // Levels, timing, and echo.
  void emitPan(PerformanceEmitter output, u8 value) const;
  void pan(u8 value);
  void panFade(u8 length, u8 value);
  void tempo(u8 value);
  void tempoFade(u8 length, u8 value);
  void volume(u8 value);
  void volumeFade(u8 length, u8 value);
  [[nodiscard]] double masterGain(u8 value) const;
  void volumeMultiplier(u8 value);
  void masterVolume(u8 value);
  void masterVolumeFade(u8 length, u8 value);
  void advanceTempoFade();
  void advanceVibratoFade();
  void advancePanFade();
  void advanceVolumeFade();
  void advanceMasterFade();
  void tick();
  void globalTranspose(s8 semitones);
  void echo(u8 channels, u8 volumeLeft, u8 volumeRight);
  void channelEcho(bool enabled);
  void echoOff();
  void echoParameters(u8 delay, s8 feedback, u8 filter);
  void echoVolumeFade(u8 length, u8 volumeLeft, u8 volumeRight);

  // Patterns and envelopes.
  void beginPattern(u8 times, Address destination);
  [[nodiscard]] Effects endOrReturn();
  void percussionBase(u8 base);
  void beginKonamiLoop(Address start);
  [[nodiscard]] Effects konamiLoop(u8 times, s8 volumeDelta, s8 pitchDelta, Address destination);
  void konamiEnvelope(u8 adsr1, u8 adsr2, u8 gain);
  void adsr(u8 adsr1, u8 adsr2);

  // Intelligent Systems extensions.
  void intelliParameter(u8 raw, u8 resolved);
  void fe3CustomParameter(u8 raw, u8 resolved);
  void fe3StandardParameter(bool present, u8 durationRate, u8 velocity);
  [[nodiscard]] Effects fe3ParameterFlow(Address standardDestination, Address customDestination);
  [[nodiscard]] Effects intelligentPercussion(u8 slot, u8 percussionMinimum);
  void defineVoiceTable(u8 size);
  void defineVoice(u8 index, u8 instrument, u8 volume, u8 pan, u8 tuningTranspose);
  void overwriteInstrument(u8 logical, u8 srcn, u8 adsr1, u8 adsr2, u8 gain, u8 pitchHigh, u8 pitchLow);
  void loadVoice(u8 index, u8 percussionMinimum, IntelliMode mode);
  void intelliGain(u8 gain);
  void percussionEntry(u8 slot, u8 patch, u8 note, u8 pan);
  void enableCustomPercussion();
  void intelliFlags(u8 mask, bool enabled);
  void fe3Flags(u8 param);
  [[nodiscard]] Effects intelliConditionalJump(Address destination);
};

using Cursor = CompilerCursor<Playback>;

struct DecodeContext {
  ByteReader reader;
  const Layout& layout;
  const Profile& selected;
  const Definition& definition;
  std::vector<Diagnostic>* diagnostics = nullptr;

  [[nodiscard]] Address address(u16 raw) const { return Address{layout.resolveAddress(raw)}; }
};

void loadStandardCommands(std::array<EventType, 256>& events, u8 first);
void configureIntelligentCommands(Definition& definition, IntelliMode mode);
[[nodiscard]] IntelligentConfig readIntelligentConfig(ByteReader reader, const Layout& layout);
[[nodiscard]] DecodedBytecodeCommand decodeIntelligentNoteParameters(Cursor& cursor, const DecodeContext& context,
                                                                    u32 begin);
[[nodiscard]] std::optional<DecodedBytecodeCommand> decodeIntelligentCommand(Cursor& cursor,
                                                                           const DecodeContext& context, EventType type);

}  // namespace vgmtrans::formats::nin_snes::sequence
