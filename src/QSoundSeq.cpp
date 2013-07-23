#include "stdafx.h"
#include "QSoundSeq.h"
#include "QSoundInstr.h"
#include "QSoundFormat.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"

DECLARE_FORMAT(QSound);

const BYTE delta_table[3][7] = { 2, 4, 8, 0x10, 0x20, 0x40, 0x80,
								  3, 6, 0xC, 0x18, 0x30, 0x60, 0xC0,
								  0, 9, 0x12, 0x24, 0x48, 0x90, 0 };
//octave_table provides the note value for the start of each octave.
//wholly unnecessary for me include it, but i'm following the original driver code verbatim for now
const BYTE octave_table[] = { 0x00, 0x0C, 0x18, 0x24, 0x30, 0x3C, 0x48, 0x54,
						   0x18, 0x24, 0x30, 0x3C, 0x48, 0x54, 0x60, 0x6C };

const WORD vol_table[128] = {
	0, 0xA, 0x18, 0x26, 0x34, 0x42, 0x51, 0x5F, 0x6E, 0x7D, 0x8C, 0x9B, 0xAA,
	0xBA, 0xC9, 0xD9, 0xE9, 0xF8, 0x109, 0x119, 0x129, 0x13A, 0x14A, 0x15B,
	0x16C, 0x17D, 0x18E, 0x1A0, 0x1B2, 0x1C3, 0x1D5, 0x1E8, 0x1FC, 0x20D, 0x21F,
	0x232, 0x245, 0x259, 0x26C, 0x280, 0x294, 0x2A8, 0x2BD, 0x2D2, 0x2E7, 0x2FC,
	0x311, 0x327, 0x33D, 0x353, 0x36A, 0x381, 0x398, 0x3B0, 0x3C7, 0x3DF, 0x3F8,
	0x411, 0x42A, 0x443, 0x45D, 0x477, 0x492, 0x4AD, 0x4C8, 0x4E4, 0x501, 0x51D,
	0x53B, 0x558, 0x577, 0x596, 0x5B5, 0x5D5, 0x5F5, 0x616, 0x638, 0x65A, 0x67D,
	0x6A1, 0x6C5, 0x6EB, 0x711, 0x738, 0x75F, 0x788, 0x7B2, 0x7DC, 0x808, 0x834,
	0x862, 0x891, 0x8C2, 0x8F3, 0x927, 0x95B, 0x991, 0x9C9, 0xA03, 0xA3F,
	0xA7D, 0xABD, 0xAFF, 0xB44, 0xB8C, 0xBD7, 0xC25, 0xC76, 0xCCC,
	0xD26, 0xD85, 0xDE9, 0xE53, 0xEC4, 0xF3C, 0xFBD, 0x1048, 0x10DF,
	0x1184, 0x123A, 0x1305, 0x13EA, 0x14F1, 0x1625, 0x179B, 0x1974, 0x1BFB, 0x1FFD
};

// Note that 0x100 = 1 semitone.  so the lower byte represents x / 256 cents
const WORD vibrato_depth_table[128] = {
	0x0, 0xC, 0xD, 0xE, 0xE, 0xF, 0x10, 0x11, 0x12, 0x12, 0x13, 0x14, 0x15,
	0x16, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1C, 0x1D, 0x1F, 0x20, 0x22, 0x24, 0x25,
	0x27, 0x29, 0x2A, 0x2C, 0x2D, 0x2F, 0x31, 0x32, 0x34, 0x38, 0x3B, 0x3E, 0x41,
	0x45, 0x48, 0x4B, 0x4E, 0x52, 0x55, 0x58, 0x5B, 0x5F, 0x62, 0x65, 0x6A, 0x72,
	0x7A, 0x82, 0x8A, 0x92, 0x9A, 0xA2, 0xAA, 0xB2, 0xBA, 0xC2, 0xCA, 0xD2,
	0xDA, 0xE2, 0xEA, 0xF2, 0xFA, 0x104, 0x114, 0x124, 0x134, 0x144, 0x154,
	0x164, 0x174, 0x184, 0x194, 0x1A4, 0x1B4, 0x1C4, 0x1D4, 0x1E4, 0x1F4, 0x208,
	0x228, 0x248, 0x268, 0x288, 0x2A8, 0x2C8, 0x2E8, 0x308, 0x328, 0x348, 0x368,
	0x388, 0x3A8, 0x3C8, 0x3E8, 0x410, 0x450, 0x490, 0x4D0, 0x510, 0x550, 0x590,
	0x5D0, 0x610, 0x650, 0x690, 0x6D0, 0x710, 0x750, 0x790, 0x7D0, 0x828, 0x888,
	0x8E8, 0x948, 0x9A8, 0xA08, 0xA68, 0xAC8, 0xB28, 0xB88, 0xBE8
};

