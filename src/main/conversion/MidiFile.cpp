/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/Types.h"
#include "Helper.h"
#include "LogManager.h"
#include "Root.h"
#include "VGMSeq.h"

#include <filesystem>
#include <ranges>

#include <spdlog/fmt/fmt.h>

MidiFile::MidiFile(VGMSeq *theAssocSeq)
    : assocSeq(theAssocSeq),
      globalTrack(this, false),
      globalTranspose(0),
      bMonophonicTracks(false) {
  this->bMonophonicTracks = assocSeq->usesMonophonicTracks();
  this->globalTrack.bMonophonic = this->bMonophonicTracks;

  this->m_ppqn = assocSeq->ppqn();
}

MidiFile::~MidiFile() {
  deleteVect<MidiTrack>(aTracks);
}

MidiTrack *MidiFile::addTrack() {
  aTracks.push_back(new MidiTrack(this, bMonophonicTracks));
  return aTracks.back();
}

MidiTrack *MidiFile::insertTrack(u32 trackNum) {
  if (trackNum + 1 > aTracks.size())
    aTracks.resize(trackNum + 1, nullptr);

  aTracks[trackNum] = new MidiTrack(this, bMonophonicTracks);
  return aTracks[trackNum];
}

int MidiFile::getMidiTrackIndex(const MidiTrack *midiTrack) {
  auto it = std::ranges::find(aTracks, midiTrack);
  if (it != aTracks.end()) {
    return static_cast<int>(std::distance(aTracks.begin(), it));
  } else {
    return -1;
  }
}

void MidiFile::setPPQN(u16 ppqn) {
  this->m_ppqn = ppqn;
}

u32 MidiFile::ppqn() const {
  return m_ppqn;
}

void MidiFile::sort() {
  for (u32 i = 0; i < aTracks.size(); i++) {
    if (aTracks[i]) {
      if (aTracks[i]->aEvents.empty()) {
        delete aTracks[i];
        aTracks.erase(aTracks.begin() + i--);
      } else
        aTracks[i]->sort();
    }
  }
}

bool MidiFile::saveMidiFile(const std::filesystem::path &filepath) {
  std::vector<u8> midiBuf;
  writeMidiToBuffer(midiBuf);
  return pRoot->UI_writeBufferToFile(filepath, &midiBuf[0], midiBuf.size());
}

void MidiFile::writeMidiToBuffer(std::vector<u8> &buf) {
  size_t nNumTracks = aTracks.size();
  buf.push_back('M');
  buf.push_back('T');
  buf.push_back('h');
  buf.push_back('d');
  buf.push_back(0);
  buf.push_back(0);
  buf.push_back(0);
  buf.push_back(6);  // MThd length - always 6
  buf.push_back(0);
  buf.push_back(1);                //Midi format - type 1
  buf.push_back((nNumTracks & 0xFF00) >> 8);  //num tracks hi
  buf.push_back(nNumTracks & 0x00FF);         //num tracks lo
  buf.push_back((m_ppqn & 0xFF00) >> 8);
  buf.push_back(m_ppqn & 0xFF);

  sort();

  for (auto& aTrack : aTracks) {
    if (aTrack) {
      std::vector<u8> trackBuf;
      globalTranspose = 0;
      aTrack->writeTrack(trackBuf);
      buf.insert(buf.end(), trackBuf.begin(), trackBuf.end());
    }
  }
  globalTranspose = 0;
}

//  *********
//  MidiTrack
//  *********

MidiTrack::MidiTrack(MidiFile *theParentSeq, bool monophonic)
    : parentSeq(theParentSeq),
      bMonophonic(monophonic),
      bHasEndOfTrack(false),
      channelGroup(0),
      DeltaTime(0),
      prevDurEvent(nullptr),
      bSustain(false) {}

MidiTrack::~MidiTrack() {
  deleteVect<MidiEvent>(aEvents);
}

void MidiTrack::sort() {
  std::ranges::stable_sort(aEvents, PriorityCmp()); // Sort all the events by priority
  std::ranges::stable_sort(aEvents, AbsTimeCmp());  // Sort all the events by absolute time,
                                                             // so that delta times can be recorded correctly
  if (!bHasEndOfTrack && !aEvents.empty()) {
    aEvents.push_back(new EndOfTrackEvent(this, aEvents.back()->absTime));
    bHasEndOfTrack = true;
  }
}

