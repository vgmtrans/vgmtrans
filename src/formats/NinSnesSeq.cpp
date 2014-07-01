#include "stdafx.h"
#include "NinSnesSeq.h"
#include "NinSnesFormat.h"

DECLARE_FORMAT(NinSnes);

using namespace std;

NinSnesSeq::NinSnesSeq(RawFile* file, uint32_t offset, uint32_t length, wstring name)
: VGMSeq(NinSnesFormat::name, file, offset, length, name)
{
	AddContainer<NinSnesSection>(aSections);
	if (!RemoveContainer<SeqTrack>(aTracks))
		Alert(L"Problem removing song tracks container");
}


NinSnesSeq::~NinSnesSeq()
{
	DeleteVect<NinSnesSection>(aSections);
}

//Load() - Function to load all the sequence data into the class
bool NinSnesSeq::LoadMain()
{
	SetPPQN(0x30);
	for (uint32_t i=0; i<8; i++)
		aTracks.push_back(new NinSnesTrack(this, dwOffset, i));
	if (!GetSectionPointers())
		return false;

	size_t nNumSections = aSections.size();
	if (nNumSections == 0)
		return false;

	LoadDefaultEventMap(this);

	sort(aSections.begin(), aSections.end(), ItemPtrOffsetCmp());
	if (!LoadAllSections())
		return false;

	//DeleteVect<SeqTrack>(aTracks);
	//for (uint32_t i=0; i<aTracks.size(); i++)
	//{
	//	if (aTracks[i]->aEvents.size() == 0)
	//		aTracks.erase(aTracks.begin() + i--);
	//}

	dwOffset = 0;
	unLength = vgmfile->rawfile->size();
	//for (uint32_t i=0; i<nNumSections; i++)
	//	unLength += aSections[i]->unLength;
	//unLength = 0x200;
	return true;
}

bool NinSnesSeq::GetSectionPointers()
{
	VGMHeader* PlayListHdr = AddHeader(dwOffset, 0, L"Section Play List");

	uint32_t offset;
	
	for (offset = dwOffset; GetShort(offset) >= 0x100; offset+=2)
	{
		uint16_t sectPtr = GetShort(offset);
		sectPlayList.push_back(sectPtr);
		if (!sectionMap[sectPtr])
		{
			NinSnesSection* newSect = new NinSnesSection(this, sectPtr);
			aSections.push_back(newSect);
			sectionMap[sectPtr] = newSect;
			newSect->GetHeaderInfo(sectPtr);
		}
		PlayListHdr->AddSimpleItem(offset, 2, L"Section Pointer");
	}
	playListRptPtr = GetShort(offset);
	PlayListHdr->unLength = offset - dwOffset;
	return true;		//successful
}


bool NinSnesSeq::LoadAllSections()
{
	curDelta = 0;
	int nextTime = 0;
	//initialize first section tracks (will be retained by subsequent sections)
	mvol = 0xC0;
	//for (uint32_t i=0; aTracks.size(); i++)
	//	aTracks[i]->AddMastVolNoItem(mvol);

	//for (int i=0; i<aSections[0]->aTracks.size(); i++)
	//	aSections[0]->aTracks[i]->AddMastVolNoItem(mvol);


	//for (uint32_t i=0; i<aSections.size(); i++)
	for (uint32_t i=0; i < sectPlayList.size(); i++)
	{
		NinSnesSection* section = sectionMap[sectPlayList[i]];
		section->readMode = readMode;
		nextTime = section->LoadSection(nextTime);
		if (nextTime == -1)
			return false;
		curDelta = nextTime;
	}
	//Time to create the Section individual Tracks out of the large Sequence Tracks
	for (uint32_t i=0; i < aSections.size(); i++)
	{
		for (uint32_t j=0; j < aSections[i]->aSectTracks.size(); j++)	//for every section track
		{
			NinSnesTrack* songTrack = (NinSnesTrack*)aSections[i]->aSongTracks[j];
			NinSnesTrack* sectTrack = (NinSnesTrack*)aSections[i]->aSectTracks[j];
			vector<SeqEvent*>& sectTrkEvents = sectTrack->aEvents;
			vector<SeqEvent*>& songTrkEvents = songTrack->aEvents;

			if (!songTrkEvents.empty())
				sectTrkEvents.insert(sectTrkEvents.begin(),
				songTrkEvents.begin()+sectTrack->beginEventIndex,
				songTrkEvents.begin()+sectTrack->endEventIndex+1);
		}
		
	}
	return true;
}

