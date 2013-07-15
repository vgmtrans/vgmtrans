#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"

//VAB Header
struct VabHdr
{
	long form; /*always “VABp”*/
	long ver; /*format version number*/
	long id; /*bank ID*/
	unsigned long fsize; /*file size*/
	unsigned short reserved0; /*system reserved*/
	unsigned short ps; /*total number of programs in this bank*/
	unsigned short ts; /*total number of effective tones*/
	unsigned short vs; /*number of waveforms (VAG)*/
	unsigned char mvol; /*master volume*/
	unsigned char pan; /*master pan*/
	unsigned char attr1; /*bank attribute*/
	unsigned char attr2; /*bank attribute*/
	unsigned long reserved1; /*system reserved*/
};


//Program Attributes
struct ProgAtr 
{
	unsigned char tones; /*number of effective tones which compose the program*/
	unsigned char mvol; /*program volume*/
	unsigned char prior; /*program priority*/
	unsigned char mode; /*program mode*/
	unsigned char mpan; /*program pan*/
	char reserved0; /*system reserved*/
	short attr; /*program attribute*/
	unsigned long reserved1; /*system reserved*/
	unsigned long reserved2; /*system reserved*/
};


//Tone Attributes
struct VagAtr
{
	unsigned char prior; /*tone priority (0 – 127); used for controlling allocation when more voices than can be keyed on are requested*/
	unsigned char mode; /*tone mode (0 = normal; 4 = reverb applied */
	unsigned char vol; /*tone volume*/
	unsigned char pan; /*tone pan*/
	unsigned char center; /*center note (0~127*/
	unsigned char shift; /*pitch correction (0~127,cent units)*/
	unsigned char min; /*minimum note limit (0~127)*/
	unsigned char max; /*maximum note limit (0~127, provided min < max)*/
	unsigned char vibW; /*vibrato width (1/128 rate,0~127)*/
	unsigned char vibT; /*1 cycle time of vibrato (tick units)*/
	unsigned char porW; /*portamento width (1/128 rate, 0~127)*/
	unsigned char porT; /*portamento holding time (tick units)*/
	unsigned char pbmin; /*pitch bend (-0~127, 127 = 1 octave)*/
	unsigned char pbmax; /*pitch bend (+0~127, 127 = 1 octave)*/
	unsigned char reserved1; /*system reserved*/
	unsigned char reserved2; /*system reserved*/
	unsigned short adsr1; /*ADSR1*/
	unsigned short adsr2; /*ADSR2*/
	short prog; /*parent program*/
	short vag; /*waveform (VAG) used*/
	short reserved[4]; /*system reserved*/
};



class Vab :
	public VGMInstrSet
{
public:
	Vab(RawFile* file, ULONG offset);
	virtual ~Vab(void);

	virtual int GetHeaderInfo();
	virtual int GetInstrPointers();

public:
	VabHdr hdr;
};


// ********
// VabInstr
// ********

class VabInstr
	: public VGMInstr
{
public:
	VabInstr(VGMInstrSet* instrSet, ULONG offset, ULONG length, ULONG theBank, ULONG theInstrNum);
	virtual ~VabInstr(void);

	virtual int LoadInstr();

public:
	ProgAtr  attr;
	BYTE masterVol;
};


// ******
// VabRgn
// ******

class VabRgn
	: public VGMRgn
{
public:
	VabRgn(VabInstr* instr, ULONG offset);
	//virtual ~WDRgn(void) {}
	//virtual int OnSelected(void);

	virtual int LoadRgn();

public:
	unsigned short ADSR1;				//raw ps2 ADSR1 value (articulation data)
	unsigned short ADSR2;				//raw ps2 ADSR2 value (articulation data)
	unsigned char bStereoRegion;
	unsigned char StereoPairOrder;
	unsigned char bFirstRegion;
	unsigned char bLastRegion;
	unsigned char bUnknownFlag2;
	ULONG sample_offset;

	VagAtr attr;
};