const WORD tremelo_depth_table[128] = {
	0, 0x1B9, 0x2B5, 0x3B1, 0x4AD, 0x5A9, 0x6A5, 0x7A1, 0x89D, 0x999, 0xA95,
	0xB91, 0xC8D, 0xD89, 0xE85, 0xF81, 0x107D, 0x1179, 0x1275, 0x1371,
	0x146D, 0x1569, 0x1665, 0x1761, 0x185D, 0x1959, 0x1A55, 0x1B51, 0x1C4D,
	0x1D49, 0x1E45, 0x1F41, 0x203D, 0x2139, 0x2235, 0x2331, 0x242D, 0x2529,
	0x2625, 0x2721, 0x281D, 0x2919, 0x2A15, 0x2B11, 0x2C0D, 0x2D09, 0x2E05,
	0x2F01, 0x2FFD, 0x30F9, 0x31F5, 0x32F1, 0x33ED, 0x34E9, 0x35E5, 0x36E1,
	0x37DD, 0x38D9, 0x39D5, 0x3AD1, 0x3BCD, 0x3CC9, 0x3DC5, 0x3EC1, 0x40FD,
	0x42F9, 0x44F5, 0x46F1, 0x48ED, 0x4AE9, 0x4CE5, 0x4EE1, 0x50DD, 0x52D9,
	0x54D5, 0x56D1, 0x58CD, 0x5AC9, 0x5CC5, 0x5EC1, 0x60BD, 0x62B9, 0x64B5,
	0x66B1, 0x68AD, 0x6AA9, 0x6CA5, 0x6EA1, 0x709D, 0x7299, 0x7495, 0x7691,
	0x788D, 0x7A89, 0x7C85, 0x7E81, 0x827D, 0x8679, 0x8A75, 0x8E71, 0x926D,
	0x9669, 0x9A65, 0x9E61, 0xA25D, 0xA659, 0xAA55, 0xAE51, 0xB24D,
	0xB649, 0xBA45, 0xBE41, 0xC23D, 0xC639, 0xCA35, 0xCE31, 0xD22D,
	0xD629, 0xDA25, 0xDE21, 0xE21D, 0xE619, 0xEA15, 0xEE11, 0xF20D,
	0xF609, 0xFA05, 0xFE01
};

const WORD lfo_rate_table[128] = {
	0, 0x118, 0x126, 0x133, 0x142, 0x150, 0x15D, 0x16C, 0x179, 0x187, 0x196,
	0x1A3, 0x1B1, 0x1C0, 0x1DB, 0x1F7, 0x213, 0x22F, 0x24B, 0x267, 0x283, 0x29F,
	0x2BB, 0x2D7, 0x2F3, 0x30F, 0x32B, 0x347, 0x363, 0x37F, 0x3B7, 0x3EF, 0x427,
	0x45F, 0x496, 0x4CF, 0x506, 0x53E, 0x576, 0x5AE, 0x5E6, 0x61E, 0x656, 0x68E,
	0x6C6, 0x6FE, 0x76E, 0x7DE, 0x84D, 0x8BD, 0x92D, 0x99D, 0xA0D, 0xA7D,
	0xAEC, 0xB5C, 0xBCC, 0xC3C, 0xCAC, 0xD1C, 0xD8C, 0xDFB, 0xEDB,
	0xFBB, 0x109B, 0x117A, 0x125A, 0x133A, 0x141A, 0x14F9, 0x15D9, 0x16B9,
	0x1799, 0x1878, 0x1958, 0x1A38, 0x1B17, 0x1BF7, 0x1DB6, 0x1F76, 0x2135,
	0x22F5, 0x24B4, 0x2674, 0x2833, 0x29F3, 0x2BB2, 0x2D71, 0x2F31, 0x30F0,
	0x32B0, 0x346F, 0x362F, 0x37EE, 0x3B6D, 0x3EEC, 0x426B, 0x45EA, 0x4969,
	0x4CE8, 0x5066, 0x53E5, 0x5764, 0x5AE3, 0x5E62, 0x61E1, 0x6550, 0x68DE,
	0x6C5E, 0x6FDC, 0x76DA, 0x7DD8, 0x84D6, 0x8BD3, 0x92D1, 0x99CF, 0xA0CD,
	0xA7CB, 0xAEC8, 0xB5C6, 0xBCC4, 0xC3C2, 0xCABF, 0xD1BD, 0xD8BB,
	0xDFB9, 0xEDB4, 0xFBB0
};


// *********
// QSoundSeq
// *********

