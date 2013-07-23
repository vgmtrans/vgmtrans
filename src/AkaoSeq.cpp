#include "stdafx.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"
#include "ScaleConversion.h"

DECLARE_FORMAT(Akao);

AkaoSeq::AkaoSeq(RawFile* file, ULONG offset)
: VGMSeq(AkaoFormat::name, file, offset)
{
	UseLinearAmplitudeScale();		//I THINK THIS APPLIES, BUT NOT POSITIVE, see FF9 320, track 3 for example of problem
	bUsesIndividualArts = false;
	UseReverb();
}

AkaoSeq::~AkaoSeq(void)
{
}

BYTE AkaoSeq::GetNumPositiveBits(ULONG ulWord)
{
	return	((ulWord&0x80000000)>0) + ((ulWord&0x40000000)>0) + ((ulWord&0x20000000)>0) + ((ulWord&0x10000000)>0) +
			((ulWord&0x8000000)>0)+((ulWord&0x4000000)>0)+((ulWord&0x2000000)>0)+((ulWord&0x1000000)>0) +
			((ulWord&0x800000)>0)+((ulWord&0x400000)>0)+((ulWord&0x200000)>0)+((ulWord&0x100000)>0) +
			((ulWord&0x80000)>0)+((ulWord&0x40000)>0)+((ulWord&0x20000)>0)+((ulWord&0x10000)>0) +
			((ulWord&0x8000)>0) + ((ulWord&0x4000)>0) + ((ulWord&0x2000)>0) + ((ulWord&0x1000)>0) +
			((ulWord&0x800)>0)+((ulWord&0x400)>0)+((ulWord&0x200)>0)+((ulWord&0x100)>0) +
			((ulWord&0x80)>0)+((ulWord&0x40)>0)+((ulWord&0x20)>0)+((ulWord&0x10)>0) +
			((ulWord&0x8)>0)+((ulWord&0x4)>0)+((ulWord&0x2)>0)+((ulWord&0x1));
}

int AkaoSeq::GetHeaderInfo(void)
{
	//first do a version check to see if it's older or newer version of AKAO sequence format
	if (GetWord(dwOffset+0x2C) == 0)
	{
		nVersion = VERSION_3;
		//assoc_ss_id = GetShort(0x14 + dwOffset);
		id = GetShort(0x14 + dwOffset);
		seq_id = GetShort(0x4 + dwOffset);
	}
	else if (GetWord(dwOffset+0x1C) == 0)
	{
		nVersion = VERSION_2;
		return false;
	}
	else
	{
		nVersion = VERSION_1;
		return false;
	}

	name = L"Akao Seq";

	VGMHeader* hdr = AddHeader(dwOffset, 0x40);
	hdr->AddSig(dwOffset, 4);
	hdr->AddSimpleItem(dwOffset+0x4, 2, L"ID");
	hdr->AddSimpleItem(dwOffset+0x6, 2, L"Size");
	hdr->AddSimpleItem(dwOffset+0x14, 2, L"Associated Sample Set ID");
	hdr->AddSimpleItem(dwOffset+0x20, 4, L"Number of Tracks (# of true bits)");
	hdr->AddSimpleItem(dwOffset+0x30, 4, L"Instrument Data Pointer");
	hdr->AddSimpleItem(dwOffset+0x34, 4, L"Drumkit Data Pointer");

	SetPPQN(0x30);
	nNumTracks = GetNumPositiveBits(GetWord(dwOffset+0x20));
	unLength = GetShort(dwOffset+6);

	//There must be either a melodic instrument section, a drumkit, or both.  We determiine
	//the start of the InstrSet based on whether a melodic instrument section is given.
	U32 instrOff = GetWord(dwOffset + 0x30);
	U32 drumkitOff = GetWord(dwOffset + 0x34);
	if (instrOff != 0)
		instrOff += 0x30 + dwOffset;
	if (drumkitOff != 0)
		drumkitOff += 0x34 + dwOffset;
	U32 instrSetLength;
	if (instrOff != 0)
		instrSetLength = unLength - (instrOff - dwOffset);
	else
		instrSetLength = unLength - (drumkitOff - dwOffset);
	instrset = new AkaoInstrSet(rawfile, instrSetLength, instrOff, drumkitOff, id, L"Akao Instr Set");
	instrset->LoadVGMFile();

	return true;		//successful
}


int AkaoSeq::GetTrackPointers(void)
{
	for(unsigned int i=0; i<nNumTracks; i++)
		aTracks.push_back(new AkaoTrack(this, GetShort(dwOffset+0x40+(i*2)) + i*2 + 0x40 + dwOffset));
	return true;
}






