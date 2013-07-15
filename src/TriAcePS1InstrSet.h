#pragma once
#include "common.h"
#include "TriAcePS1Format.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"



// *****************
// TriAcePS1InstrSet
// *****************

class TriAcePS1InstrSet
	: public VGMInstrSet
{

public:
	TriAcePS1InstrSet(RawFile* file, ULONG offset);
	virtual ~TriAcePS1InstrSet(void);

	virtual int GetHeaderInfo();
	virtual int GetInstrPointers();

public:

	//-----------------------
	//1,Sep.2009 revise		to do Ç±Ç§ÇµÇΩÇ¢ÅB
	//-----------------------
	typedef struct _InstrHeader
	{
		U32 FileSize;	//
		U16 InstSize;	//End of Instruction information
		U16 unk_06;		//
		U16 unk_08;		//
		U16 unk_0A;		//
	} InstrHeader;
	//	Å™Å@Å™Å@Å™
	U16 instrSectionSize;		// to do delete
	U8  numInstrs;				// to do delete
	//-----------------------

	//DWORD dwSampSectOffset;
	//DWORD dwSampSectSize;
	//DWORD dwNumInstrs;
	//DWORD dwTotalRegions;
};


// **************
// TriAcePS1Instr
// **************

class TriAcePS1Instr
	: public VGMInstr
{
public:
	
	typedef struct _RgnInfo
	{
		U8 note_range_low;		//These ranges only seem to kick in when the instr has more than 1 rgn
		U8 note_range_high;
		U8 vel_range_low;
		U8 vel_range_high;
		U32 sampOffset;
		U32 loopOffset;
		U8 attenuation;
		S8 pitchTuneSemitones;
		S8 pitchTuneFine;
		U8 unk_17;
		U32 unk_18;
	} RgnInfo;

	typedef struct _InstrInfo
	{
		U8 progNum;
		U8 bankNum;
		U16 ADSR1;
		U16 ADSR2;
		U8 unk_06;
		U8 numRgns;
	} InstrInfo;


public:
	TriAcePS1Instr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank, ULONG theInstrNum);
	~TriAcePS1Instr() { if (rgns) delete[] rgns; }
	virtual int LoadInstr();

public:
	InstrInfo instrinfo;
	RgnInfo* rgns;
};