QSoundSeq::QSoundSeq(RawFile* file, ULONG offset, QSoundVer fmtVersion, wstring& name)
: VGMSeq(QSoundFormat::name, file, offset, 0, name), 
  fmt_version(fmtVersion)
{
	HasMonophonicTracks();
	AlwaysWriteInitialVol(127);
}

QSoundSeq::~QSoundSeq(void)
{
}

int QSoundSeq::GetHeaderInfo(void)
{
	// for 100% accuracy, we'd left shift by 256, but that seems unnecessary and excessive
	SetPPQN(0x30 << 4);
	return true;		//successful
}


int QSoundSeq::GetTrackPointers(void)
{
	// Hack for D&D Shadow over Mystara...
	if (GetByte(dwOffset) == 0x92)
		return false;

	this->AddHeader(dwOffset, 1, L"Sequence Flags");
	VGMHeader* header = this->AddHeader(dwOffset+1, GetShortBE(dwOffset+1) - 1, L"Track Pointers");

	for (int i=0; i<16; i++)
	{
		UINT offset = GetShortBE(dwOffset+1+i*2);
		if (offset == 0)
		{
			header->AddSimpleItem(dwOffset+1 + (i*2), 2, L"No Track");
			continue;
		}
		//if (GetShortBE(offset+dwOffset) == 0xE017)	//Rest, EndTrack (used by empty tracks)
		//	continue;
		QSoundTrack *newTrack = new QSoundTrack(this, offset+dwOffset);
		aTracks.push_back(newTrack);
		header->AddSimpleItem(dwOffset+1 + (i*2), 2, L"Track Pointer");
	}
	if (aTracks.size() == 0)
		return false;

	return true;
}

//int QSoundSeq::LoadTracks(void)
//{
//	for (UINT i=0; i<nNumTracks; i++)
//	{
//		if (!aTracks[i]->LoadTrack(i, 0xFFFFFFFF))
//			return false;
//	}
//	aTracks[0]->InsertTimeSig(0, 0, 4, 4, GetPPQN(), 0);
//}