AkaoTrack::AkaoTrack(AkaoSeq* parentFile, long offset, long length)
: SeqTrack(parentFile, offset, length), loop_begin_layer(0), loop_layer(0), bNotePlaying(0)
{
	memset(loopID, 0, 8);
	memset(loop_counter, 0, 8);
}


//--------------------------------------------------
//Revisions:
//	2009. 6.17(Wed.) :	Re-make by "Sound tester 774" in "“à‘ ‰¹Œ¹‚ðMIDI•ÏŠ·‚·‚éƒXƒŒ(in http://www.2ch.net)"
//						Add un-known command(op-code).
//--------------------------------------------------
int AkaoTrack::ReadEvent(void)
{
	ULONG beginOffset = curOffset;
	BYTE status_byte = GetByte(curOffset++);

	int i, k;

	if (status_byte < 0x9A)   //it's either a  note-on message, a tie message, or a rest message
	{
		//looking at offset 8005E5C8 in the FFO FF2 exe for this delta time table code  4294967296  0x100000000/0xBA2E8BA3   = 1.374999... = 11/8
		//0x68 after eq = 9
		/*i = ((status_byte * 0xBA2E8BA3) & 0xFF00000000) >> 32; // emulating multu and mfhi instruction
		//i >>= 3;  //srl 3
*/		i = status_byte / 11;
		k = i*2;  //sll 1 - aka multiply by 2
		k +=  i;   //addu $v0, $v1
		k *= 4;  //sll 2 - aka multiply by 4
		k -= i;  //subu    $v0, $v1
		k = status_byte - k; //subu    $v0, $s1, $v0
		//k now equals the table index value
		//relative key calculation found at 8005E6F8 in FFO FF2 exe

		if (status_byte < 0x83) // it's a note-on message
		{
			relative_key = status_byte / 11;
			base_key = octave*12;
			if (bNotePlaying)
			{
				AddNoteOffNoItem(prevKey);
				bNotePlaying = false;
			}
			//if (bAssociatedWithSSTable)
			//	FindNoteInstrAssoc(hFile, base_key + relative_key);


			AddNoteOn(beginOffset, curOffset-beginOffset, base_key + relative_key, vel);
			bNotePlaying = true;

			AddDelta(delta_time_table[k]);
		}
		else if (status_byte < 0x8F)  //if it's between 0x83 and 0x8E it is a tie event
		{
			AddDelta(delta_time_table[k]);
			if (loop_counter[loop_layer] == 0 && loop_layer == 0)		//do this so we don't repeat this for every single time it loops
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tie", NULL, CLR_TIE);
		}
		else				//otherwise, it's between 0x8F and 0x99 and it's a rest message
		{
			if (bNotePlaying)
			{
				AddNoteOff(beginOffset, curOffset-beginOffset, prevKey);
				bNotePlaying = false;
			}
			AddRest(beginOffset, curOffset-beginOffset, delta_time_table[k]);
		}
	}
	else if ((status_byte >= 0xF0) && (status_byte <= 0xFB))
	{
		relative_key = status_byte-0xF0;
		base_key = octave*12;
		if (bNotePlaying)
		{
			AddNoteOffNoItem(prevKey);
			bNotePlaying = false;
		}
		bNotePlaying = true;
		AddNoteOn(beginOffset, curOffset-beginOffset+1, base_key + relative_key, vel);
		AddDelta(GetByte(curOffset++));
	}
	else switch (status_byte)
	{
	 case 0xA0 :
//		----[ 2009. 6.12	change ]-----
//		 AddUnknown(beginOffset, curOffset-beginOffset);
//		 break;
		AddEndOfTrack(beginOffset, curOffset-beginOffset);
		return false;

	 case 0xA1 :			// change program to articulation number
		{
			((AkaoSeq*)parentSeq)->bUsesIndividualArts = true;
			BYTE artNum = GetByte(curOffset++);
			BYTE progNum = ((AkaoSeq*)parentSeq)->instrset->aInstrs.size() + artNum;
			AddProgramChange(beginOffset, curOffset-beginOffset, progNum);
		}
		break;

	case 0xA2 :			// set next note length [ticks]
		curOffset++;
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Next note length", NULL, CLR_CHANGESTATE);
		break;

	 case 0xA3 :			//set track volume
		 vol = GetByte(curOffset++);			//expression value		
		 AddVol(beginOffset, curOffset-beginOffset, vol);
		 break;

	 case 0xA4:			// pitch slide half steps.  (Portamento)
		{
			BYTE dur = GetByte(curOffset++);		//first byte is duration of slide
			BYTE steps = GetByte(curOffset++);		//second byte is number of halfsteps to slide... not sure if signed or not, only seen positive
			//AddPitchBendSlide(
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Portamento", NULL, CLR_PORTAMENTO);
		}
		break;

	 case 0xA5 :			//set octave
		 octave = GetByte(curOffset++);
		 AddSetOctave(beginOffset, curOffset-beginOffset, octave);
		 break;

	 case 0xA6 :			//set octave + 1
		 AddIncrementOctave(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xA7 :			//set octave - 1
		 AddDecrementOctave(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xA8 :			//set expression (USED TO BE VELOCITY, may go back after testing, as this is messy)
		 {
			 //vel = GetByte(curOffset++);
			 //vel = Convert7bitPercentVolValToStdMidiVal(vel);		//I THINK THIS APPLIES, BUT NOT POSITIVE
			 //AddGenericEvent(beginOffset, curOffset-beginOffset, L"Set Velocity", NULL, BG_CLR_CYAN);
			 BYTE cExpression = GetByte(curOffset++);
////			 ‚±‚Á‚¿‚Ìlog‰‰ŽZ‚Í—v‚ç‚È‚¢
////			 vel = Convert7bitPercentVolValToStdMidiVal(vel);		//I THINK THIS APPLIES, BUT NOT POSITIVE
			 vel = 127;		//‚Æ‚è‚ ‚¦‚¸ 127 ‚É‚µ‚Ä‚¨‚­
			 AddExpression(beginOffset, curOffset-beginOffset, cExpression);
		 }
		 break;

	 case 0xA9 :			//set Expression fade
		 {
			BYTE dur = GetByte(curOffset++);
			BYTE targExpr = GetByte(curOffset++);			//reads the target velocity value - the velocity value we are fading toward
			AddExpressionSlide(beginOffset, curOffset-beginOffset, dur, targExpr);
		 }
		 break;

	 case 0xAA :			//set pan
		 {
			BYTE pan = GetByte(curOffset++);
			AddPan(beginOffset, curOffset-beginOffset, pan);
		 }
		break;

	 case 0xAB :			//set pan fade
		 {
			BYTE dur = GetByte(curOffset++);
			BYTE targPan = GetByte(curOffset++);			//reads the target velocity value - the velocity value we are fading toward
			AddPanSlide(beginOffset, curOffset-beginOffset, dur, targPan);
		 }
		 break;

	 case 0xAC :			//unknown
		 curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		 break;

/*AD = set attack AE = set decay AF = set sustain level  B0 = AE then AF
<CBongo> B1 = set sustain release  B2 = set release
 B7 might be set A,D,S,R values all at once
 B3 appears to be "reset ADSR to the initial values from the instrument/sample data"*/

	 case 0xAD :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", NULL, CLR_ADSR);
		 break;
	 case 0xAE :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", NULL, CLR_ADSR);
		 break;
	 case 0xAF :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", NULL, CLR_ADSR);
		 break;
	 case 0xB0 :
		 curOffset++;
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", NULL, CLR_ADSR);
		 break;
	 case 0xB1 :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", NULL, CLR_ADSR);
		 break;
	 case 0xB2 :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", NULL, CLR_ADSR);
		 break;
	 case 0xB3 :
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"ADSR", NULL, CLR_ADSR);
		 break;

	//
	//	0xB4 to 0xB7	LFO Pitch bend
	//	0xB8 to 0xBC	LFO Expression
	//	0xBC to 0xBF	LFO Panpot
	//

	 case 0xB4 :			//unknown
		 curOffset += 3;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Pitch bend) Length, cycle", NULL, CLR_LFO);
		 break;
	 case 0xB5 :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Pitch bend) Depth", NULL, CLR_LFO);
		 break;
	 case 0xB6 :			//unknown
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Pitch bend) off", NULL, CLR_LFO);
		 break;
	 case 0xB7 :
		 curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xB8 :			//unknown
		 curOffset += 3;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Expression) Length, cycle", NULL, CLR_LFO);
		 break;
	 case 0xB9 :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Expression) Depth", NULL, CLR_LFO);
		 break;
	 case 0xBA :			//unknown
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Expression) off", NULL, CLR_LFO);
		 break;
	 case 0xBB :
		 curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xBC :			//unknown
		 curOffset += 2;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Panpot) Length, cycle", NULL, CLR_LFO);
		 break;
	 case 0xBD :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Panpot) Depth", NULL, CLR_LFO);
		 break;
	 case 0xBE :			//unknown
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Panpot) off", NULL, CLR_LFO);
		 break;
	 case 0xBF :
		 curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xC0 :
		 {
			char cTranspose = GetByte(curOffset++);
			AddTranspose(beginOffset, curOffset-beginOffset,cTranspose);
		 }
		 break;
	 case 0xC1 :
		 {
			BYTE cTranspose = GetByte(curOffset++);
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Transpose move", NULL, CLR_TRANSPOSE);
		 }
		 break;

	case 0xC2 :
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reverb On", NULL, CLR_REVERB);
		 break;
	case 0xC3 :
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reverb Off", NULL, CLR_REVERB);
		break;

	case 0xC4 :
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"Noise On", NULL, CLR_UNKNOWN);
		 break;
	case 0xC5 :
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Noise Off", NULL, CLR_UNKNOWN);
		break;

	case 0xC6 :
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"FM Modulation On", NULL, CLR_UNKNOWN);
		break;
	case 0xC7 :
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"FM Modulation Off", NULL, CLR_UNKNOWN);
		break;

	case 0xC8 :			// set loop begin marker
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Repeat Start", NULL, CLR_LOOP);
		loop_begin_layer++;
		loop_begin_loc[loop_begin_layer] = curOffset;
		//bInLoop = true;
		break;

	case 0xC9 :
		{
			BYTE value1 = GetByte(curOffset++);
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Repeat End", NULL, CLR_LOOP);
			if (loop_begin_layer == 0)		//if loop_begin_layer == 0, then there was never a matching loop begin event!  this is seen in ff9 402 and ff9 hunter's chance
				break;
			if (loopID[loop_layer] != curOffset)
			{
				loop_layer++;
				loopID[loop_layer] = curOffset;
				curOffset = loop_begin_loc[loop_begin_layer];
				loop_counter[loop_layer] = value1-2;
				bInLoop = true;
			}
			else if (loop_counter[loop_layer] > 0)
			{
				loop_counter[loop_layer]--;
				curOffset = loop_begin_loc[loop_begin_layer];
				//if (loop_counter[loop_layer] == 0 && loop_layer == 1)
				//	bInLoop = false;
			}
			else
			{
				loopID[loop_layer]=0;
				loop_layer--;
				loop_begin_layer--;
				if (loop_layer == 0)
					bInLoop = false;
			}
		}
		break;

	case 0xCA :
		{
			BYTE value1 = 2;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Repeat End", NULL, CLR_LOOP);
			if (loop_begin_layer == 0)
				break;
			if (loopID[loop_layer] != curOffset)
			{
				loop_layer++;
				loopID[loop_layer] = curOffset;
				curOffset = loop_begin_loc[loop_begin_layer];
				loop_counter[loop_layer] = value1-2;
				bInLoop = true;
			}
			else if (loop_counter[loop_layer] > 0)
			{
				loop_counter[loop_layer]--;
				curOffset = loop_begin_loc[loop_begin_layer];
				//if (loop_counter[loop_layer] == 0 && loop_layer == 1)
				//	bInLoop = false;
			}
			else
			{
				loopID[loop_layer]=0;
				loop_layer--;
				loop_begin_layer--;
				if (loop_layer == 0)
					bInLoop = false;
			}
		}
		break;

	case 0xCC :
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Slur On", NULL, CLR_PORTAMENTO);
		break;
	case 0xCD :
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Slur Off", NULL, CLR_PORTAMENTO);
		break;

	case 0xD0 :
		AddNoteOff(beginOffset, curOffset-beginOffset, prevKey);
		break;

	case 0xD1 :			// unknown
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Deactivate Notes?", NULL, CLR_UNKNOWN);
		break;

	case 0xD2 :
		curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xD3 :
		curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		break;

	case 0xD8 :			//pitch bend
		{
			BYTE cValue = GetByte(curOffset++);		//signed data byte.  range of 1 octave (0x7F = +1 octave 0x80 = -1 octave)
			int fullValue = (int)(cValue * 64.503937007874015748031496062992);
			fullValue += 0x2000;
			BYTE lo = fullValue & 0x7F;
			BYTE hi = (fullValue & 0x3F80) >> 7;
			AddPitchBendMidiFormat(beginOffset, curOffset-beginOffset, lo, hi);
		}
		break;

	 case 0xD9 :
		 curOffset++;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"Pitch bend move", NULL, CLR_UNKNOWN);
		 break;

	 case 0xDA :
		 curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xDC :
		 curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xDD :
		 curOffset += 2;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Pitch bend) times", NULL, CLR_UNKNOWN);
		 break;
	 case 0xDE :
		 curOffset += 2;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Expression) times", NULL, CLR_UNKNOWN);
		 break;
	 case 0xDF :
		 curOffset += 2;
		 AddGenericEvent(beginOffset, curOffset-beginOffset, L"LFO(Panpot) times", NULL, CLR_UNKNOWN);
		 break;

	 case 0xE1 :
		 curOffset++;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xE6 :
		 curOffset += 2;
		 AddUnknown(beginOffset, curOffset-beginOffset);
		 break;

	 case 0xFC :			//tie time
		 //if (nVersion == VERSION_1 || nVersion == VERSION_2)
		//	 goto MetaEvent;
		 //rest_time += pDoc->GetByte(j++);
		AddDelta(GetByte(curOffset++));
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"Tie (custom)", NULL, CLR_TIE);
		break;

	 case 0xFD :
		 {
			 if (bNotePlaying)
			 {
				 AddNoteOffNoItem(prevKey);
		 		bNotePlaying = false;
			 }
			 BYTE restTime = GetByte(curOffset++);
			 AddRest(beginOffset, curOffset-beginOffset, restTime);
		 }
		 break;

	 case 0xFE :			//meta event
		//MetaEvent:
		switch (GetByte(curOffset++))
		{
		case 0x00 :			//tempo
			{
				BYTE value1 = GetByte(curOffset++);
				BYTE value2 = GetByte(curOffset++);
				double dValue1 = ((value2<<8) + value1)/218.4555555555555555555555555;		//no clue how this magic number is properly derived
				AddTempoBPM(beginOffset, curOffset-beginOffset, dValue1);
			}
			break;

		case 0x01 :			//tempo slide
			{
				BYTE value1 = GetByte(curOffset++);    //NEED TO ADDRESS value 1	
				BYTE value2 = GetByte(curOffset++);
				//AddTempoSlide(
				//RecordMidiSetTempo(current_delta_time, value2); 
			}
			break;

		case 0x02 :			//reverb
			curOffset++;
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reverb Level", NULL, CLR_REVERB);
			break;

		case 0x03 :			//reverb
			curOffset++;
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reverb Fade", NULL, CLR_REVERB);
			break;

		case 0x04 :			//signals this track uses the drum kit
			AddProgramChange(beginOffset, curOffset-beginOffset, 127, L"Drum kit On");
			//channel = 9;
			break;

		case 0x05 :			//reverb
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Drum kit Off", NULL, CLR_UNKNOWN);
			break;

		case 0x06 :			//Branch Relative
			{
				/*USHORT siValue = GetShort(pDoc, j);
				if (nScanMode == MODE_CONVERT_MIDI)		//if we are converting the midi, the actually branch
				{
					if (seq_repeat_counter-- > 0)
						curOffset += siValue;
					else
						curOffset += 2;
				}
				else
					curOffset += 2;								//otherwise, just skip over the relative branch offset*/
				curOffset += 2;
				AddGenericEvent(beginOffset, curOffset-beginOffset, L"Dal Segno.(Loop)", NULL, CLR_LOOP);
				return false;
			}
			break;

		case 0x07 :			//Permanence Loop break with conditional.
			curOffset++;
			curOffset++;
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Dal Segno (Loop) Break", NULL, CLR_LOOP);
			break;

		case 0x09 :			//Repeat break with conditional.
			curOffset++;
			curOffset++;
			curOffset++;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Repeat Break", NULL, CLR_LOOP);
			break;

		//case 0x0E :			//call subroutine
		//case 0x0F :			//return from subroutine

		case 0x10 :			//unknown
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;

		case 0x14 :			//program change
			{
				BYTE curProgram = GetByte(curOffset++);
				//if (!bAssociatedWithSSTable)
				//	RecordMidiProgramChange(current_delta_time, curProgram, hFile);
				AddProgramChange(beginOffset, curOffset-beginOffset, curProgram);
			}
			break;

		case 0x15 :			//Time Signature
			{
				BYTE denom = 4; curOffset++;//(192 / GetByte(curOffset++));
				BYTE numer = GetByte(curOffset++);
				//AddTimeSig(beginOffset, curOffset-beginOffset, numer, denom, parentSeq->GetPPQN());
			}
			break;

		case 0x16 :			//Maker
			curOffset += 2;
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"Maker", NULL, CLR_UNKNOWN);
			break;
		case 0x1C :			//unknown
			curOffset++;
			AddUnknown(beginOffset, curOffset-beginOffset);
			break;
		default :
			AddGenericEvent(beginOffset, curOffset-beginOffset, L"UNKNOWN META EVENT", NULL, CLR_UNRECOGNIZED);
			break;
		}
		break;

	default :
		AddGenericEvent(beginOffset, curOffset-beginOffset, L"UNKNOWN EVENT", NULL, CLR_UNRECOGNIZED);
		break;
	}
	return true;
}