void MidiTrack::writeTrack(std::vector<u8> &buf) const {
  buf.push_back('M');
  buf.push_back('T');
  buf.push_back('r');
  buf.push_back('k');
  buf.push_back(0);
  buf.push_back(0);
  buf.push_back(0);
  buf.push_back(0);
  u32 time = 0;  // start at 0 ticks

  std::vector<MidiEvent *> finalEvents(aEvents);
  std::vector<MidiEvent *> &globEvents = parentSeq->globalTrack.aEvents;
  finalEvents.insert(finalEvents.end(), globEvents.begin(), globEvents.end());

  std::ranges::stable_sort(finalEvents, PriorityCmp()); // Sort all the events by priority
  std::ranges::stable_sort(finalEvents, AbsTimeCmp());  // Sort all the events by absolute time,
                                                                 // so that delta times can be recorded correctly

  size_t numEvents = finalEvents.size();

  for (size_t i = 0; i < numEvents; i++)
    time = finalEvents[i]->writeEvent(buf, time);  // write all events into the buffer

  size_t trackSize = buf.size() - 8;  // -8 for MTrk and size that shouldn't be accounted for
  buf[4] = static_cast<u8>((trackSize & 0xFF000000) >> 24);
  buf[5] = static_cast<u8>((trackSize & 0x00FF0000) >> 16);
  buf[6] = static_cast<u8>((trackSize & 0x0000FF00) >> 8);
  buf[7] = static_cast<u8>(trackSize & 0x000000FF);
}

void MidiTrack::setChannelGroup(int theChannelGroup) {
  channelGroup = theChannelGroup;
}

// Delta Time Functions
u32 MidiTrack::getDelta() const {
  return DeltaTime;
}

void MidiTrack::setDelta(u32 NewDelta) {
  DeltaTime = NewDelta;
}

void MidiTrack::addDelta(u32 AddDelta) {
  DeltaTime += AddDelta;
}

void MidiTrack::subtractDelta(u32 SubtractDelta) {
  DeltaTime -= SubtractDelta;
}

void MidiTrack::resetDelta() {
  DeltaTime = 0;
}

void MidiTrack::addNoteOn(u8 channel, s8 key, s8 vel) {
  aEvents.push_back(new NoteEvent(this, channel, getDelta(), true, key, vel));
}

void MidiTrack::insertNoteOn(u8 channel, s8 key, s8 vel, u32 absTime) {
  aEvents.push_back(new NoteEvent(this, channel, absTime, true, key, vel));
}

void MidiTrack::addNoteOff(u8 channel, s8 key) {
  aEvents.push_back(new NoteEvent(this, channel, getDelta(), false, key));
}

void MidiTrack::insertNoteOff(u8 channel, s8 key, u32 absTime) {
  aEvents.push_back(new NoteEvent(this, channel, absTime, false, key));
}

void MidiTrack::addNoteByDur(u8 channel, s8 key, s8 vel, u32 duration) {
  purgePrevNoteOffs(getDelta());
  aEvents.push_back(new NoteEvent(this, channel, getDelta(), true, key, vel));  // add note on
  NoteEvent *prevDurNoteOff = new NoteEvent(this, channel, getDelta() + duration, false, key);
  prevDurNoteOffs.push_back(prevDurNoteOff);
  aEvents.push_back(prevDurNoteOff);  // add note off at end of dur
}

//TODO: MOVE! This definitely doesn't belong here.
void MidiTrack::addNoteByDur_TriAce(u8 channel, s8 key, s8 vel, u32 duration) {
  u32 CurDelta = getDelta();
  size_t nNumEvents = aEvents.size();

  NoteEvent* ContNote = nullptr;  // Continuted Note
  for (size_t curEvt = 0; curEvt < nNumEvents; curEvt++) {
    // Check for a event on this track with the following conditions:
    //	1. Its Event Delta Time is > current Delta Time.
    //	2. It's a Note Off event
    //	3. Its key matches the key of the new note.
    // If so, we're restarting an already played note. In the case of the TriAce driver that means,
    // that we resume the note, so we need to move the NoteOff event.

    // Note: In previous TriAce drivers (like MegaDrive and SNES versions),
    //       a Note gets extended by a Note On event at the tick where another note expires.
    //       Valkyrie Profile: 225 Fragments of the Heart confirms, that this is NOT the case in the PS1 version.
    if (aEvents[curEvt]->absTime > CurDelta && aEvents[curEvt]->eventType() == MIDIEVENT_NOTEON) {
      NoteEvent *NoteEvt = dynamic_cast<NoteEvent*>(aEvents[curEvt]);
      if (NoteEvt->key == key && !NoteEvt->bNoteDown) {
        ContNote = NoteEvt;
        break;
      }
    }
  }

  if (ContNote == nullptr) {
    purgePrevNoteOffs(CurDelta);
    aEvents.push_back(new NoteEvent(this, channel, CurDelta, true, key, vel));  // add note on
    NoteEvent *prevDurNoteOff = new NoteEvent(this, channel, CurDelta + duration, false, key);
    prevDurNoteOffs.push_back(prevDurNoteOff);
    aEvents.push_back(prevDurNoteOff);  // add note off at end of dur
  } else {
    ContNote->absTime = CurDelta + duration;  // fix DeltaTime of the already inserted NoteOff event
  }
}

void MidiTrack::insertNoteByDur(u8 channel, s8 key, s8 vel, u32 duration, u32 absTime) {
  purgePrevNoteOffs(std::max(getDelta(), absTime));
  aEvents.push_back(new NoteEvent(this, channel, absTime, true, key, vel));  // add note on
  NoteEvent *prevDurNoteOff = new NoteEvent(this, channel, absTime + duration, false, key);
  prevDurNoteOffs.push_back(prevDurNoteOff);
  aEvents.push_back(prevDurNoteOff);  // add note off at end of dur
}