void NinSnesSeq::LoadDefaultEventMap(NinSnesSeq *pSeqFile)
{
	pSeqFile->META_CUTOFF = 0xE0;
	pSeqFile->NOTE_CUTOFF = 0xCA;
	pSeqFile->NOTEREST_CUTOFF = 0x80;
	pSeqFile->EventMap[0x00] = EVENT_RET;
	pSeqFile->EventMap[0xC8] = EVENT_TIE;
	pSeqFile->EventMap[0xC9] = EVENT_REST;
	pSeqFile->EventMap[0xE0] = EVENT_PROGCHANGE;
	pSeqFile->EventMap[0xE1] = EVENT_PAN;
	pSeqFile->EventMap[0xE2] = EVENT_PANFADE;
	pSeqFile->EventMap[0xE5] = EVENT_MASTVOL;
	pSeqFile->EventMap[0xE6] = EVENT_MASTVOLFADE;
	pSeqFile->EventMap[0xE7] = EVENT_TEMPO;
	pSeqFile->EventMap[0xE8] = EVENT_TEMPOFADE;
	pSeqFile->EventMap[0xE9] = EVENT_GLOBTRANSP;
	pSeqFile->EventMap[0xEA] = EVENT_TRANSP;
	pSeqFile->EventMap[0xED] = EVENT_VOL;
	pSeqFile->EventMap[0xEE] = EVENT_VOLFADE;
	pSeqFile->EventMap[0xEF] = EVENT_GOSUB;
	pSeqFile->EventMap[0xFA] = EVENT_SETPERCBASE;
	pSeqFile->EventMap[0xE4] = EVENT_UNKNOWN0;
	pSeqFile->EventMap[0xEC] = EVENT_UNKNOWN0;
	pSeqFile->EventMap[0xF3] = EVENT_UNKNOWN0;
	pSeqFile->EventMap[0xF6] = EVENT_UNKNOWN0;
	pSeqFile->EventMap[0xF0] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0xF4] = EVENT_UNKNOWN1;
	pSeqFile->EventMap[0xE3] = EVENT_UNKNOWN3;
	pSeqFile->EventMap[0xEB] = EVENT_UNKNOWN3;
	pSeqFile->EventMap[0xF1] = EVENT_UNKNOWN3;
	pSeqFile->EventMap[0xF2] = EVENT_UNKNOWN3;
	pSeqFile->EventMap[0xF5] = EVENT_UNKNOWN3;
	pSeqFile->EventMap[0xF7] = EVENT_UNKNOWN3;
	pSeqFile->EventMap[0xF8] = EVENT_UNKNOWN3;
	pSeqFile->EventMap[0xF9] = EVENT_UNKNOWN3;
}

// **************
// NinSnesSection
// **************


NinSnesSection::NinSnesSection(NinSnesSeq* prntSeq, uint32_t offset)
: VGMContainerItem(prntSeq, offset, 0, L"Section")
{
	AddContainer<SeqTrack>(aSectTracks);
}

NinSnesSection::~NinSnesSection()
{
	DeleteVect<SeqTrack>(aSectTracks);
}

bool NinSnesSection::GetHeaderInfo(uint16_t headerOffset)
{
	hdrOffset = headerOffset;
	for (int i=0; i<8; i++)
	{
		if (GetShort(hdrOffset+i*2))
		{
			uint16_t trackOffset = GetShort(hdrOffset+i*2);
			if (trackOffset >= vgmfile->rawfile->size())
				return false;
			//NinSnesTrack* newTrack = new NinSnesTrack(this, trackOffset, i);
			//aSectTracks.push_back(newTrack);
			trackOffsets.push_back(trackOffset);
			aSongTracks.push_back(((VGMSeq*)vgmfile)->aTracks[i]);
			aSectTracks.push_back(new NinSnesTrack(this, trackOffset, i));
		}
	}
	if (aSongTracks.size() == 0)
		return false;
	//sort(aTracks.begin(), aTracks.end(), ItemPtrOffsetCmp());
	dwOffset = aSongTracks[0]->dwOffset;
	return true;
}


