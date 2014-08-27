#pragma once
#include "../VGMSeq.h"
#include "../SeqTrack.h"
#include "../Format.h"			//Replace with MP2k-specific format header when that's ready


//--------------------------------------------------------------
//		HOSASeq
//--------------------------------------------------------------
class HOSASeq : public VGMSeq
{
public:
	HOSASeq(RawFile* file, uint32_t offset);
	virtual ~HOSASeq(void);

	virtual bool GetHeaderInfo(void);
	virtual bool GetTrackPointers(void);	//Function to find all of the track pointers.   Returns number of total tracks.
	virtual uint32_t GetID() {return assocHOSA_ID;}

protected:
	unsigned short seqID;
	unsigned short assocHOSA_ID;
};



//--------------------------------------------------------------
//		HOSATrack
//--------------------------------------------------------------
class HOSATrack	: public SeqTrack
{
public:
//	HOSATrack(HOSASeq* parentFile);
	HOSATrack(HOSASeq* parentFile, long offset = 0, long length = 0);
//	~HOSATrack(void) {}
//	virtual		void	SetChannel(int trackNum);
	virtual		bool	ReadEvent(void);
				void	ReadDeltaTime(unsigned char cCom_bit5, unsigned int *iVariable);
	unsigned	int		DecodeVariable();	//Decode of 可変長

public:
	unsigned 	int		iDeltaTimeCom;		//Default delta time for Command
	unsigned 	int		iDeltaTimeNote;		//Default delta time for Note
	unsigned 	int		iLengthTimeNote;	//Default length for Note
//	unsigned 	int		iDeltaTimeCounter;	//Counter of delta time for note/command.
//	vector<char>			listNote;		//Key On Note
//	vector<unsigned  int>	listLength;		//
				char	cVelocity;			//Default velocity
				char	cNoteNum;			//Default Note Number
	unsigned	char	cTempo;				//Tempo
	unsigned	char	cInstrument;		//
	unsigned	char	cVolume;			//
	unsigned	char	cPanpot;			//
	unsigned	char	cExpression;		//
};