void MidiTrack::purgePrevNoteOffs() {
  prevDurNoteOffs.clear();
}

void MidiTrack::purgePrevNoteOffs(u32 absTime) {
  prevDurNoteOffs.erase(std::remove_if(prevDurNoteOffs.begin(), prevDurNoteOffs.end(),
    [absTime](const NoteEvent *e) { return e && e->absTime <= absTime; }),
    prevDurNoteOffs.end());
}

/*void MidiTrack::AddVolMarker(u8 channel, u8 vol, s8 priority)
{
	MidiEvent* newEvent = new VolMarkerEvent(this, channel, GetDelta(), vol);
	aEvents.push_back(newEvent);
}

void MidiTrack::InsertVolMarker(u8 channel, u8 vol, u32 absTime, s8 priority)
{
	MidiEvent* newEvent = new VolMarkerEvent(this, channel, absTime, vol);
	aEvents.push_back(newEvent);
}*/

void MidiTrack::addControllerEvent(u8 channel, u8 controllerNum, u8 theDataByte) {
  aEvents.push_back(new ControllerEvent(this, channel, getDelta(), controllerNum, theDataByte));
}

void MidiTrack::insertControllerEvent(u8 channel, u8 controllerNum, u8 theDataByte, u32 absTime) {
  aEvents.push_back(new ControllerEvent(this, channel, absTime, controllerNum, theDataByte));
}

void MidiTrack::addVol(u8 channel, u8 vol) {
  aEvents.push_back(new VolumeEvent(this, channel, getDelta(), vol));
}

void MidiTrack::insertVol(u8 channel, u8 vol, u32 absTime) {
  aEvents.push_back(new VolumeEvent(this, channel, absTime, vol));
}

void MidiTrack::addVolumeFine(u8 channel, u8 volume_lsb) {
  aEvents.push_back(new VolumeFineEvent(this, channel, getDelta(), volume_lsb));
}

void MidiTrack::insertVolumeFine(u8 channel, u8 volume_lsb, u32 absTime) {
  aEvents.push_back(new VolumeFineEvent(this, channel, absTime, volume_lsb));
}

//TODO: Master Volume sysex events are meant to be global to device, not per channel.
// For per channel master volume, we should add a system for normalizing controller vol events.
void MidiTrack::addMasterVol(u8 channel, u8 volMsb, u8 volLsb) {
  MidiEvent *newEvent = new MasterVolEvent(this, channel, getDelta(), volMsb, volLsb);
  aEvents.push_back(newEvent);
}

void MidiTrack::insertMasterVol(u8 channel, u8 volMsb, u32 absTime) {
  insertMasterVol(channel, volMsb, 0, absTime);
}

void MidiTrack::insertMasterVol(u8 channel, u8 volMsb, u8 volLsb, u32 absTime) {
  MidiEvent *newEvent = new MasterVolEvent(this, channel, absTime, volMsb, volLsb);
  aEvents.push_back(newEvent);
}

void MidiTrack::addExpression(u8 channel, u8 expression) {
  aEvents.push_back(new ExpressionEvent(this, channel, getDelta(), expression));
}

void MidiTrack::insertExpression(u8 channel, u8 expression, u32 absTime) {
  aEvents.push_back(new ExpressionEvent(this, channel, absTime, expression));
}

void MidiTrack::addExpressionFine(u8 channel, u8 expression_lsb) {
  aEvents.push_back(new ExpressionFineEvent(this, channel, getDelta(), expression_lsb));
}

void MidiTrack::insertExpressionFine(u8 channel, u8 expression_lsb, u32 absTime) {
  aEvents.push_back(new ExpressionFineEvent(this, channel, absTime, expression_lsb));
}

void MidiTrack::addSustain(u8 channel, u8 depth) {
  aEvents.push_back(new SustainEvent(this, channel, getDelta(), depth));
}

void MidiTrack::insertSustain(u8 channel, u8 depth, u32 absTime) {
  aEvents.push_back(new SustainEvent(this, channel, absTime, depth));
}

void MidiTrack::addPortamento(u8 channel, bool bOn) {
  aEvents.push_back(new PortamentoEvent(this, channel, getDelta(), bOn));
}

void MidiTrack::insertPortamento(u8 channel, bool bOn, u32 absTime) {
  aEvents.push_back(new PortamentoEvent(this, channel, absTime, bOn));
}

void MidiTrack::addPortamentoTime(u8 channel, u8 time) {
  aEvents.push_back(new PortamentoTimeEvent(this, channel, getDelta(), time));
}

void MidiTrack::insertPortamentoTime(u8 channel, u8 time, u32 absTime) {
  aEvents.push_back(new PortamentoTimeEvent(this, channel, absTime, time));
}

void MidiTrack::addPortamentoTimeFine(u8 channel, u8 time) {
  aEvents.push_back(new PortamentoTimeFineEvent(this, channel, getDelta(), time));
}