int NinSnesSection::LoadSection(int startTime)
{
	uint32_t totalTime = 0;
	int infLoopDetOffset = 0;
	int numNothingLoops = 0;
	int result;
	int i;
	for (uint32_t j=0; j<aSongTracks.size(); j++)
	{
		NinSnesTrack* theTrack = ((NinSnesTrack*)aSongTracks[j]);
		theTrack->subcount = 0;
		theTrack->nextEventTime = 0;
		theTrack->curOffset = trackOffsets[j];
		if (this->readMode == READMODE_CONVERT_TO_MIDI)
		{
			if (((NinSnesTrack*)aSongTracks[j])->pMidiTrack == NULL)
			{
				theTrack->pMidiTrack = ((NinSnesSeq*)vgmfile)->midi->AddTrack();
				theTrack->SetChannelAndGroupFromTrkNum(theTrack->trackNum);
			}
		}
		theTrack->SetTime(((NinSnesSeq*)vgmfile)->curDelta);

		// Get Index of first event for Section Track
		((NinSnesTrack*)aSectTracks[j])->beginEventIndex =
			theTrack->aEvents.size();
	}
	for (i=0; result = ((NinSnesTrack*)aSongTracks[i])->ReadEvent(totalTime); i++)
	{	
		//int infLoopDetTotTime;
		if (result == -1)		//critical problem, stop search and destroy sequence
			return 0xFFFFFFFF;
		if (i == aSongTracks.size()-1)
		{
			
			//if (totalTime == endTime)
			//	break;
			totalTime++;
			i = -1;
			/*if (aTracks[0]->curOffset == infLoopDetOffset)
			{
				numNothingLoops++;
				if (numNothingLoops >= 5000)
					return false;
			}
			else
			{
				numNothingLoops = 0;
				infLoopDetOffset = aTracks[0]->curOffset;
			}*/
			//infLoopDetTotTime = totalTime;
		}
	}

	dwOffset = trackOffsets[0];
	unLength=0;
	for (uint32_t n=0; n<aSongTracks.size(); n++)
	{
		NinSnesTrack* theSongTrk = ((NinSnesTrack*)aSongTracks[n]);
		NinSnesTrack* theSectTrk = ((NinSnesTrack*)aSectTracks[n]);

		if (theSongTrk->subcount > 0)
			theSectTrk->unLength = theSongTrk->subret - trackOffsets[n];
		else
			theSectTrk->unLength = theSongTrk->curOffset - trackOffsets[n];
		unLength += theSectTrk->unLength;

		//Get index of last Event for Section Track
		((NinSnesTrack*)aSectTracks[n])->endEventIndex =
			theSongTrk->aEvents.size()-1;
	}

	return aSongTracks[i]->GetTime();	//return the current delta of the last track written to
}


//  ************
//  NinSnesTrack
//  ************

NinSnesTrack::NinSnesTrack(NinSnesSection* parentSect, uint32_t offset, int trackNumber)
: SeqTrack((VGMSeq*)parentSect->vgmfile, offset, 0), prntSect(parentSect), trackNum(trackNumber)
{
	bDetermineTrackLengthEventByEvent = true;
}

NinSnesTrack::NinSnesTrack(NinSnesSeq* parentSeq, uint32_t offset, int trackNumber)
: SeqTrack((VGMSeq*)parentSeq, offset, 0), prntSect(NULL), trackNum(trackNumber)
{
	bDetermineTrackLengthEventByEvent = true;
}


void NinSnesTrack::AddTime(uint32_t AddDelta)
{
	nextEventTime += AddDelta;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->AddDelta(AddDelta);
}

