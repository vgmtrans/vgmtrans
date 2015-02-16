#include "stdafx.h"
#include "HOSASeq.h"
#include "HOSAFormat.h"

DECLARE_FORMAT(HOSA);


//**************************************************************
//		HOSASeq
//**************************************************************
//==============================================================
//		Constructor
//==============================================================
HOSASeq::HOSASeq(RawFile* file, uint32_t offset)
:	VGMSeq(HOSAFormat::name, file, offset)
{
	UseReverb();
	UseLinearAmplitudeScale();
}

//==============================================================
//		Destructor
//==============================================================
HOSASeq::~HOSASeq(void)
{
}

//==============================================================
//		Get the information of HOSA header 
//--------------------------------------------------------------
//	Input
//		Nothing		
//	Output
//		flag		true	=	successful
//					false	=	error
//	Memo
//		VGMSeq::LoadMain() から call される。
//==============================================================
bool HOSASeq::GetHeaderInfo(void)
{

	name = L"HOSA Seq";							//this object name


//	About the unLength, if (unLength==0), 
//	"VGMSeq::LoadMain()" will calculate the unLength after "SeqTrack::LoadTrack()".
	nNumTracks		= GetByte(dwOffset+0x06);	//uint8_t (8bit)
	assocHOSA_ID	= 0x00;

//	Add the new object "VGMHeader" in this object "HOSASeq"（Super class："VGMContainerItem")
//	Delect object is in "VGMContainerItem::~VGMContainerItem()"
	VGMHeader* hdr = AddHeader(dwOffset, 0x0050);
	hdr->AddSig(dwOffset, 4);
	hdr->AddSimpleItem(dwOffset+0x06, 1, L"Quantity of Tracks");

	SetPPQN(0x30);								//Timebase

	return true;								//successful
}
//==============================================================
//		Get the pointer of Music Track
//--------------------------------------------------------------
//	Input
//		Nothing
//	Output
//		flag		true	=	successful
//					false	=	
//	Memo
//		VGMSeq::LoadMain() から call される。
//==============================================================
bool HOSASeq::GetTrackPointers(void)
{
	for(unsigned int i=0; i<nNumTracks; i++)
		aTracks.push_back(new HOSATrack(this, GetShort(dwOffset+0x50+(i*2)) + dwOffset));
		//delect object is in "VGMSeq::~VGMSeq()"
	return true;
}





//**************************************************************
//		HOSATrack
//**************************************************************
//==============================================================
//		Constructor		with initialize all member 変数
//--------------------------------------------------------------
//	Input
//		Nothing
//	Output
//		Nothing
//==============================================================
HOSATrack::HOSATrack(HOSASeq* parentFile, long offset, long length):
	SeqTrack(parentFile, offset, length),
	iDeltaTimeCom(0),		//Default delta time for Command
	iDeltaTimeNote(0),		//Default delta time for Note
	iLengthTimeNote(0),		//Default length for Note
//	iDeltaTimeCounter(0),	//Delta time counter
	cVelocity(127),			//Default velocity
	cNoteNum(60)			//Default Note Number
{
}