void MidiTrack::insertPortamentoTimeFine(u8 channel, u8 time, u32 absTime) {
  aEvents.push_back(new PortamentoTimeFineEvent(this, channel, absTime, time));
}

void MidiTrack::addPortamentoControl(u8 channel, u8 key) {
  aEvents.push_back(new PortamentoControlEvent(this, channel, getDelta(), key));
}

void MidiTrack::insertPortamentoControl(u8 channel, u8 key, u32 absTime) {
  aEvents.push_back(new PortamentoControlEvent(this, channel, absTime, key));
}

void MidiTrack::addMono(u8 channel) {
  aEvents.push_back(new MonoEvent(this, channel, getDelta()));
}

void MidiTrack::insertMono(u8 channel, u32 absTime) {
  aEvents.push_back(new MonoEvent(this, channel, absTime));
}

void MidiTrack::addLegatoPedal(u8 channel, bool bOn) {
  aEvents.push_back(new LegatoPedalEvent(this, channel, getDelta(), bOn));
}

void MidiTrack::insertLegatoPedal(u8 channel, bool bOn, u32 absTime) {
  aEvents.push_back(new LegatoPedalEvent(this, channel, absTime, bOn));
}

void MidiTrack::addPan(u8 channel, u8 pan) {
  aEvents.push_back(new PanEvent(this, channel, getDelta(), pan));
}

void MidiTrack::insertPan(u8 channel, u8 pan, u32 absTime) {
  aEvents.push_back(new PanEvent(this, channel, absTime, pan));
}

void MidiTrack::addReverb(u8 channel, u8 reverb) {
  aEvents.push_back(new ControllerEvent(this, channel, getDelta(), 91, reverb));
}

void MidiTrack::insertReverb(u8 channel, u8 reverb, u32 absTime) {
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 91, reverb));
}

void MidiTrack::addModulation(u8 channel, u8 depth) {
  aEvents.push_back(new ModulationEvent(this, channel, getDelta(), depth));
}

void MidiTrack::insertModulation(u8 channel, u8 depth, u32 absTime) {
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 1, depth));
}

void MidiTrack::addBreath(u8 channel, u8 depth) {
  aEvents.push_back(new BreathEvent(this, channel, getDelta(), depth));
}

void MidiTrack::insertBreath(u8 channel, u8 depth, u32 absTime) {
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 2, depth));
}

void MidiTrack::addPitchBend(u8 channel, s16 bend) {
  aEvents.push_back(new PitchBendEvent(this, channel, getDelta(), bend));
}

void MidiTrack::insertPitchBend(u8 channel, s16 bend, u32 absTime) {
  aEvents.push_back(new PitchBendEvent(this, channel, absTime, bend));
}

void MidiTrack::addChannelPressure(u8 channel, u8 pressure) {
  aEvents.push_back(new ChannelPressureEvent(this, channel, getDelta(), pressure));
}

void MidiTrack::insertChannelPressure(u8 channel, u8 pressure, u32 absTime) {
  aEvents.push_back(new ChannelPressureEvent(this, channel, absTime, pressure));
}

void MidiTrack::addPitchBendRange(u8 channel, u16 cents) {
  insertPitchBendRange(channel, cents, getDelta());
}

void MidiTrack::insertPitchBendRange(u8 channel, u16 cents, u32 absTime) {
  u8 semitones = cents / 100;
  u8 finetune_cents = cents % 100;
  // We push the LSB controller event first as somee virtual instruments only react upon receiving MSB
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 101, 0, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 100, 0, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 38, finetune_cents, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 6, semitones, PRIORITY_HIGHER - 1));
}

void MidiTrack::addFineTuning(u8 channel, u8 msb, u8 lsb) {
  insertFineTuning(channel, msb, lsb, getDelta());
}

void MidiTrack::insertFineTuning(u8 channel, u8 msb, u8 lsb, u32 absTime) {
  // We push the LSB controller event first as somee virtual instruments only react upon receiving MSB
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 101, 0, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 100, 1, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 38, lsb, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 6, msb, PRIORITY_HIGHER - 1));
}

void MidiTrack::addFineTuning(u8 channel, double cents) {
  insertFineTuning(channel, cents, getDelta());
}

void MidiTrack::insertFineTuning(u8 channel, double cents, u32 absTime) {
  double semitones = std::max(-1.0, std::min(1.0, cents / 100.0));
  s16 midiTuning = std::min(static_cast<int>(lround(8192 * semitones)), 8191) + 8192;

  insertFineTuning(channel, midiTuning >> 7, midiTuning & 0x7f, absTime);
}

void MidiTrack::addCoarseTuning(u8 channel, u8 msb, u8 lsb) {
  insertCoarseTuning(channel, msb, lsb, getDelta());
}

void MidiTrack::insertCoarseTuning(u8 channel, u8 msb, u8 lsb, u32 absTime) {
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 101, 0, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 100, 2, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 38, lsb, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 6, msb, PRIORITY_HIGHER - 1));
}

