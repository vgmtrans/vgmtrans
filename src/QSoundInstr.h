#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMMiscFile.h"

class QSoundInstr;

extern enum QSoundVer;

typedef struct _qs_qs_prog_info_ver_101 {	// ex: Punisher
	BYTE sample_index;
	BYTE ignored;		//only seen used in slammast, but the game never reads the val
	BYTE attack_rate;
	BYTE decay_rate;
	BYTE sustain_level;
	BYTE sustain_rate;
	BYTE release_rate;
	BYTE unknown;
} qs_prog_info_ver_101;

typedef struct _qs_qs_prog_info_ver_103 {	// ex: Super Street Fighter 2
	WORD sample_index;
	BYTE unknown;
	BYTE attack_rate;
	BYTE decay_rate;
	BYTE sustain_level;
	BYTE sustain_rate;
	BYTE release_rate;
} qs_prog_info_ver_103;

typedef struct _qs_prog_info_ver_130 {
	WORD sample_index;
	BYTE unknown;
	BYTE artic_index;
} qs_prog_info_ver_130;

typedef struct _qs_artic_info {
	BYTE attack_rate;
	BYTE decay_rate;
	BYTE sustain_level;
	BYTE sustain_rate;
	BYTE release_rate;
	BYTE unknown_5;
	BYTE unknown_6;
	BYTE unknown_7;
} qs_artic_info;

typedef struct _qs_samp_info {
	BYTE bank;
	BYTE start_addr_lo;
	BYTE start_addr_hi;
	BYTE loop_offset_lo;
	BYTE loop_offset_hi;
	BYTE end_addr_lo;
	BYTE end_addr_hi;
	BYTE unity_key;
} qs_samp_info;

// The QSound interrupt clock is set to 251hz in mame.  But the main sound
// code runs only every 4th tick. (see the "and 3" instruction at 0xF9 in sfa2 code)
#define QSOUND_TICK_FREQ (1/(251/4.0))


// the tables below are taken from sfa2
//  have verified that the exact same values are stored in dinosaurs & cadillacs (early qsound driver)

const WORD linear_table[128] = {
	0, 0x3FF, 0x5FE, 0x7FF, 0x9FE, 0xBFE, 0xDFD, 0xFFF, 0x11FE, 0x13FE,
	0x15FD, 0x17FE, 0x19FD, 0x1BFD, 0x1DFC, 0x1FFF, 0x21FD, 0x23FE, 0x25FD,
	0x27FE, 0x29FD, 0x2BFD, 0x2DFC, 0x2FFE, 0x31FD, 0x33FD, 0x35FC, 0x37FD,
	0x39FC, 0x3BFC, 0x3DFB, 0x3FFF, 0x41FE, 0x43FE, 0x45FD, 0x47FE, 0x49FD,
	0x4BFD, 0x4DFC, 0x4FFE, 0x51FD, 0x53FD, 0x55FC, 0x57FD, 0x59FC, 0x5BFC,
	0x5DFB, 0x5FFE, 0x61FD, 0x63FD, 0x65FC, 0x67FD, 0x69FC, 0x6BFC, 0x6DFB,
	0x6FFD, 0x71FC, 0x73FC, 0x75FB, 0x77FC, 0x79FB, 0x7BFB, 0x7DFA, 0x7FFF,
	0x81FE, 0x83FE, 0x85FD, 0x87FE, 0x89FD, 0x8BFD, 0x8DFC, 0x8FFE, 0x91FD,
	0x93FD, 0x95FC, 0x97FD, 0x99FC, 0x9BFC, 0x9DFB, 0x9FFE, 0xA1FD, 0xA3FD,
	0xA5FC, 0xA7FD, 0xA9FC, 0xABFC, 0xADFB, 0xAFFD, 0xB1FC, 0xB3FC,
	0xB5FB, 0xB7FC, 0xB9FB, 0xBBFB, 0xBDFA, 0xBFFE, 0xC1FD, 0xC3FD,
	0xC5FC, 0xC7FD, 0xC9FC, 0xCBFC, 0xCDFB, 0xCFFD, 0xD1FC, 0xD3FC,
	0xD5FB, 0xD7FC, 0xD9FB, 0xDBFB, 0xDDFA, 0xDFFD, 0xE1FC, 0xE3FC,
	0xE5FB, 0xE7FC, 0xE9FB, 0xEBFB, 0xEDFA, 0xEFFC, 0xF1FB, 0xF3FB,
	0xF5FA, 0xF7FB, 0xF9FA, 0xFBFA, 0xFDF9, 0xFFFE
};

