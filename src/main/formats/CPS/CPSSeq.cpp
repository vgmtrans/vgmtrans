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
  HasMonophonicTracks();
  AlwaysWriteInitialVol(127);
  AlwaysWriteInitialMonoMode();
  // Until we add CPS3 vibrato and pitch bend using markers, set the default pitch bend range here
  if (fmt_version >= VER_200)
    AlwaysWriteInitialPitchBendRange(12, 0);
}

CPSSeq::~CPSSeq(void) {
}

bool CPSSeq::GetHeaderInfo(void) {
  // for 100% accuracy, we'd left shift by 256, but that seems unnecessary and excessive

  if (fmt_version >= VER_200)
    SetPPQN(0x30);
  else
    SetPPQN(0x30 << 4);
  return true;        //successful
}


bool CPSSeq::GetTrackPointers() {
  // CPS1 games sometimes have this set. Suggests 4 byte seq.
  // Oddly, some tracks have first byte set to 0x92 in D&D Shadow Over Mystara
  if ((GetByte(dwOffset) & 0x80) > 0)
    return false;

  this->AddHeader(dwOffset, 1, "Sequence Flags");
  VGMHeader *header = this->AddHeader(dwOffset + 1, GetShortBE(dwOffset + 1) - 1, "Track Pointers");

  const int maxTracks = fmt_version <= VER_CPS1_502 ? 12 : 16;

  for (int i = 0; i < maxTracks; i++) {
    uint32_t offset = GetShortBE(dwOffset + 1 + i * 2);
    if (offset == 0) {
      header->AddSimpleItem(dwOffset + 1 + (i * 2), 2, "No Track");
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
    header->AddSimpleItem(dwOffset + 1 + (i * 2), 2, "Track Pointer");
  }
  if (aTracks.size() == 0)
    return false;

  return true;
}

bool CPSSeq::PostLoad() {
  if (readMode != READMODE_CONVERT_TO_MIDI)
    return true;

  // We need to add pitch bend events for vibrato, which is controlled by a software LFO
  //  This is actually a bit tricky because the LFO is running independent of the sequence
  //  tempo.  It gets updated (250/4) times a second, always.  We will have to convert
  //  ticks in our sequence into absolute elapsed time, which means we also need to keep
  //  track of any tempo events that change the absolute time per tick.
  std::vector<MidiEvent *> tempoEvents;
  std::vector<MidiTrack *> &miditracks = midi->aTracks;

  // First get all tempo events, we assume they occur on track 1
  for (unsigned int i = 0; i < miditracks[0]->aEvents.size(); i++) {
    MidiEvent *event = miditracks[0]->aEvents[i];
    if (event->GetEventType() == MIDIEVENT_TEMPO)
      tempoEvents.push_back(event);
  }

  // For each track, gather all vibrato events, lfo events, pitch bend events and track end events
  for (unsigned int i = 0; i < miditracks.size(); i++) {
    std::vector<MidiEvent *> events(tempoEvents);
    MidiTrack *track = miditracks[i];
    int channel = this->aTracks[i]->channel;

    for (unsigned int j = 0; j < track->aEvents.size(); j++) {
      MidiEvent *event = miditracks[i]->aEvents[j];
      MidiEventType type = event->GetEventType();
      if (type == MIDIEVENT_MARKER || type == MIDIEVENT_PITCHBEND || type == MIDIEVENT_ENDOFTRACK)
        events.push_back(event);
    }
    // We need to sort by priority so that vibrato events get ordered correctly.  Since they cause the
    //  pitch bend range to be set, they need to occur before pitchbend events.

    // First, sort all the events by priority
    stable_sort(events.begin(), events.end(), PriorityCmp());
    // Next, sort all the events by absolute time, so that delta times can be recorded correctly
    stable_sort(events.begin(), events.end(), AbsTimeCmp());

    // And now we add vibrato and pitch bend events
    const uint32_t ppqn = GetPPQN();                    // pulses (ticks) per quarter note
    const uint32_t mpLFOt = (uint32_t) ((1 / (250 / 4.0)) * 1000000);    // microseconds per LFO tick
    uint32_t mpqn = 500000;      // microseconds per quarter note - 120 bpm default
    uint32_t mpt = mpqn / ppqn;  // microseconds per MIDI tick
    short pitchbendCents = 0;         // pitch bend in cents
    uint32_t pitchbendRange = 200;    // pitch bend range in cents default 2 semitones
    double vibratoCents = 0;          // vibrato depth in cents
    uint16_t tremelo = 0;        // tremelo depth.  we divide this value by 0x10000 to get percent amplitude attenuation
    uint16_t lfoRate = 0;        // value added to lfo env every lfo tick
    uint32_t lfoVal = 0;         // LFO envelope value. 0 - 0xFFFFFF .  Effective envelope range is -0x1000000 to +0x1000000
    int lfoStage = 0;            // 0 = rising from mid, 1 = falling from peak, 2 = falling from mid 3 = rising from bottom
    short lfoCents = 0;          // cents adjustment from most recent LFO val excluding pitchbend.
    long effectiveLfoVal = 0;
    uint32_t startAbsTicks = 0;        // The MIDI tick time to start from for a given vibrato segment

    size_t numEvents = events.size();
    for (size_t j = 0; j < numEvents; j++) {
      MidiEvent *event = events[j];
      uint32_t curTicks = event->AbsTime;            //current absolute ticks

      if (curTicks > 0 /*&& (vibrato > 0 || tremelo > 0)*/ && startAbsTicks < curTicks) {
        long segmentDurTicks = curTicks - startAbsTicks;
        double segmentDur = segmentDurTicks * mpt;    // duration of this segment in micros
        double lfoTicks = segmentDur / (double) mpLFOt;
        double numLfoPhases = (lfoTicks * (double) lfoRate) / (double) 0x20000;
        double lfoRatePerMidiTick = (numLfoPhases * 0x20000) / (double) segmentDurTicks;

        const uint8_t tickRes = 16;
        uint32_t lfoRatePerLoop = (uint32_t) ((tickRes * lfoRatePerMidiTick) * 256);

        for (int t = 0; t < segmentDurTicks; t += tickRes) {
          lfoVal += lfoRatePerLoop;
          if (lfoVal > 0xFFFFFF) {
            lfoVal -= 0x1000000;
            lfoStage = (lfoStage + 1) % 4;
          }
          effectiveLfoVal = lfoVal;
          if (lfoStage == 1)
            effectiveLfoVal = 0x1000000 - (long)lfoVal;
          else if (lfoStage == 2)
            effectiveLfoVal = -((long) lfoVal);
          else if (lfoStage == 3)
            effectiveLfoVal = -0x1000000 + (long)lfoVal;

          double lfoPercent = (effectiveLfoVal / (double) 0x1000000);

          if (vibratoCents > 0) {
            lfoCents = (short) (lfoPercent * vibratoCents);
            track->InsertPitchBend(channel,
                                   (short) (((lfoCents + pitchbendCents) / (double) pitchbendRange) * 8192),
                                   startAbsTicks + t);
//            printf("lfoCents: %d  pitchbendCents: %d  range: %d  value: %d\n", lfoCents, pitchbendCents, pitchbendRange,
//                   (short) (((lfoCents + pitchbendCents) / (double) pitchbendRange) * 8192));
          }

          if (tremelo > 0) {
            uint8_t expression = ConvertPercentAmpToStdMidiVal((0x10000 - (tremelo * fabs(lfoPercent))) / (double) 0x10000);
            track->InsertExpression(channel, expression, startAbsTicks + t);
          }
        }
        // TODO add adjustment for segmentDurTicks % tickRes
      }

//      uint32_t fmtPitchBendRange = fmt_version != VER_201B ? 50 : 1200;
      uint32_t fmtPitchBendRange = 50;

      switch (event->GetEventType()) {
        case MIDIEVENT_TEMPO: {
          TempoEvent *tempoevent = (TempoEvent *) event;
          mpqn = tempoevent->microSecs;
          mpt = mpqn / ppqn;
          break;
        }

        case MIDIEVENT_MARKER: {
          MarkerEvent *marker = (MarkerEvent *) event;

          // A vibrato event, it will provide us the frequency range of the lfo
          if (marker->name == "vibrato") {
            vibratoCents = vibrato_depth_table[marker->databyte1] * (100 / 256.0);
            uint8_t pitchBendRangeMSB = static_cast<uint8_t>(ceil(static_cast<double>(vibratoCents + fmtPitchBendRange) / 100.0));
            pitchbendRange = pitchBendRangeMSB * 100;
            printf("Vibrato byte: %X  Vibrato cents: %f  Converted to range: %d in cents\n", marker->databyte1, vibratoCents, pitchbendRange);

            // Fluidsynth does not support pitch bend range LSB, so we'll round up the value to the
            // nearest MSB. This doesn't really matter, as we calculate pitch bend in absolute terms
            // (cents) and we're passing 14 bit resolution pitch bend events. It's arguably more
            // elegant to normalize pitch bend range to semitone units anyway.
            track->InsertPitchBendRange(channel, pitchBendRangeMSB, 0, curTicks);
            lfoCents = (short) ((effectiveLfoVal / (double) 0x1000000) * vibratoCents);

            if (curTicks > 0)
              track->InsertPitchBend(channel,
                                     (short) (((lfoCents + pitchbendCents) / (double) pitchbendRange) * 8192),
                                     curTicks);
          }
          else if (marker->name == "tremelo") {
            tremelo = tremelo_depth_table[marker->databyte1];
            if (tremelo == 0)
              track->InsertExpression(channel, 127, curTicks);
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
              track->InsertPitchBend(channel, (short) (((0 + pitchbendCents) / (double) pitchbendRange) * 8192), curTicks);
            if (tremelo > 0)
              track->InsertExpression(channel, 127, curTicks);
          }
          else if (marker->name == "pitchbend") {
            pitchbendCents = (short) (((int8_t) marker->databyte1 / 128.0) * fmtPitchBendRange);
            track->InsertPitchBend(channel, (short) (((lfoCents + pitchbendCents) / (double) pitchbendRange) * 8192), curTicks);
          }
          break;
        }

        default:
          break;
      }
      startAbsTicks = curTicks;
    }
  }


  return VGMSeq::PostLoad();
}