void MidiTrack::addCoarseTuning(u8 channel, double semitones) {
  insertCoarseTuning(channel, semitones, getDelta());
}

void MidiTrack::insertCoarseTuning(u8 channel, double semitones, u32 absTime) {
  semitones = std::max(-64.0, std::min(64.0, semitones));
  s16 midiTuning = std::min(static_cast<int>(lround(128 * semitones)), 8191) + 8192;
  insertCoarseTuning(channel, midiTuning >> 7, midiTuning & 0x7f, absTime);
}

void MidiTrack::addModulationDepthRange(u8 channel, u8 msb, u8 lsb) {
  insertModulationDepthRange(channel, msb, lsb, getDelta());
}

void MidiTrack::insertModulationDepthRange(u8 channel, u8 msb, u8 lsb, u32 absTime) {
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 101, 0, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 100, 5, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 38, lsb, PRIORITY_HIGHER - 1));
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 6, msb, PRIORITY_HIGHER - 1));
}

void MidiTrack::addModulationDepthRange(u8 channel, double semitones) {
  insertModulationDepthRange(channel, semitones, getDelta());
}

void MidiTrack::insertModulationDepthRange(u8 channel, double semitones, u32 absTime) {
  semitones = std::max(-64.0, std::min(64.0, semitones));
  s16 midiTuning = std::min(static_cast<int>(lround(128 * semitones)), 8191) + 8192;
  insertFineTuning(channel, midiTuning >> 7, midiTuning & 0x7f, absTime);
}

void MidiTrack::addProgramChange(u8 channel, u8 progNum) {
  aEvents.push_back(new ProgChangeEvent(this, channel, getDelta(), progNum));
  // Temporary hack workaround Cubase refusing to send program change events to VST3
  aEvents.push_back(new ControllerEvent(this, channel, getDelta(), 115, progNum));
}

void MidiTrack::addBankSelect(u8 channel, u8 bank) {
  aEvents.push_back(new BankSelectEvent(this, channel, getDelta(), bank));
}

void MidiTrack::addBankSelectFine(u8 channel, u8 lsb) {
  aEvents.push_back(new BankSelectFineEvent(this, channel, getDelta(), lsb));
}

void MidiTrack::insertBankSelect(u8 channel, u8 bank, u32 absTime) {
  aEvents.push_back(new ControllerEvent(this, channel, absTime, 0, bank));
}

void MidiTrack::addTempo(u32 microSeconds) {
  aEvents.push_back(new TempoEvent(this, getDelta(), microSeconds));
  //bAddedTempo = true;
}

void MidiTrack::addTempoBPM(double BPM) {
  u32 microSecs = static_cast<u32>(std::round(60000000.0 / BPM));
  aEvents.push_back(new TempoEvent(this, getDelta(), microSecs));
  //bAddedTempo = true;
}

void MidiTrack::insertTempo(u32 microSeconds, u32 absTime) {
  aEvents.push_back(new TempoEvent(this, absTime, microSeconds));
  //bAddedTempo = true;
}

void MidiTrack::insertTempoBPM(double BPM, u32 absTime) {
  u32 microSecs = static_cast<u32>(std::round(60000000.0 / BPM));
  aEvents.push_back(new TempoEvent(this, absTime, microSecs));
  //bAddedTempo = true;
}

void MidiTrack::addMidiPort(u8 port) {
  aEvents.push_back(new MidiPortEvent(this, getDelta(), port));
}

void MidiTrack::insertMidiPort(u8 port, u32 absTime) {
  aEvents.push_back(new MidiPortEvent(this, absTime, port));
}

void MidiTrack::addTimeSig(u8 numer, u8 denom, u8 ticksPerQuarter) {
  aEvents.push_back(new TimeSigEvent(this, getDelta(), numer, denom, ticksPerQuarter));
  //bAddedTimeSig = true;
}

void MidiTrack::insertTimeSig(u8 numer, u8 denom, u8 ticksPerQuarter, u32 absTime) {
  aEvents.push_back(new TimeSigEvent(this, absTime, numer, denom, ticksPerQuarter));
  //bAddedTimeSig = true;
}

void MidiTrack::addEndOfTrack() {
  aEvents.push_back(new EndOfTrackEvent(this, getDelta()));
  bHasEndOfTrack = true;
}

void MidiTrack::insertEndOfTrack(u32 absTime) {
  aEvents.push_back(new EndOfTrackEvent(this, absTime));
  bHasEndOfTrack = true;
}

void MidiTrack::addText(const std::string &str) {
  aEvents.push_back(new TextEvent(this, getDelta(), str));
}

void MidiTrack::insertText(const std::string &str, u32 absTime) {
  aEvents.push_back(new TextEvent(this, absTime, str));
}

void MidiTrack::addSeqName(const std::string &str) {
  aEvents.push_back(new SeqNameEvent(this, getDelta(), str));
}

void MidiTrack::insertSeqName(const std::string &str, u32 absTime) {
  aEvents.push_back(new SeqNameEvent(this, absTime, str));
}

