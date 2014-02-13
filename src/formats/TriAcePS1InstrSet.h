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

	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

public:

	//-----------------------
	//1,Sep.2009 revise		to do Ç±Ç§ÇµÇΩÇ¢ÅB
	//-----------------------
	typedef struct _InstrHeader
	{
		uint32_t FileSize;	//
		uint16_t InstSize;	//End of Instruction information
		uint16_t unk_06;		//
		uint16_t unk_08;		//
		uint16_t unk_0A;		//
	} InstrHeader;
	//	Å™Å@Å™Å@Å™
	uint16_t instrSectionSize;		// to do delete
	uint8_t  numInstrs;				// to do delete
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
		uint8_t note_range_low;		//These ranges only seem to kick in when the instr has more than 1 rgn
		uint8_t note_range_high;
		uint8_t vel_range_low;
		uint8_t vel_range_high;
		uint32_t sampOffset;
		uint32_t loopOffset;
		uint8_t attenuation;
		int8_t pitchTuneSemitones;
		int8_t pitchTuneFine;
		uint8_t unk_17;
		uint32_t unk_18;
	} RgnInfo;

	typedef struct _InstrInfo
	{
		uint8_t progNum;
		uint8_t bankNum;
		uint16_t ADSR1;
		uint16_t ADSR2;
		uint8_t unk_06;
		uint8_t numRgns;
	} InstrInfo;


public:
	TriAcePS1Instr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank, ULONG theInstrNum);
	~TriAcePS1Instr() { if (rgns) delete[] rgns; }
	virtual bool LoadInstr();

public:
	InstrInfo instrinfo;
	RgnInfo* rgns;
};
