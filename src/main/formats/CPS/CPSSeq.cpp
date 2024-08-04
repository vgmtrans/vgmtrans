/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "CPSSeq.h"
#include "CPSTrackV1.h"
#include "CPSTrackV2.h"
#include "CPS2Instr.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"

DECLARE_FORMAT(CPS1);
DECLARE_FORMAT(CPS2);

// ******
// CPSSeq
// ******

CPSSeq::CPSSeq(RawFile *file, uint32_t offset, CPSFormatVer fmtVersion, std::string name)
    : VGMSeq(CPS2Format::name, file, offset, 0, std::move(name)),
      fmt_version(fmtVersion) {
  setUsesMonophonicTracks();
  setAlwaysWriteInitialVol(127);
  setAlwaysWriteInitialMonoMode(true);
  // Until we add CPS3 vibrato and pitch bend using markers, set the default pitch bend range here
  if (fmt_version >= VER_200)
    setAlwaysWriteInitialPitchBendRange(12 * 100);
}

CPSSeq::~CPSSeq() {
}

bool CPSSeq::parseHeader() {
  setPPQN(0x30);
  return true;
}


bool CPSSeq::parseTrackPointers() {
  // CPS1 games sometimes have this set. Suggests 4 byte seq.
  // Oddly, some tracks have first byte set to 0x92 in D&D Shadow Over Mystara
  if ((readByte(dwOffset) & 0x80) > 0)
    return false;

  this->addHeader(dwOffset, 1, "Sequence Flags");
  VGMHeader *header = this->addHeader(dwOffset + 1, readShortBE(dwOffset + 1) - 1, "Track Pointers");

  const int maxTracks = fmt_version <= VER_CPS1_502 ? 12 : 16;

  for (int i = 0; i < maxTracks; i++) {
    uint32_t offset = readShortBE(dwOffset + 1 + i * 2);
    if (offset == 0) {
      header->addChild(dwOffset + 1 + (i * 2), 2, "No Track");
      continue;
    }
    //if (GetShortBE(offset+dwOffset) == 0xE017)	//Rest, EndTrack (used by empty tracks)
    //	continue;

    SeqTrack *newTrack;

    switch (fmt_version) {
      case VER_CPS1_200:
      case VER_CPS1_200ff:
      case VER_CPS1_350:
      case VER_CPS1_425:
        newTrack = new CPSTrackV1(this, i < 8 ? CPSSynth::YM2151 : CPSSynth::OKIM6295, offset);
        break;
      case VER_200:
      case VER_201B:
      case VER_210:
      case VER_211:
      case VER_CPS3:
        newTrack = new CPSTrackV2(this, offset + dwOffset);
        break;
      default:
        newTrack = new CPSTrackV1(this, CPSSynth::QSOUND, offset + dwOffset);
    }
    aTracks.push_back(newTrack);
    header->addChild(dwOffset + 1 + (i * 2), 2, "Track Pointer");
  }
  if (aTracks.size() == 0)
    return false;

  return true;
}