void MidiTrack::addTrackName(const std::string &str) {
  aEvents.push_back(new TrackNameEvent(this, getDelta(), str));
}

void MidiTrack::insertTrackName(const std::string &str, u32 absTime) {
  aEvents.push_back(new TrackNameEvent(this, absTime, str));
}

void MidiTrack::addGMReset() {
  aEvents.push_back(new GMResetEvent(this, getDelta()));
}

void MidiTrack::insertGMReset(u32 absTime) {
  aEvents.push_back(new GMResetEvent(this, absTime));
}

void MidiTrack::addGM2Reset() {
  aEvents.push_back(new GM2ResetEvent(this, getDelta()));
}

void MidiTrack::insertGM2Reset(u32 absTime) {
  aEvents.push_back(new GM2ResetEvent(this, absTime));
}

void MidiTrack::addGSReset() {
  aEvents.push_back(new GSResetEvent(this, getDelta()));
}

void MidiTrack::insertGSReset(u32 absTime) {
  aEvents.push_back(new GSResetEvent(this, absTime));
}

void MidiTrack::addXGReset() {
  aEvents.push_back(new XGResetEvent(this, getDelta()));
}

void MidiTrack::insertXGReset(u32 absTime) {
  aEvents.push_back(new XGResetEvent(this, absTime));
}

// SPECIAL NON-MIDI EVENTS

// Transpose events offset the key when we write the Midi file.
//  used to implement global transpose events found in QSound

//void MidiTrack::AddTranspose(s8 semitones)
//{
//	aEvents.push_back(new TransposeEvent(this, GetDelta(), semitones));
//}

void MidiTrack::insertGlobalTranspose(u32 absTime, s8 semitones) {
  aEvents.push_back(new GlobalTransposeEvent(this, absTime, semitones));
}


void MidiTrack::addMarker(u8 channel,
                          const std::string &markername,
                          u8 databyte1,
                          u8 databyte2,
                          s8 priority) {
  aEvents.push_back(new MarkerEvent(this, channel, getDelta(), markername, databyte1, databyte2, priority));
}

void MidiTrack::insertMarker(u8 channel,
                  const std::string &markername,
                  u8 databyte1,
                  u8 databyte2,
                  s8 priority,
                  u32 absTime) {
  aEvents.push_back(new MarkerEvent(this, channel, absTime, markername, databyte1, databyte2, priority));
}

// YMMY Events

void MidiTrack::addYmmySynthAssignment(uint8_t channel, std::string synthName) {
  aEvents.push_back(new YmmySynthChangeEvent(this, channel, channelGroup, getDelta(), synthName));
}

//  *********
//  MidiEvent
//  *********

MidiEvent::MidiEvent(MidiTrack *thePrntTrk, u32 absoluteTime, u8 theChannel, s8 thePriority)
    : prntTrk(thePrntTrk), channel(theChannel), absTime(absoluteTime), priority(thePriority) {
}

bool MidiEvent::isMetaEvent() {
  MidiEventType type = eventType();
  return type == MIDIEVENT_TEMPO ||
         type == MIDIEVENT_TEXT ||
         type == MIDIEVENT_MIDIPORT ||
         type == MIDIEVENT_TIMESIG ||
         type == MIDIEVENT_ENDOFTRACK;
}

bool MidiEvent::isSysexEvent() {
  MidiEventType type = eventType();
  return type == MIDIEVENT_MASTERVOL ||
         type == MIDIEVENT_RESET ||
         type == MIDIEVENT_YMMY_SYNTHASSIGN;
}

void MidiEvent::writeVarLength(std::vector<u8> &buf, u32 value) {
  u32 buffer = value & 0x7F;

  while ((value >>= 7)) {
    buffer <<= 8;
    buffer |= ((value & 0x7F) | 0x80);
  }

  while (true) {
    buf.push_back(static_cast<u8>(buffer));
    if (buffer & 0x80)
      buffer >>= 8;
    else
      break;
  }
}

u32 MidiEvent::writeMetaEvent(std::vector<u8> &buf,
                                   u32 time,
                                   u8 metaType,
                                   const u8* data,
                                   size_t dataSize) const {
  writeVarLength(buf, absTime - time);
  buf.push_back(0xFF);
  buf.push_back(metaType);
  writeVarLength(buf, static_cast<u32>(dataSize));
  for (size_t dataIndex = 0; dataIndex < dataSize; dataIndex++) {
    buf.push_back(data[dataIndex]);
  }
  return absTime;
}

u32 MidiEvent::writeMetaTextEvent(std::vector<u8> &buf, u32 time, u8 metaType, const std::string& str) const {
  return writeMetaEvent(buf, time, metaType, (u8 *)str.c_str(), str.length());
}

std::string MidiEvent::getNoteName(int noteNumber) {
  const char* noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

  int octave;
  int key;
  if (noteNumber >= 0) {
    octave = noteNumber / 12;
    key = noteNumber % 12;
  } else {
    octave = -(-noteNumber / 12) - 1;
    key = 12 - (-(noteNumber + 1) % 12) - 1;
  }
  octave--;

  return fmt::format("{} {}", noteNames[key], octave);
}