int QSoundSeq::PostLoad()
{
	if (readMode != READMODE_CONVERT_TO_MIDI)
		return true;

	// We need to add pitch bend events for vibrato, which is controlled by a software LFO
	//  This is actually a bit tricky because the LFO is running independent of the sequence
	//  tempo.  It gets updated (251/4) times a second, always.  We will have to convert 
	//  ticks in our sequence into absolute elapsed time, which means we also need to keep
	//  track of any tempo events that change the absolute time per tick.
	vector<MidiEvent*> tempoEvents;
	vector<MidiTrack*>& miditracks = midi->aTracks;

	// First get all tempo events, we assume they occur on track 1
	for (unsigned int i = 0; i < miditracks[0]->aEvents.size(); i++)
	{
		MidiEvent* event = miditracks[0]->aEvents[i];
		if (event->GetEventType() == MIDIEVENT_TEMPO)
			tempoEvents.push_back(event);
	}

	// Now for each track, gather all vibrato events, lfo events, pitch bend events and track end events
	for (unsigned int i = 0; i < miditracks.size(); i++)
	{
		vector<MidiEvent*> events(tempoEvents);
		MidiTrack* track = miditracks[i];
		int channel = this->aTracks[i]->channel;

		for (unsigned int j = 0; j < track->aEvents.size(); j++)
		{
			MidiEvent* event = miditracks[i]->aEvents[j];
			MidiEventType type = event->GetEventType();
			if (type == MIDIEVENT_MARKER || type == MIDIEVENT_PITCHBEND || type == MIDIEVENT_ENDOFTRACK)
				events.push_back(event);
		}
		// We need to sort by priority so that vibrato events get ordered correctly.  Since they cause the
		//  pitch bend range to be set, they need to occur before pitchbend events.
		stable_sort(events.begin(), events.end(), PriorityCmp());	//Sort all the events by priority
		stable_sort(events.begin(), events.end(), AbsTimeCmp());	//Sort all the events by absolute time, so that delta times can be recorded correctly
		

		// And now we actually add vibrato and pitch bend events
		const ULONG ppqn = GetPPQN();					// pulses (ticks) per quarter note
		const ULONG mpLFOt = (ULONG)((1/(251/4.0)) * 1000000);	// microseconds per LFO tick
		ULONG mpqn = 500000;							// microseconds per quarter note - 120 bpm default
		ULONG mpt = mpqn / ppqn;						// microseconds per MIDI tick
		short pitchbend = 0;							// pitch bend in cents
		int pitchbendRange = 200;						// pitch bend range in cents default 2 semitones
		double vibrato = 0;								// vibrato depth in cents
		WORD tremelo = 0;								// tremelo depth.  we divide this value by 0x10000 to get percent amplitude attenuation
		WORD lfoRate = 0;								// value added to lfo env every lfo tick
		U32 lfoVal = 0;									// LFO envelope value. 0 - 0xFFFFFF .  Effective envelope range is -0x1000000 to +0x1000000
		int lfoStage = 0;								// 0 = rising from mid, 1 = falling from peak, 2 = falling from mid 3 = rising from bottom
		short lfoCents = 0;								// cents adjustment from most recent LFO val excluding pitchbend.
		long effectiveLfoVal = 0;
		//bool bLfoRising = true;;						// is LFO rising or falling?
		
		ULONG startAbsTicks = 0;							// The MIDI tick time to start from for a given vibrato segment

		int numEvents = events.size();
		for (int j = 0; j < numEvents; j++)
		{
			MidiEvent* event = events[j];
			ULONG curTicks = event->AbsTime;			//current absolute ticks
			
			if (curTicks > 0 /*&& (vibrato > 0 || tremelo > 0)*/ && startAbsTicks < curTicks)
			{
				// if we're starting a fresh vibrato segment, let's start the LFO env at 0 and set it to rise
				/*if (prevVibrato == 0 && prevTremelo == 0)
				{
					lfoVal = 0;
					lfoStage = 0;
				}*/
				
				long segmentDurTicks = curTicks - startAbsTicks;
				double segmentDur = segmentDurTicks * mpt;	// duration of this segment in micros
				double lfoTicks = segmentDur / (double)mpLFOt;
				double numLfoPhases = (lfoTicks * (double)lfoRate) / (double)0x20000;
				//double midiTicksPerPhase = (curTicks-startAbsTicks) / numLfoPhases;
				//double centsPerMidiTick = vibrato / midiTicksPerPhase;
				double lfoRatePerMidiTick = (numLfoPhases * 0x20000) / (double)segmentDurTicks;

				const BYTE tickRes = 16;
				U32 lfoRatePerLoop = (U32)((tickRes * lfoRatePerMidiTick) * 256);

				for (int t = 0; t < segmentDurTicks; t += tickRes)
				{
					lfoVal += lfoRatePerLoop;
					if (lfoVal > 0xFFFFFF)
					{
						lfoVal -= 0x1000000;
						lfoStage = (lfoStage+1) % 4;
					}
					effectiveLfoVal = lfoVal;
					if (lfoStage == 1)
						effectiveLfoVal = 0x1000000 - lfoVal;
					else if (lfoStage == 2)
						effectiveLfoVal = -((long)lfoVal);
					else if (lfoStage == 3)
						effectiveLfoVal = -0x1000000 + lfoVal;

					double lfoPercent = (effectiveLfoVal / (double)0x1000000);

					if (vibrato > 0)
					{
						lfoCents = (short)(lfoPercent * vibrato);
						track->InsertPitchBend(channel, (short)(((lfoCents + pitchbend) / (double)pitchbendRange) * 8192), startAbsTicks + t);
					}

					if (tremelo > 0)
					{
						BYTE expression = ConvertPercentAmpToStdMidiVal((0x10000 - (tremelo*abs(lfoPercent))) / (double)0x10000);
						track->InsertExpression(channel, expression, startAbsTicks + t);
					}
				}
				// TODO add adjustment for segmentDurTicks % tickRes						
			}


			switch(event->GetEventType())
			{
			case MIDIEVENT_TEMPO:
				{
					TempoEvent* tempoevent = (TempoEvent*)event;
					mpqn = tempoevent->microSecs;
					mpt = mpqn / ppqn;
				}
				break;
			case MIDIEVENT_ENDOFTRACK:

				break;
			case MIDIEVENT_MARKER:
				{
					MarkerEvent* marker = (MarkerEvent*)event;
					if (marker->name == "vibrato")
					{
						vibrato = vibrato_depth_table[marker->databyte1] * (100/256.0);
						//pitchbendRange = max(200, (vibrato + 50));		//50 cents to allow for pitchbend values, which range -50/+50
						pitchbendRange = (int)max(200, ceil((vibrato+50)/100.0)*100);	//+50 cents to allow for pitchbend values, which range -50/+50
						track->InsertPitchBendRange(channel, pitchbendRange/100, pitchbendRange%100, curTicks);
						
						lfoCents = (short)((effectiveLfoVal / (double)0x1000000) * vibrato);
						
						if (curTicks > 0)
							track->InsertPitchBend(channel, (short)(((lfoCents + pitchbend) / (double)pitchbendRange) * 8192), curTicks);
					}
					else if (marker->name == "tremelo")
					{
						tremelo = tremelo_depth_table[marker->databyte1];
						if (tremelo == 0)
							track->InsertExpression(channel, 127, curTicks);
					}
					else if (marker->name == "lfo")
					{
						lfoRate = lfo_rate_table[marker->databyte1];
					}
					else if (marker->name == "resetlfo")
					{
						if (marker->databyte1 != 1)
							break;
						lfoVal = 0;
						effectiveLfoVal = 0;
						lfoStage = 0;
						lfoCents = 0;
						if (vibrato > 0)
							track->InsertPitchBend(channel, (short)(((0 + pitchbend) / (double)pitchbendRange) * 8192), curTicks);
						if (tremelo > 0)
							track->InsertExpression(channel, 127, curTicks);
					}
					else if (marker->name == "pitchbend")
					{
						pitchbend = (short)(((char)marker->databyte1 / 256.0) * 100);
						//stringstream stream;
						//stream << "cents: " << cents << "  finalval: " <<  (cents / (double)pitchbendRange) * 8192 << "\n";
						//ATLTRACE(stream.str().c_str());
						track->InsertPitchBend(channel, (short)(((lfoCents + pitchbend) / (double)pitchbendRange) * 8192), curTicks);
					}
				}
				break;
			}
			startAbsTicks = curTicks;

		}
	}

	

	return VGMSeq::PostLoad();
}