//==============================================================
//		Process of 1 Event
//--------------------------------------------------------------
//	Input
//		Nothing
//	Output
//		Nothing
//	Memo
//		SeqTrack::LoadTrack() から call される。
//==============================================================
bool HOSATrack::ReadEvent(void)
{

	//==================================
	//	[ Local 変数 ]
	//----------------------------------
	const		uint32_t	beginOffset	= curOffset;					//start offset point

	const		uint8_t	cCommand	= GetByte(curOffset++);			//command (op-code)
	const		uint8_t	cCom_bit0	= (cCommand & 0x1F);			//length / contents
	const		uint8_t	cCom_bit5	= (cCommand & 0x60) >> 5;		//Delta times
	const		uint8_t	cCom_bit7	= (cCommand & 0x80) >> 7;		//0=Notes / 1=Controls

//	unsigned 	int					iMinLengthCounter;				//デルタタイム最小値
//				int					i;		//general
//	vector<char>::iterator			it_note;
//	vector<int unsigned >::iterator	it_Length;



	//==================================
	//	[ Process of "Delta time" and "Note off Event" ]
	//----------------------------------
	//Command用の(DeltaCounter==0)まで繰り返し。
	//while(iDeltaTimeCounter!=0){

	//	//Search the minimum length time [ticks]
	//	iMinLengthCounter	=	iDeltaTimeCounter;
	//	for(i=0;i<listLength.size();i++){
	//		if(iMinLengthCounter > listLength[i]){
	//			iMinLengthCounter = listLength[i];
	//		}
	//	}

	//	//Writing delta time.
	//	AddDelta(iMinLengthCounter);
	//	
	//	//Subtract the minimum length time from "Delta time" and "Length times"
	//	iDeltaTimeCounter	-=	iMinLengthCounter;
	//	for(i=0;i<listNote.size();i++){
	//		listLength[i] -= iMinLengthCounter;
	//	}

	//	//Search the ("Length times" == 0)
	//	it_note		= listNote.begin();
	//	it_Length	= listLength.begin();
	//	while(it_Length != listLength.end()){
	//		if(*it_Length==0){
	//			//Write "Note-off Event"
	//			AddNoteOffNoItem(*it_note);
	//			//Erase note-information from vector.
	//			it_note		= listNote.erase(it_note);
	//			it_Length	= listLength.erase(it_Length);
	//		} else {
	//			it_note++;
	//			it_Length++;
	//		}
	//	}
	//}



	//==================================
	//	[ Process of command (note / control) ]
	//----------------------------------

	//----------------------------------
	//	Command: Notes  (cCommand == 0x00 - 0x7F)
	if(cCom_bit7==0){

		//--------
		//[2]Update the default Note Number
		cNoteNum	= GetByte(curOffset++);

		//--------
		//[3]Update the default Delta time
		ReadDeltaTime(cCom_bit5, &iDeltaTimeCom);

		//--------
		//[4]Update the default Length
		iLengthTimeNote = GetShort(parentSeq->dwOffset + 0x10 + cCom_bit0*2);
		if(iLengthTimeNote==0){
//			iLengthTimeNote = ReadVarLen(curOffset);		//No count curOffset
			iLengthTimeNote = DecodeVariable();
		};

		//--------
		//[5]Update the default Velocity
		if(cNoteNum & 0x80){
			cNoteNum &= 0x7F;
			cVelocity = GetByte(curOffset++);
		};

		//--------
		//[3]Update the default Delta time (continuation)
		if(cCom_bit5==1){
			iDeltaTimeCom = iLengthTimeNote;	//Delta time is the same as note length.
		};
		iDeltaTimeNote = iDeltaTimeCom;			//Default delta time for Note.

		//--------
		//Write Note-On Event
		AddNoteByDur(beginOffset, curOffset-beginOffset, cNoteNum, cVelocity, iLengthTimeNote);


	//----------------------------------
	//	Command: Controls  (cCommand == 0x80 - 0x9F, 0xC0 - 0xFF)
	} else {
		if(cCom_bit5!=1){
			switch(cCom_bit0){
				//------------
				//End of Track
				case(0x00):
					AddEndOfTrack(beginOffset, curOffset-beginOffset);
					return false;
					break;
				//------------
				//Tempo
				case(0x01):
					cTempo = GetByte(curOffset++);
					AddTempoBPM(beginOffset, curOffset-beginOffset, cTempo);
					break;
				//------------
				//Reverb
				case(0x02):
					curOffset++;
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Reverb Depth", NULL, CLR_REVERB);
					break;
				//------------
				//Instrument
				case(0x03):
					cInstrument = GetByte(curOffset++);
					AddProgramChange(beginOffset, curOffset-beginOffset, cInstrument);
					break;
				//------------
				//Volume
				case(0x04):
					cVolume = GetByte(curOffset++);
					AddVol(beginOffset, curOffset-beginOffset, cVolume);
					break;
				//------------
				//Panpot
				case(0x05):
					cPanpot = GetByte(curOffset++);
					AddPan(beginOffset, curOffset-beginOffset, cPanpot);
					break;
				//------------
				//Expression
				case(0x06):
					cExpression = GetByte(curOffset++);
					AddExpression(beginOffset, curOffset-beginOffset, cExpression);
					break;
				//------------
				//Unknown
				case(0x07):
					curOffset++;
					curOffset++;
					AddUnknown(beginOffset, curOffset-beginOffset);
					break;
				//------------
				//Dal Segno. (Loop)
				case(0x09):
					curOffset++;
					AddGenericEvent(beginOffset, curOffset-beginOffset, L"Dal Segno.(Loop)", NULL, CLR_LOOP);
					break;
				//------------
				//Unknown
				case(0x0F):
					AddUnknown(beginOffset, curOffset-beginOffset);
					break;
				//------------
				//Unknowns
				default:
					curOffset++;
					AddUnknown(beginOffset, curOffset-beginOffset);
					break;
			}

			//--------
			//[3]Delta time
			uint32_t	beginOffset2 = curOffset;
			ReadDeltaTime(cCom_bit5, &iDeltaTimeCom);
			if(curOffset != beginOffset2){
				AddGenericEvent(beginOffset2,curOffset-beginOffset2, L"Delta time", NULL, CLR_CHANGESTATE);
			};

	//----------------------------------
	//	Command: Notes  (cCommand == 0xA0 - 0xBF)
		} else {
			if(cCom_bit0 & 0x10){
				//Add (Command = 0xB0 - 0xBF)
				cNoteNum+=(cCom_bit0 & 0x0F);
			}else{
				//Sub (Command = 0xA0 - 0xAF)
				cNoteNum-=(cCom_bit0 & 0x0F);
			};
			//--------
			//Write Note-On Event
			AddNoteByDur(beginOffset, curOffset-beginOffset, cNoteNum, cVelocity, iLengthTimeNote);
			iDeltaTimeCom		= iDeltaTimeNote;
		}
	}

	//==================================
	//	[ Process of "Note" with note length ]
	//----------------------------------
	//if(fNoteOutput!=0){
	//	//Write "Note-on Event"
	//	AddNoteOn(beginOffset, curOffset-beginOffset, fNoteOutput, cVelocity);
	//	//Add note-information to vector.
	//	listNote.push_back(fNoteOutput);		//Note Number
	//	listLength.push_back(iLengthTimeNote);	//Length
	//	//Next delta time
	//	iDeltaTimeCom		= iDeltaTimeNote;
	//}



	//==================================
	//	[ Process of "Setting Delta time" ]
	//----------------------------------
	//iDeltaTimeCounter = iDeltaTimeCom;
	AddTime(iDeltaTimeCom);



	return true;

}
//==============================================================
//		Read delta time
//--------------------------------------------------------------
//	Input
//		unsigned	char	cCom_bit5		bit5-6 of command(op-code)
//					int		*iVariable		Pointer of Variable
//	Output
//					int		*iVariable		Result of read variable
//==============================================================
void	HOSATrack::ReadDeltaTime(unsigned char cCom_bit5, unsigned int *iVariable){

	switch(cCom_bit5){
		//----
		case(2):	//	2 : 可変長
//			*iVariable = ReadVarLen(curOffset);		//No count curOffset
			*iVariable = DecodeVariable();
			break;
		//----
		case(3):	//	3 : From header
			*iVariable = GetShort(parentSeq->dwOffset + 0x10 + (GetByte(curOffset++) & 0x1f)*2);
			break;
		//----
		default:
			// 0 : No update
			// 1 : No update
			break;
	};

};
//==============================================================
//		Decode of Variable
//--------------------------------------------------------------
//	Input
//		Nothing
//	Output
//		int		iVariable		Result of decode 
//==============================================================
unsigned	int		HOSATrack::DecodeVariable(){

	//==================================
	//	[ Local 変数 ]
	//----------------------------------
	unsigned	__int32		iVariable=0;	// Result of decode
	unsigned	int			count=4;		// for counter
	unsigned	char		cFread;			// for reading

	//==================================
	//	[ Read Variable ]
	//----------------------------------
	do{
		cFread=GetByte(curOffset++);		//1 Byte Read
		iVariable=(iVariable<<7)+((unsigned int)cFread&0x7F);
		count--;
	} while((count>0)&&(cFread&0x80));

	return(iVariable);

};