bool MidiEvent::operator<(const MidiEvent &theMidiEvent) const {
  return (absTime < theMidiEvent.absTime);
}

bool MidiEvent::operator>(const MidiEvent &theMidiEvent) const {
  return (absTime > theMidiEvent.absTime);
}

//  *********
//  NoteEvent
//  *********


NoteEvent::NoteEvent(MidiTrack *prntTrk,
                     u8 channel,
                     u32 absoluteTime,
                     bool bNoteDown,
                     u8 theKey,
                     u8 theVel)
    : MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_LOWER),
      bNoteDown(bNoteDown),
      key(theKey),
      vel(theVel) {
}

u32 NoteEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  writeVarLength(buf, absTime - time);

  u8 finalKey = key + ((channel == 9) ? 0 : prntTrk->parentSeq->globalTranspose);

  if (bNoteDown) {
    buf.push_back(0x90 + channel);
    if (prntTrk->activeNotes.contains(key)) {
      L_WARN("During MIDI conversion, received note on event for a key with an already live note on event."
        " Channel: {} Key: {}", channel, key);
    }
    prntTrk->activeNotes[key] = finalKey;
  }
  else {
    buf.push_back(0x80 + channel);
    if (prntTrk->activeNotes.contains(key)) {
      finalKey = prntTrk->activeNotes[key];
      prntTrk->activeNotes.erase(key);
    } else {
      L_WARN("During MIDI conversion, a note off event could not find a matching prior note on event."
        " Channel: {} Key: {}", channel, key);
    }
  }

  buf.push_back(finalKey);
  buf.push_back(vel);

  return absTime;
}

//  ************
//  DurNoteEvent
//  ************

//DurNoteEvent::DurNoteEvent(MidiTrack* prntTrk, u8 channel, u32 absoluteTime, u8 theKey, u8 theVel, u32 theDur)
//: MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_LOWER), key(theKey), vel(theVel), duration(theDur)
//{
//}
/*
DurNoteEvent* DurNoteEvent::MakeCopy()
{
	return new DurNoteEvent(prntTrk, channel, AbsTime, key, vel, duration);
}*/

/*void DurNoteEvent::PrepareWrite(vector<MidiEvent*> & aEvents)
{
	prntTrk->aEvents.push_back(new NoteEvent(prntTrk, channel, AbsTime, true, key, vel));	//add note on
	prntTrk->aEvents.push_back(new NoteEvent(prntTrk, channel, AbsTime+duration, false, key, vel));  //add note off at end of dur
}*/

//u32 DurNoteEvent::WriteEvent(vector<u8> & buf, u32 time)		//we do note use WriteEvent on DurNoteEvents... this is what PrepareWrite is for, to create NoteEvents in substitute
//{
//	return false;
//}

//  ********
//  VolEvent
//  ********

/*VolEvent::VolEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 theVol, s8 thePriority)
: ControllerEvent(prntTrk, channel, absoluteTime, 7), vol(theVol)
{
}

VolEvent* VolEvent::MakeCopy()
{
	return new VolEvent(prntTrk, channel, AbsTime, vol, priority);
}*/

//  ***************
//  ControllerEvent
//  ***************

ControllerEvent::ControllerEvent(MidiTrack *prntTrk,
                                 u8 channel,
                                 u32 absoluteTime,
                                 u8 controllerNum,
                                 u8 theDataByte,
                                 s8 thePriority)
    : MidiEvent(prntTrk, absoluteTime, channel, thePriority), controlNum(controllerNum), dataByte(theDataByte) {
}

u32 ControllerEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  writeVarLength(buf, absTime - time);
  buf.push_back(0xB0 + channel);
  buf.push_back(controlNum & 0x7F);
  buf.push_back(dataByte);
  return absTime;
}

//  **********************
//  PortamentoControlEvent
//  **********************

u32 PortamentoControlEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  // Add the global transpose into the starting key of the portamento control event
  u8 originalDataByte = dataByte;
  dataByte = std::clamp<s16>(dataByte + prntTrk->parentSeq->globalTranspose, 0, 127);
  u32 result = ControllerEvent::writeEvent(buf, time);
  dataByte = originalDataByte;
  return result;
}

//  **********
//  SysexEvent
//  **********

SysexEvent::SysexEvent(MidiTrack *prntTrk,
                       u32 absoluteTime,
                       const std::vector<u8>& sysexData,
                       s8 thePriority)
    : MidiEvent(prntTrk, absoluteTime, 0, thePriority), sysexData(sysexData) {
}

u32 SysexEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  writeVarLength(buf, absTime - time);
  buf.push_back(0xF0);
  buf.insert(buf.end(), sysexData.begin(), sysexData.end());
  buf.push_back(0xF7);
  return absTime;
}

//  ***************
//  ProgChangeEvent
//  ***************