// *************
// QSoundTrack
// *************


QSoundTrack::QSoundTrack(QSoundSeq* parentSeq, long offset, long length)
: SeqTrack(parentSeq, offset, length)
{
	ResetVars();
}

void QSoundTrack::ResetVars()
{
	dur = 0xFF;		// verified in progear (required there as well)
	bPrevNoteTie = 0;
	prevTieNote = 0;
	origTieNote = 0;
	curDeltaTable = 0;
	noteState = 0;
	bank = 0;
	memset(loop, 0, sizeof(loop));
	memset(loopOffset, 0, sizeof(loopOffset));
	SeqTrack::ResetVars();
}


int QSoundTrack::ReadEvent(void)
{
	ULONG beginOffset = curOffset;
	BYTE status_byte = GetByte(curOffset++);

	UINT delta;

	if (status_byte >= 0x20)
	{
		// Super Puzzle Fighter 2 Turbo appears to be using very unique driver
		if (this->GetVersion() == VER_201B && status_byte == 0xFF)
		{
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			return false;
		}

		if ( (noteState & 0x30) == 0)
			curDeltaTable = 1;
		//else if (noteState & 0x20)
		else if ((noteState & 0x10) == 0)
			curDeltaTable = 0;
		else
		{
			noteState &= ~(1 << 4);		//RES 4		at 0xBD6 in sfa2
			curDeltaTable = 2;
		}

		//effectively, use the highest 3 bits of the status byte as index to delta_table.
		//those highest 3 bits are used as though lowest 3 bits, of course
		delta = delta_table[curDeltaTable][((status_byte>>5)&7)-1];	// this code starts at 0xBB3 in sfa2
		delta <<= 4;
		//rest_time += value1;
		

		if ((status_byte & 0x1F) != 0)		//if it's not a rest
		{
			//UINT absDur = (double)(delta/256.0)*(double)dur;
			// for 100% accuracy, we'd be shifting by 8, but that seems excessive for MIDI
			UINT absDur = (UINT)((double)(delta/(double)(256<<4))*(double)(dur<<4));

			key = (status_byte & 0x1F) + octave_table[noteState & 0x0F] - 1;

			if ((noteState&0x40) > 0) // if tie
			{
				//AddDelta(absDur);
				//MakePrevDurNoteEnd();
				//delta -= absDur;
				if (!bPrevNoteTie)
				{
					//AddPortamentoNoItem(true);
					AddNoteOn(beginOffset, curOffset-beginOffset, key, 127, L"Note On (tied / with portamento)");
					origTieNote = key;
				}
				else if (key != prevTieNote)
				{
					AddNoteOffNoItem(0);
					AddNoteOn(beginOffset, curOffset-beginOffset, key, 127, L"Note On (tied)");
				}
				else
					AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Tie"), NULL, CLR_NOTEON);
				bPrevNoteTie = true;
				prevTieNote = key;
			}
			else
			{
				
				//if (pMidiTrack->prevDurNoteOff && pMidiTrack->prevDurNoteOff->AbsTime > GetDelta())
				//	MakePrevDurNoteEnd();
				//BYTE convVel = ConvertPercentVolToAttenDB(vel_table[vel] / (double)vel_table[sizeof(vel_table)-1]);
				if (bPrevNoteTie)
				{
					if (key != prevTieNote)
					{
						AddNoteOffNoItem(0);
						//AddPortamentoNoItem(false);
						AddNoteByDur(beginOffset, curOffset-beginOffset, key, 127, absDur);
					}
					else
					{
						AddDelta(absDur);
						delta -= absDur;
						AddNoteOff(beginOffset, curOffset-beginOffset, 0, L"Note Off (tied)");
					}
				}
				else
					AddNoteByDur(beginOffset, curOffset-beginOffset, key, 127, absDur);
				bPrevNoteTie = false;
			}
		}
		else			//it's a rest
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Rest"), NULL, CLR_REST);
		AddDelta(delta);
	}
	else
	{
		int loopNum;
		switch (status_byte)
		{
		case 0x00 :
			noteState ^= 0x20;
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Note State xor 0x20 (change duration table)"), NULL, CLR_CHANGESTATE);
			break;
		case 0x01 :
			noteState ^= 0x40;
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Note State xor 0x40 (Toggle tie)"), NULL, CLR_CHANGESTATE);
			break;
		case 0x02 :
			noteState |= (1 << 4);
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Note State |= 0x10 (change duration table)"), NULL, CLR_CHANGESTATE);
			break;
		case 0x03 :
			noteState ^= 8;
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Note State xor 8 (change octave)"), NULL, CLR_CHANGESTATE);
			break;
		case 0x04 :
			noteState &= 0x97;
			noteState |= GetByte(curOffset++);
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Change Note State (& 0x97)"), NULL, CLR_CHANGESTATE);
			break;
		case 0x05 :
			{
				QSoundVer fmt_version = ((QSoundSeq*)parentSeq)->fmt_version;
				//if (isEqual(fmt_version, 1.71))
				
				if (((QSoundSeq*)parentSeq)->fmt_version >= VER_140){
					// This byte is clearly the desired BPM, however there is a loss of resolution when the driver
					// converts this value because it is represented with 16 bits... See the table in sfa3 at 0x3492.
					// I've decided to keep the desired BPM rather than use the exact tempo value from the table
					BYTE tempo = GetByte(curOffset++);
					AddTempoBPM(beginOffset, curOffset-beginOffset, tempo);
				}
				else
				{
					// In versions < 1.40, the tempo value is subtracted from the remaining delta duration every tick.
					// We know the ppqn is 48 (0x30) because this results in clean looking MIDIs.  Internally,
					// 0x30 is left-shifted by 8 to get 0x3000 for finer resolution.
					// We also know that a tick is run 62.75 times a second, because the Z80 interrupt timer is 251 Hz, but code
					// at 0xF9 in sfa2 shows it runs once every 4 ticks (251/4 = 62.75).  There are 0.5 seconds in a beat at 120bpm.
					// So, to get the tempo value that would equal 120bpm...
					// 62.75*0.5*x = 12288
					//   x = 391.6494023904382
					// We divide this by 120 to get the value we divide by to convert to BPM, or put another way:
					// (0x3000 / (62.75*0.5)) / x = 120
					//   x = 3.26375
					// So we must divide the provided tempo value by 3.26375 to get the actual BPM.  Btw, there is a table in
					// sfa3 at 0x3492 which converts a BPM index to the x value in the first equation.  It seems to confirm our math
					USHORT tempo = GetShortBE(curOffset);
					double fTempo = tempo/3.26375;
					curOffset+=2;
					AddTempoBPM(beginOffset, curOffset-beginOffset, fTempo);
					//RecordMidiSetTempoBPM(current_delta_time, flValue1, hFile);
				}
			}
			break;
		case 0x06 :
			dur = GetByte(curOffset++);
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Set Duration"), NULL, CLR_CHANGESTATE);
			break;
		case 0x07 :
			vol = GetByte(curOffset++);
			vol = ConvertPercentAmpToStdMidiVal(vol_table[vol] / (double)0x1FFF);
			//vol = round((vol_table[vol] / (double)0x1FFF)*127.0);
			this->AddVol(beginOffset, curOffset-beginOffset, vol);
			//AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Set Volume"), NULL, BG_CLR_STEEL);
			break;
		case 0x08 :
			{
				BYTE progNum = GetByte(curOffset++);
				//if (((QSoundSeq*)parentSeq)->fmt_version < VER_116)
				{
					AddBankSelectNoItem((bank*2) + (progNum/128));
					AddProgramChange(beginOffset, curOffset-beginOffset, progNum%128);
				}
				//else
				//	AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
			}
			break;
		case 0x09 :					//effectively sets the octave
			noteState &= 0xF8;
			noteState |= GetByte(curOffset++);
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Set Octave"), NULL, CLR_CHANGESTATE);
			break;
		case 0x0A :					// Global Transpose
			{
				char globTranspose = GetByte(curOffset++);
				AddGlobalTranspose(beginOffset, curOffset-beginOffset, globTranspose);
			}
			break;
		case 0x0B :				//Transpose
			transpose = GetByte(curOffset++);
			AddTranspose(beginOffset, curOffset-beginOffset, transpose);
			break;
		case 0x0C :				//Pitch bend - only gets applied at Note-on, but don't care.  It's used mostly as a detune for chorus effect
			{
				BYTE pitchbend = GetByte(curOffset++);
				//double cents = (pitchbend / 256.0) * 100;
				AddMarker(beginOffset, curOffset-beginOffset, string("pitchbend"), pitchbend, 0, L"Pitch Bend", PRIORITY_MIDDLE, CLR_PITCHBEND);
				//AddPitchBend(beginOffset, curOffset-beginOffset, (cents / 200) * 8192);
			}
			break;
		case 0x0D :	
			{
				// Portamento: take the rate value, left shift it 1.  This value * (100/256) is increment in cents every (251/4) seconds until we hit target key.
				BYTE portamentoRate = GetByte(curOffset++);
				AddPortamentoTime(beginOffset, curOffset-beginOffset, portamentoRate);
				//AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Portamento"), NULL, CLR_PITCHBEND);
			}
			break;
		case 0x0E :				//loop
			loopNum = 0;
			goto theLoop;
		case 0x0F :
			loopNum = 1;
			goto theLoop;
		case 0x10 :
			loopNum = 2;
			goto theLoop;
		case 0x11 :
			loopNum = 3;
theLoop:	if (loop[loopNum] == 0 && loopOffset[loopNum] == 0)						//first time hitting loop
			{
				loopOffset[loopNum] = curOffset;
				loop[loopNum] = GetByte(curOffset++);	//set the number of times to loop
				bInLoop = true;
			}
			else if (loopOffset[loopNum] != curOffset)
			{
				// hack to check for infinite loop scenario in Punisher at 0xDF07: one 0E loop contains another 0E loop.
				// Also in slammast at f1a7, f1ab.. two 0E loops (song 0x26).  Actual game behavior is an infinite loop, see 0xF840 for
				// track ptrs repeating over the same tiny range.
				return false;

			}
			
			//else if (/*GetVersion() == VER_101 &&*/ loop[loopNum] > GetByte(curOffset))	// only seen in VER_101 so far (Punisher)
			//	return false;
			else										//already engaged in loop
			{
				loop[loopNum]--;						//decrement loop counter
				curOffset++;
			}

			short jump;
			if ((GetByte(curOffset) & 0x80) == 0)
				jump = -GetByte(curOffset++);
			else
			{
				jump = GetShortBE(curOffset);//(GetByte(curOffset++)<<8) + GetByte(curOffset++);
				curOffset += 2;
			}
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Loop"), NULL, CLR_LOOP);

			if (loop[loopNum] == 0)
			{
				bInLoop = false;
				loopOffset[loopNum] = 0;
			}
			else
				curOffset += jump;
			break;


			//if (loop[loopNum] == 0)
			//{
			//	bInLoop = false;
			//	curOffset+=2;
			//	AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Loop"), NULL, BG_CLR_CHEDDAR);
			//	break;
			//}



			//{
			//	short jump;
			//	if ((GetByte(curOffset) & 0x80) == 0)
			//		jump = -GetByte(curOffset++);
			//	else
			//	{
			//		jump = GetShortBE(curOffset);//(GetByte(curOffset++)<<8) + GetByte(curOffset++);
			//		curOffset += 2;
			//	}
			//	curOffset += jump;
			//}
			//break;
		case 0x12 :				
			loopNum = 0;
			goto loopBreak;
		case 0x13 :
			loopNum = 1;
			goto loopBreak;
		case 0x14 :
			loopNum = 2;
			goto loopBreak;
		case 0x15 :
			loopNum = 3;
loopBreak:	if (loop[loopNum]-1 == 0)
			{
				bInLoop = false;
				loop[loopNum] = 0;
				loopOffset[loopNum] = 0;
				noteState &= 0x97;
				noteState |= GetByte(curOffset++);
				{
					short jump = (GetByte(curOffset++)<<8) + GetByte(curOffset++);
					AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Loop Break"), NULL, CLR_LOOP);
					curOffset += jump;
				}
			}
			else
				curOffset += 3;
			break;
		case 0x16 :	// Loop Always
			{
				short jump = (GetByte(curOffset++)<<8) + GetByte(curOffset++);
				bool bResult = AddLoopForever(beginOffset, 3);
				// Add an End Track if it exists afterward, for completeness sake
				// TODO: FIX THIS SO WE DON'T ACTUALLY WRITE THE END OF TRACK EVENT TO MIDI.
				//if (GetByte(curOffset) == 0x17)
				//	AddEndOfTrack(curOffset, 1);

				curOffset += jump;
				return bResult;
			}
			break;
		case 0x17 :
			//MakePrevDurNoteEnd();
			AddEndOfTrack(beginOffset, curOffset-beginOffset);
			return false;
		case 0x18 :				// pan
			{
				//the pan value is b/w 0 and 0x20.  0 - hard left, 0x10 - center, 0x20 - hard right
				BYTE pan = GetByte(curOffset++) * 4;
				if (pan != 0)
					pan--;
				//pan *= 4;
				//if (pan == 0x80) pan = 0x7f;
				this->AddPan(beginOffset, curOffset-beginOffset, pan);
			}
			break;
		case 0x19 :
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset, L"Reg9 Event (unknown to MAME)");
			break;
		case 0x1A :	//master(?) vol
			{
				//curOffset++;
				vol = GetByte(curOffset++);
				AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Master Volume"), NULL, CLR_UNKNOWN);
				//this->AddMasterVol(beginOffset, curOffset-beginOffset, vol);
				//AddVolume(beginOffset, curOffset-beginOffset, vool);
			}
			//AddUnknown(beginOffset, curOffset-beginOffset);
			break;
		case 0x1B :				//Vibrato depth... 
			{
				BYTE vibratoDepth;
				if (GetVersion() < VER_171)
				{
					vibratoDepth = GetByte(curOffset++);
					AddMarker(beginOffset, curOffset-beginOffset, string("vibrato"), vibratoDepth, 0, _T("Vibrato"), PRIORITY_HIGH, CLR_PITCHBEND);
				}
				else
				{
					// First data byte defines behavior 0-3
					BYTE type = GetByte(curOffset++);
					BYTE data = GetByte(curOffset++);
					switch (type)
					{
					case 0:		// vibrato
						AddMarker(beginOffset, curOffset-beginOffset, string("vibrato"), data, 0, _T("Vibrato"), PRIORITY_HIGH, CLR_PITCHBEND);
						break;
					case 1:		// tremelo
						AddMarker(beginOffset, curOffset-beginOffset, string("tremelo"), data, 0, _T("Tremelo"), PRIORITY_MIDDLE, CLR_EXPRESSION);
						break;
					case 2:		// LFO rate
						AddMarker(beginOffset, curOffset-beginOffset, string("lfo"), data, 0, _T("LFO Rate"), PRIORITY_MIDDLE, CLR_LFO);
						break;
					case 3:		// LFO reset
						AddMarker(beginOffset, curOffset-beginOffset, string("resetlfo"), data, 0, _T("LFO Reset"), PRIORITY_MIDDLE, CLR_LFO);
						break;
					}
				}				
			}
			break;
		case 0x1C :
			{
				BYTE tremeloDepth;
				if (GetVersion() < VER_171)
				{
					tremeloDepth = GetByte(curOffset++);
					AddMarker(beginOffset, curOffset-beginOffset, string("tremelo"), tremeloDepth, 0, _T("Tremelo"), PRIORITY_MIDDLE, CLR_EXPRESSION);
				}
				else
				{
					// I'm not sure at all about the behavior here, need to test
					curOffset+=2;
					AddUnknown(beginOffset, curOffset-beginOffset);
				}
				
			}		
			break;
		case 0x1D :	// LFO rate (for versions < 1.71)
			{
				BYTE rate = GetByte(curOffset++);
				if (GetVersion() < VER_171)
					AddMarker(beginOffset, curOffset-beginOffset, string("lfo"), rate, 0, _T("LFO Rate"), PRIORITY_MIDDLE, CLR_LFO);
				else
					AddUnknown(beginOffset, curOffset-beginOffset, L"NOP");
			}
			break;
		case 0x1E :	// Reset LFO state (for versions < 1.71)
			{
				BYTE data = GetByte(curOffset++);
				if (GetVersion() < VER_171)
					AddMarker(beginOffset, curOffset-beginOffset, string("resetlfo"), data, 0, _T("LFO Reset"), PRIORITY_MIDDLE, CLR_LFO);
				else
					AddUnknown(beginOffset, curOffset-beginOffset, L"NOP");
			}
			break;
		case 0x1F :
			{
				U8 value = GetByte(curOffset++);
				if (((QSoundSeq*)parentSeq)->fmt_version < VER_116)
				{
					AddBankSelectNoItem(2+(value/128));
					AddProgramChange(beginOffset, curOffset-beginOffset, value%128);
				}
				else
				{
					bank = value;
					AddBankSelectNoItem(bank*2);
					AddGenericEvent(beginOffset, curOffset-beginOffset, _T("Bank Change"), NULL, CLR_PROGCHANGE);
				}
					
			}
			break;
		case 0x20 :
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;
		case 0x21 :
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;
		case 0x22 :
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;
		case 0x23 :
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;
		default :
			AddGenericEvent(beginOffset, curOffset-beginOffset, _T("UNKNOWN"), NULL, CLR_UNRECOGNIZED);
		}
	}
	return true;
}