bool CPSSeq::postLoad() {
  bool succeeded = VGMSeq::postLoad();

  if (readMode != READMODE_CONVERT_TO_MIDI)
    return true;

  const double UPDATE_RATE_IN_HZ = (fmt_version == VER_CPS3) ? CPS3_DRIVER_RATE_HZ : CPS2_DRIVER_RATE_HZ;

  // We need to add pitch bend events for vibrato, which is controlled by a software LFO
  //  This is actually a bit tricky because the LFO is running independent of the sequence
  //  tempo, always updating at the rate of the driver irq.  We will have to convert
  //  ticks in our sequence into absolute elapsed time, which means we also need to keep
  //  track of any tempo events that change the absolute time per tick.
  std::vector<MidiEvent *> tempoEvents;
  std::vector<MidiTrack *> &miditracks = midi->aTracks;

  // First get all tempo events, we assume they occur on track 1
  for (unsigned int i = 0; i < miditracks[0]->aEvents.size(); i++) {
    MidiEvent *event = miditracks[0]->aEvents[i];
    if (event->eventType() == MIDIEVENT_TEMPO)
      tempoEvents.push_back(event);
  }

  // For each track, gather all vibrato events, lfo events, pitch bend events and track end events
  for (unsigned int i = 0; i < miditracks.size(); i++) {
    std::vector<MidiEvent *> events(tempoEvents);
    MidiTrack *track = miditracks[i];
    int channel = this->aTracks[i]->channel;

    for (unsigned int j = 0; j < track->aEvents.size(); j++) {
      MidiEvent *event = miditracks[i]->aEvents[j];
      MidiEventType type = event->eventType();
      if (type == MIDIEVENT_MARKER || type == MIDIEVENT_PITCHBEND || type == MIDIEVENT_ENDOFTRACK)
        events.push_back(event);
    }
    // We need to sort by priority so that vibrato events get ordered correctly.  Since they cause the
    //  pitch bend range to be set, they need to occur before pitchbend events.

    // First, sort all the events by priority
    std::ranges::stable_sort(events, PriorityCmp());
    // Next, sort all the events by absolute time, so that delta times can be recorded correctly
    std::ranges::stable_sort(events, AbsTimeCmp());

    // And now we add vibrato and pitch bend events
    const uint32_t mpLFOt = static_cast<uint32_t>(1000000 / UPDATE_RATE_IN_HZ);    // microseconds per LFO tick
    uint32_t mpqn = 500000;      // microseconds per quarter note - 120 bpm default
    uint32_t mpt = mpqn / ppqn();  // microseconds per MIDI tick = microseconds per quarter / pulses per quarter note
    int16_t pitchbendCents = 0;         // pitch bend in cents
    // This represents the pitch bend range set for the MIDI track. It will change to accomodate
    // vibrato depth. When vibrato depth changes, this becomes the format's actual pitch bend range
    // (fmtPitchBendRange) plus the range of vibrato depth, ceiled to the nearest semitone.
    uint32_t pitchbendRange = fmt_version >= VER_200 ? 1200 : 200; // pitch bend range in cents.
    double vibratoCents = 0;          // vibrato depth in cents
    uint16_t tremelo = 0;        // tremelo depth.  we divide this value by 0x10000 to get percent amplitude attenuation
    uint16_t lfoRate = 0;        // value added to lfo env every lfo tick
    uint32_t lfoVal = 0;         // LFO envelope value. 0 - 0xFFFFFF .  Effective envelope range is -0x1000000 to +0x1000000
    int lfoStage = 0;            // 0 = rising from mid, 1 = falling from peak, 2 = falling from mid 3 = rising from bottom
    int16_t lfoCents = 0;          // cents adjustment from most recent LFO val excluding pitchbend.
    int32_t effectiveLfoVal = 0;
    uint32_t startAbsTicks = 0;        // The MIDI tick time to start from for a given vibrato segment

    size_t numEvents = events.size();
    for (size_t j = 0; j < numEvents; j++) {
      MidiEvent *event = events[j];
      uint32_t curTicks = event->absTime;            //current absolute ticks

      // For the span of time since the prior event, fill in any pitch and expression events that
      // should occur as a result of LFO fluctuation when vibrato and/or tremelo depth are set.
      if (curTicks > 0 /*&& (vibratoCents > 0 || tremelo > 0)*/ && startAbsTicks < curTicks) {

        // Calculate the elapsed time in this segment and determine the incremental change in the LFO per tick.
        long segmentDurTicks = curTicks - startAbsTicks;
        double segmentDur = segmentDurTicks * mpt;    // duration of this segment in micros
        double lfoTicks = segmentDur / static_cast<double>(mpLFOt);
        double numLfoPhases = (lfoTicks * static_cast<double>(lfoRate)) / 0x20000;
        double lfoRatePerMidiTick = (numLfoPhases * 0x20000) / static_cast<double>(segmentDurTicks);
        uint32_t lfoRatePerLoop = static_cast<uint32_t>(lfoRatePerMidiTick * 256);

        for (int t = 0; t < segmentDurTicks; ++t) {
          lfoVal += lfoRatePerLoop;
          if (lfoVal > 0xFFFFFF) {
            lfoVal -= 0x1000000;
            lfoStage = (lfoStage + 1) % 4;
          }
          effectiveLfoVal = lfoVal;
          if (lfoStage == 1)
            effectiveLfoVal = 0x1000000 - static_cast<int32_t>(lfoVal);
          else if (lfoStage == 2)
            effectiveLfoVal = -static_cast<int32_t>(lfoVal);
          else if (lfoStage == 3)
            effectiveLfoVal = -0x1000000 + static_cast<int32_t>(lfoVal);

          double lfoPercent = effectiveLfoVal / static_cast<double>(0x1000000);

          // If the track has enabled vibrato at this point, insert pitch bend events.
          if (vibratoCents > 0) {
            lfoCents = static_cast<int16_t>(lfoPercent * vibratoCents);
            track->insertPitchBend(channel,
                                   static_cast<int16_t>(((lfoCents + pitchbendCents) / static_cast<double>(pitchbendRange)) * 8192),
                                   startAbsTicks + t);
          }

          // If the track has enabled tremelo at this point, insert expression events.
          if (tremelo > 0) {
            uint8_t expression = convertPercentAmpToStdMidiVal((0x10000 - (tremelo * fabs(lfoPercent))) / static_cast<double>(0x10000));
            track->insertExpression(channel, expression, startAbsTicks + t);
          }
        }
      }

      uint32_t fmtPitchBendRange = fmt_version >= VER_200 ? 1200 : 50;

      // We just handled LFO-induced events in the span between the prior event up to this one. Now,
      // check if the event is a tempo or marker event, which require special handling.
      switch (event->eventType()) {
        case MIDIEVENT_TEMPO: {
          TempoEvent *tempoevent = static_cast<TempoEvent*>(event);
          mpqn = tempoevent->microSecs;
          mpt = mpqn / ppqn();
          break;
        }

        case MIDIEVENT_MARKER: {
          MarkerEvent *marker = static_cast<MarkerEvent*>(event);

          // A vibrato event, it will provide us the frequency range of the lfo
          if (marker->name == "vibrato") {
            vibratoCents = vibrato_depth_table[marker->databyte1] * (100 / 256.0);
            uint8_t pitchBendRangeMSB = static_cast<uint8_t>(ceil(static_cast<double>(vibratoCents + fmtPitchBendRange) / 100.0));
            pitchbendRange = pitchBendRangeMSB * 100;

            // Fluidsynth does not support pitch bend range LSB, so we'll round up the value to the
            // nearest MSB. This doesn't really matter, as we calculate pitch bend in absolute terms
            // (cents) and we're passing 14 bit resolution pitch bend events. It's arguably more
            // elegant to normalize pitch bend range to semitone units anyway.
            track->insertPitchBendRange(channel, pitchbendRange, curTicks);
            lfoCents = static_cast<int16_t>((effectiveLfoVal / static_cast<double>(0x1000000)) * vibratoCents);

            if (curTicks > 0)
              track->insertPitchBend(channel,
                                     static_cast<int16_t>((lfoCents + pitchbendCents) / static_cast<double>(pitchbendRange) * 8192),
                                     curTicks);
          }
          else if (marker->name == "tremelo") {
            tremelo = tremelo_depth_table[marker->databyte1];
            if (tremelo == 0)
              track->insertExpression(channel, 127, curTicks);
          }
          else if (marker->name == "lfo") {
            lfoRate = lfo_rate_table[marker->databyte1];
          }
          else if (marker->name == "resetlfo") {
            if (marker->databyte1 != 1)
              break;
            lfoVal = 0;
            effectiveLfoVal = 0;
            lfoStage = 0;
            lfoCents = 0;
            if (vibratoCents > 0)
              track->insertPitchBend(channel, static_cast<int16_t>(((0 + pitchbendCents) / static_cast<double>(pitchbendRange)) * 8192), curTicks);
            if (tremelo > 0)
              track->insertExpression(channel, 127, curTicks);
          }
          else if (marker->name == "pitchbend") {
            pitchbendCents = static_cast<int16_t>((static_cast<int8_t>(marker->databyte1) / 128.0) * fmtPitchBendRange);
            track->insertPitchBend(channel, static_cast<int16_t>((lfoCents + pitchbendCents) / static_cast<double>(pitchbendRange) * 8192), curTicks);
          }
          break;
        }

        default:
          break;
      }
      startAbsTicks = curTicks;
    }
  }

  return succeeded;
}