ProgChangeEvent::ProgChangeEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 progNum)
    : MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_HIGH), programNum(progNum) {
}

u32 ProgChangeEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  writeVarLength(buf, absTime - time);
  buf.push_back(0xC0 + channel);
  buf.push_back(programNum & 0x7F);
  return absTime;
}

//  **************
//  PitchBendEvent
//  **************

PitchBendEvent::PitchBendEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, s16 bendAmt)
    : MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_MIDDLE), bend(bendAmt) {
}

u32 PitchBendEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  u8 loByte = (bend + 0x2000) & 0x7F;
  u8 hiByte = ((bend + 0x2000) & 0x3F80) >> 7;
  writeVarLength(buf, absTime - time);
  buf.push_back(0xE0 + channel);
  buf.push_back(loByte);
  buf.push_back(hiByte);
  return absTime;
}

//  ********************
//  ChannelPressureEvent
//  ********************

ChannelPressureEvent::ChannelPressureEvent(MidiTrack *prntTrk,
                                           u8 channel,
                                           u32 absoluteTime,
                                           u8 pressureAmt)
    : MidiEvent(prntTrk, absoluteTime, channel, PRIORITY_MIDDLE), pressure(pressureAmt) {
}

u32 ChannelPressureEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  writeVarLength(buf, absTime - time);
  buf.push_back(0xD0 + channel);
  buf.push_back(pressure & 0x7F);
  return absTime;
}

//  **********
//  TempoEvent
//  **********

TempoEvent::TempoEvent(MidiTrack *prntTrk, u32 absoluteTime, u32 microSeconds)
    : MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_HIGHEST), microSecs(microSeconds) {
}

u32 TempoEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  u8 data[3] = {
      static_cast<u8>((microSecs & 0xFF0000) >> 16),
      static_cast<u8>((microSecs & 0x00FF00) >> 8),
      static_cast<u8>(microSecs & 0x0000FF)
  };
  return writeMetaEvent(buf, time, 0x51, data, 3);
}


//  *************
//  MidiPortEvent
//  *************

MidiPortEvent::MidiPortEvent(MidiTrack *prntTrk, u32 absoluteTime, u8 port)
    : MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_HIGHEST), port(port) {
}

u32 MidiPortEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  return writeMetaEvent(buf, time, 0x21, &port, 1);
}

//  ************
//  TimeSigEvent
//  ************

TimeSigEvent::TimeSigEvent(MidiTrack *prntTrk,
                           u32 absoluteTime,
                           u8 numerator,
                           u8 denominator,
                           u8 clicksPerQuarter)
    : MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_HIGHEST), numer(numerator), denom(denominator),
      ticksPerQuarter(clicksPerQuarter) {
}

u32 TimeSigEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  //denom is expressed in power of 2... so if we have 6/8 time.  it's 6 = 2^x  ==  ln6 / ln2
  u8 data[4] = {
      numer,
      static_cast<u8>(log(static_cast<double>(denom)) / 0.69314718055994530941723212145818),
      ticksPerQuarter,
      8
  };
  return writeMetaEvent(buf, time, 0x58, data, 4);
}

//  ***************
//  EndOfTrackEvent
//  ***************

EndOfTrackEvent::EndOfTrackEvent(MidiTrack *prntTrk, u32 absoluteTime)
    : MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_LOWEST) {
}


u32 EndOfTrackEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  return writeMetaEvent(buf, time, 0x2F, nullptr, 0);
}

//  *********
//  TextEvent
//  *********

TextEvent::TextEvent(MidiTrack *prntTrk, u32 absoluteTime, std::string str)
    : MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_LOWEST), text(std::move(str)) {
}

u32 TextEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  return writeMetaTextEvent(buf, time, 0x01, text);
}

//  ************
//  SeqNameEvent
//  ************

SeqNameEvent::SeqNameEvent(MidiTrack *prntTrk, u32 absoluteTime, std::string str)
    : MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_LOWEST), text(std::move(str)) {
}

u32 SeqNameEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  return writeMetaTextEvent(buf, time, 0x03, text);
}

//  **************
//  TrackNameEvent
//  **************

TrackNameEvent::TrackNameEvent(MidiTrack *prntTrk, u32 absoluteTime, std::string str)
    : MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_LOWEST), text(std::move(str)) {
}

u32 TrackNameEvent::writeEvent(std::vector<u8> &buf, u32 time) {
  return writeMetaTextEvent(buf, time, 0x03, text);
}

//***************
// SPECIAL EVENTS
//***************

//  **************
//  TransposeEvent
//  **************


GlobalTransposeEvent::GlobalTransposeEvent(MidiTrack *prntTrk, u32 absoluteTime, s8 theSemitones)
    : MidiEvent(prntTrk, absoluteTime, 0, PRIORITY_HIGHEST) {
  semitones = theSemitones;
}

u32 GlobalTransposeEvent::writeEvent(std::vector<u8>& /*buf*/, u32 time) {
  this->prntTrk->parentSeq->globalTranspose = this->semitones;
  return time;
}