const WORD attack_rate_table[64] = {
	0, 0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 0x0B, 0x0D, 0x0F,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x64, 0x84, 0x0A4, 0x0C5, 0x0E6, 0x107, 0x149,
	0x18B, 0x1CC, 0x20E, 0x292, 0x315, 0x398, 0x41C, 0x523, 0x62A, 0x731, 0x838,
	0x0A46, 0x0C54, 0x0E62, 0x1070, 0x148C, 0x18A8, 0x1CC4, 0x20E0, 0x2918,
	0x3150, 0x3989, 0x41C1, 0x5233, 0x62A0, 0x730D, 0x837D, 0xA45D, 0xC54C,
	0xE61C, 0xEFFF, 0xF3FF, 0xF9FF, 0xFCFF, 0xFFFF
};

const WORD decay_rate_table[64] = {
	0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 8, 0x0A, 0x0C, 0x0E, 0x11, 0x13,
	0x18, 0x1D, 0x21, 0x26, 0x30, 0x39, 0x43, 0x4C, 0x5F, 0x72, 0x85, 0x98, 0x0BE,
	0x0E4, 0x10A, 0x130, 0x17D, 0x1C9, 0x215, 0x260, 0x2F9, 0x391, 0x42A, 0x4C1,
	0x5F2, 0x722, 0x853, 0x983, 0x0BE4, 0x0E46, 0x10A6, 0x1307, 0x17C9, 0x1C8B,
	0x214C, 0x2608, 0x2F92, 0x3916, 0x4299, 0x4C1E, 0x5F24, 0x7228, 0x8533,
	0x9835, 0xBE3E, 0xE451, 0xEFFF, 0xFFFF
};

// ****************
// QSoundArticTable
// ****************

class QSoundArticTable
	: public VGMMiscFile
{
public:
	QSoundArticTable(RawFile* file, wstring& name, U32 offset, U32 length);
	virtual ~QSoundArticTable(void);

	virtual int LoadMain();

public:
	qs_artic_info* artics;
};

// *********************
// QSoundSampleInfoTable
// *********************

class QSoundSampleInfoTable
	: public VGMMiscFile
{
public:
	QSoundSampleInfoTable(RawFile* file, wstring& name, U32 offset, U32 length = 0);
	virtual ~QSoundSampleInfoTable(void);

	virtual int LoadMain();

public:
	qs_samp_info* infos;
	int numSamples;
};

// **************
// QSoundInstrSet
// **************

class QSoundInstrSet
	: public VGMInstrSet
{
public:
	QSoundInstrSet(RawFile* file,
				   QSoundVer fmt_version,
				   U32 offset, 
				   int numInstrBanks,
				   QSoundSampleInfoTable* sampInfoTable,
				   QSoundArticTable* articTable,
                   wstring& name);
	virtual ~QSoundInstrSet(void);

	virtual int GetHeaderInfo();
	virtual int GetInstrPointers();

public:
	//prog_info*	prog_infos;
	QSoundVer fmt_version;
	UINT num_instr_banks;
	QSoundSampleInfoTable* sampInfoTable;
	QSoundArticTable* articTable;
	//U32 samp_table_offset;
	//U32 samp_table_length;
	//qs_samp_info*	qs_samp_infos;
	//UINT numSamples;

protected:
	
};


// ***********
// QSoundInstr
// ***********

class QSoundInstr
	: public VGMInstr
{
public:
	QSoundInstr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank, ULONG theInstrNum, wstring& name);
	virtual ~QSoundInstr(void);
	virtual int LoadInstr();
protected:
	QSoundVer GetFormatVer() { return ((QSoundInstrSet*)parInstrSet)->fmt_version; }

protected:
	BYTE attack_rate;
	BYTE decay_rate;
	BYTE sustain_level;
	BYTE sustain_rate;
	BYTE release_rate;

	int info_ptr;		//pointer to start of instrument set block
	int nNumRegions;
};


// *****
// WDRgn
// *****

//class WDRgn
//	: public VGMRgn
//{
//public:
//	WDRgn(QSoundInstr* instr, ULONG offset);
//	//virtual ~WDRgn(void) {}
//	//virtual int OnSelected(void);
//
//public:
//	unsigned short ADSR1;				//raw ps2 ADSR1 value (articulation data)
//	unsigned short ADSR2;				//raw ps2 ADSR2 value (articulation data)
//	unsigned char bStereoRegion;
//	unsigned char StereoPairOrder;
//	unsigned char bFirstRegion;
//	unsigned char bLastRegion;
//	unsigned char bUnknownFlag2;
//	ULONG sample_offset;
//};



// **************
// QSoundSampColl
// **************

class QSoundSampColl
	: public VGMSampColl
{
public:
	QSoundSampColl(RawFile* file, QSoundInstrSet* instrset, QSoundSampleInfoTable* sampinfotable, ULONG offset, 
		           ULONG length = 0, wstring& name = wstring(L"QSound Sample Collection"));
	virtual bool GetHeaderInfo();		//retrieve any header data
	virtual bool GetSampleInfo();		//retrieve sample info, including pointers to data, # channels, rate, etc.

private:
	QSoundInstrSet* instrset;
	QSoundSampleInfoTable* sampInfoTable;
};