// TODO: SetTime - remove it, it will make things complicated.
void NinSnesTrack::SetTime(uint32_t NewDelta)
{
	time = NewDelta;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->SetDelta(NewDelta);
}

// TODO: SubtractTime - remove it, it will make things complicated.
void NinSnesTrack::SubtractTime(uint32_t SubtractDelta)
{
	nextEventTime -= SubtractDelta;
	if (readMode == READMODE_CONVERT_TO_MIDI)
		pMidiTrack->SubtractDelta(SubtractDelta);
}

void NinSnesTrack::SetPercBase(uint8_t newBase)
{
	((NinSnesSeq*)vgmfile)->percbase = newBase;
}

uint8_t NinSnesTrack::GetPercBase()
{
	return ((NinSnesSeq*)vgmfile)->percbase;
}


bool NinSnesTrack::ReadEvent(uint32_t totalTime)
{
	//if (totalTime >= parentSect->endTime)
	//	return false;
	//if (parentSect->totalTime == parentSect->endTime)
	//	return true;
	while (totalTime == nextEventTime/* || GetByte(curOffset) == 0*/)
	{
		uint32_t beginOffset = curOffset;

		if (curOffset >= 0x10000-5)
			return false;

		map<uint8_t, int>::iterator p;
		uint8_t event_type = 0;
		uint8_t status_byte = GetByte(curOffset++);
		p =  ((NinSnesSeq*)vgmfile)->EventMap.find(status_byte);
		if (p != ((NinSnesSeq*)vgmfile)->EventMap.end())
			event_type = p->second;		//found the status_byte in the map - use it
		
		if (event_type == EVENT_RET)
		{
			if (subcount > 0)
			{
				if (--subcount > 0)
				{
					bInLoop = true;
					curOffset = substart;			
					continue;
				}
				else
				{
					bInLoop = false;
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop End", NULL, CLR_LOOP);
					curOffset = subret;
					substart = 0;
					subret = 0;
					continue;
				}
			}
			else
			{
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"End of Section", NULL, CLR_LOOP);
				return false;
			}
		}
		else if (status_byte < ((NinSnesSeq*)vgmfile)->NOTEREST_CUTOFF)
		{
			dur = status_byte;
			uint8_t op1 = GetByte(curOffset++);
			if (op1 < ((NinSnesSeq*)vgmfile)->NOTEREST_CUTOFF)
			{
				durpct = durpcttbl[(op1 >> 4) & 0x7];
				vel = voltbl[op1 & 0xf] >> 1;
				status_byte = GetByte(curOffset++);
			}
			else
				status_byte = op1;
			p =  ((NinSnesSeq*)vgmfile)->EventMap.find(status_byte);
			if (p != ((NinSnesSeq*)vgmfile)->EventMap.end())
				event_type = p->second;		//found the status_byte in the map - use it

		}
		if (status_byte < ((NinSnesSeq*)vgmfile)->META_CUTOFF)//0xe0)
		{
			notedur = dur * durpct / 256;
			if (notedur > dur-2)
				notedur = dur - 2;
			if (notedur < 1)
				notedur = 1;

			if (event_type == EVENT_TIE)		//Tie
			{
				AddTime(dur);
				MakePrevDurNoteEnd();
				SubtractTime(dur);
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tie", NULL, CLR_TIE);
				//AddEventItem("Tie", ICON_CONTROL, beginOffset, curOffset-beginOffset, BG_CLR_STEEL);
				
				//	# tie
				//my ($last_ev) = grep {$_->[0] eq 'note'} reverse @{$e[$v]};
  				//$last_ev->[2] += $dur[$v];  # use full dur; already did notedur
  				//$notedesc = "Tie";
			}
			else if (event_type == EVENT_REST)
			{
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Rest", NULL, CLR_REST);
				//AddEventItem("Rest", ICON_REST, beginOffset, curOffset-beginOffset, BG_CLR_LIGHTBLUE);
			}
			else if (status_byte < ((NinSnesSeq*)vgmfile)->NOTE_CUTOFF)	//Note			//ADDRESS THIS.. NOTE ISN'T AN EVENT HERE
			{
				status_byte &= 0x7F;
				octave = status_byte/12;
				key = status_byte%12;
				uint8_t mnote = 12 * (octave + /*t+*/2) + key; //don't include transpose or globTranspose because AddNoteByDur does it	//didn't include transpose (vs transpose[v])
				AddNoteByDur(beginOffset, curOffset-beginOffset, mnote, vel, notedur);
			}
			else							//Percussion Note
			{
				int8_t mnote = /*-patchmap[*/status_byte-((NinSnesSeq*)vgmfile)->NOTE_CUTOFF;//+GetPercBase();];
				//selectMsg.Format("Drum Num: %X", mnote);
				//in this next line, we subtract globTranspose and transpose, because it is added by the AddNoteByDur function later, and we don't want them
				AddPercNoteByDur(beginOffset, curOffset-beginOffset, mnote/*-transpose-parentSeq->globTranspose*/, vel, dur);
				//AddNoteByDur(beginOffset, curOffset-beginOffset, mnote-transpose-parentSeq->globTranspose, vel, dur, 9, "Percussion Note", "Percussion Note");
			}
			AddTime(dur);
		}
		else
		{
			switch (event_type)
			{
			case EVENT_PROGCHANGE:		//Program Change
				{
					int8_t progNum = GetByte(curOffset++);
					if (progNum < 0) //>= ((FZeroSeq*)parentSeq)->NOTEREST_CUTOFF)
						progNum = progNum - ((NinSnesSeq*)vgmfile)->NOTE_CUTOFF + GetPercBase();
					
					//if (progNum < sizeof(patchmap))
					//	progNum = patchmap[progNum];
					
					AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
				}
				break;
			case EVENT_PAN:		//Pan
				{
					uint8_t pan = GetByte(curOffset++) & 0x1F;
					pan = pantbl[pan];
					AddPan(beginOffset, curOffset-beginOffset, pan);
				}
				break;
			case EVENT_PANFADE:		//Pan Fade
				{
					uint8_t dur = GetByte(curOffset++);
					uint32_t targPan = pantbl[GetByte(curOffset++)];
					AddPanSlide(beginOffset, curOffset-beginOffset, dur, (uint8_t)targPan);
				}
				break;
			case EVENT_UNKNOWN0:		//Unknown, 0 data bytes
				AddUnknown(beginOffset, curOffset-beginOffset);
				break;
			case EVENT_UNKNOWN1:		//Unknown, 1 data bytes
				curOffset++;
				AddUnknown(beginOffset, curOffset-beginOffset);
				break;
			case EVENT_UNKNOWN2:		//Unknown, 2 data bytes
				curOffset+=2;
				AddUnknown(beginOffset, curOffset-beginOffset);
				break;
			case EVENT_UNKNOWN3:		//Unknown, 3 data bytes
				curOffset+=3;
				AddUnknown(beginOffset, curOffset-beginOffset);
				break;
			case EVENT_MASTVOL:		//Master Volume
				{
					uint8_t newMVol = GetByte(curOffset++)/2;
					//SetMVol(GetByte(curOffset++));
				//	AddMasterVol(beginOffset, curOffset-beginOffset, newMVol);
					//for (int i=0; i<((NinSnesSeq*)vgmfile)->aTracks.size(); i++)
					//	((NinSnesSeq*)vgmfile)->aTracks[i]->AddMastVol(beginOffset, curOffset-beginOffset, GetMVol());
					//AddVolume(beginOffset, curOffset-beginOffset, vol * GetMVol() / 512, "Master Volume", "Master Volume");
				}
				break;
			case EVENT_MASTVOLFADE:		//Master Volume Fade
				{
					curOffset+=2;
		/*			uint8_t dur = GetByte(curOffset++);
					uint8_t targVol = GetByte(curOffset++);
					for (int i=0; i<parentSeq->aTracks.size(); i++)
						((NinSnesSeq*)vgmfile)->aTracks[i]->AddMastVolSlide(beginOffset, curOffset-beginOffset, dur, targVol);
					SetMVol(targVol);*/
				}
				break;
			case EVENT_TEMPO:		//Tempo
				{
					uint8_t tempo = GetByte(curOffset++);
					AddTempo(beginOffset, curOffset-beginOffset, 24000000/tempo);
				 }
				break;
			case EVENT_TEMPOFADE:		//Tempo Fade
				{
					uint8_t dur = GetByte(curOffset++);
					uint32_t targTempo = 24000000/GetByte(curOffset++);
					AddTempoSlide(beginOffset, curOffset-beginOffset, dur, targTempo);
					//AddEventItem("Tempo Fade", ICON_CONTROL, beginOffset, curOffset-beginOffset, CLR_UNKNOWN);
				}
				break;
			case EVENT_GLOBTRANSP:		//
				//((NinSnesSeq*)vgmfile)->globTranspose = GetByte(curOffset++);
				//AddGenericEvent(beginOffset, curOffset-beginOffset, L"Global Transpose", NULL, BG_CLR_PINK);
				{
					int8_t globTranspose = GetByte(curOffset++);
					AddGlobalTranspose(beginOffset, curOffset-beginOffset, globTranspose);
				}
				
				break;
			case EVENT_TRANSP:
				transpose = GetByte(curOffset++);
				AddTranspose(beginOffset, curOffset-beginOffset, transpose);
				//AddGenericEvent(beginOffset, curOffset-beginOffset, L"Track Transpose", NULL, BG_CLR_PINK);
				//AddEventItem("Track Transpose", ICON_CONTROL, beginOffset, curOffset-beginOffset, BG_CLR_PINK);
				break;
			case EVENT_VOL:		//Volume
				vol = GetByte(curOffset++)/2;
				AddVol(beginOffset, curOffset-beginOffset, vol);
				break;
			case EVENT_VOLFADE:		//Volume Slide
				{
					uint8_t dur = GetByte(curOffset++);
					uint8_t targVol = GetByte(curOffset++)/2;
					AddVolSlide(beginOffset, curOffset-beginOffset, dur, targVol);
				}
				break;
			case EVENT_GOSUB:		//Gosub
				substart = GetByte(curOffset++);
				substart += GetByte(curOffset++) << 8;
				subcount = GetByte(curOffset++);
				subret = curOffset;
				if (substart >= vgmfile->rawfile->size())
					return -1;
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Go Sub", NULL, CLR_LOOP);
				//AddEventItem("Go Sub", ICON_CONTROL, beginOffset, curOffset-beginOffset, BG_CLR_YELLOW);
				curOffset = substart;
				break;
			case EVENT_SETPERCBASE:
				SetPercBase(GetByte(curOffset++));
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Set Perc Base", NULL, CLR_CHANGESTATE);
				//AddEventItem("Set Perc Base", ICON_CONTROL, beginOffset, curOffset-beginOffset, BG_CLR_WHEAT);
				break;
			case EVENT_LOOPBEGIN:
				loopdest = (uint16_t)curOffset;
				loopcount = 0;
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Set Perc Base", NULL, CLR_CHANGESTATE);
				//AddEventItem("Loop Begin", ICON_CONTROL, beginOffset, curOffset-beginOffset, BG_CLR_YELLOW);
				break;
			case EVENT_LOOPEND:
				if (++loopcount == GetByte(curOffset++))		//if last loop
				{
					bInLoop = false;
					loopcount = 0;
					curOffset += 2;
				} 
				else										
				{
					if (loopcount == 1)						//if first time hitting loop
					{
						AddGenericEvent(beginOffset, curOffset-beginOffset, L"Loop End", NULL, CLR_LOOP);
						//AddEventItem("Loop End", ICON_CONTROL, beginOffset, curOffset-beginOffset, BG_CLR_YELLOW);
						bInLoop = true;
					}
					curOffset = loopdest;
				}
				break;
			default:
				AddUnknown(beginOffset, curOffset-beginOffset);
				//AddEventItem("UNKNOWN", ICON_UNKNOWN, beginOffset, curOffset-beginOffset, BG_CLR_RED);
				break;
			}
		}
	}
	return true;
}
