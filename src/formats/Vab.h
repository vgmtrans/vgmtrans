#pragma once
#include "../VGMInstrSet.h"
#include "../VGMSampColl.h"
#include "../VGMRgn.h"

//VAB Header
struct VabHdr
{
	int32_t form; /*always "VABp"*/
	int32_t ver; /*format version number*/
	int32_t id; /*bank ID*/
	uint32_t fsize; /*file size*/
	uint16_t reserved0; /*system reserved*/
	uint16_t ps; /*total number of programs in this bank*/
	uint16_t ts; /*total number of effective tones*/
	uint16_t vs; /*number of waveforms (VAG)*/
	uint8_t mvol; /*master volume*/
	uint8_t pan; /*master pan*/
	uint8_t attr1; /*bank attribute*/
	uint8_t attr2; /*bank attribute*/
	uint32_t reserved1; /*system reserved*/
};


//Program Attributes
struct ProgAtr 
{
	uint8_t tones; /*number of effective tones which compose the program*/
	uint8_t mvol; /*program volume*/
	uint8_t prior; /*program priority*/
	uint8_t mode; /*program mode*/
	uint8_t mpan; /*program pan*/
	int8_t reserved0; /*system reserved*/
	int16_t attr; /*program attribute*/
	uint32_t reserved1; /*system reserved*/
	uint32_t reserved2; /*system reserved*/
};


//Tone Attributes
struct VagAtr
{
	uint8_t prior; /*tone priority (0 - 127); used for controlling allocation when more voices than can be keyed on are requested*/
	uint8_t mode; /*tone mode (0 = normal; 4 = reverb applied */
	uint8_t vol; /*tone volume*/
	uint8_t pan; /*tone pan*/
	uint8_t center; /*center note (0~127)*/
	uint8_t shift; /*pitch correction (0~127,cent units)*/
	uint8_t min; /*minimum note limit (0~127)*/
	uint8_t max; /*maximum note limit (0~127, provided min < max)*/
	uint8_t vibW; /*vibrato width (1/128 rate,0~127)*/
	uint8_t vibT; /*1 cycle time of vibrato (tick units)*/
	uint8_t porW; /*portamento width (1/128 rate, 0~127)*/
	uint8_t porT; /*portamento holding time (tick units)*/
	uint8_t pbmin; /*pitch bend (-0~127, 127 = 1 octave)*/
	uint8_t pbmax; /*pitch bend (+0~127, 127 = 1 octave)*/
	uint8_t reserved1; /*system reserved*/
	uint8_t reserved2; /*system reserved*/
	uint16_t adsr1; /*ADSR1*/
	uint16_t adsr2; /*ADSR2*/
	int16_t prog; /*parent program*/
	int16_t vag; /*waveform (VAG) used*/
	int16_t reserved[4]; /*system reserved*/
};



class Vab :
	public VGMInstrSet
{
public:
	Vab(RawFile* file, uint32_t offset);
	virtual ~Vab(void);

	virtual bool GetHeaderInfo();
	virtual bool GetInstrPointers();

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
	VabInstr(VGMInstrSet* instrSet, uint32_t offset, uint32_t length, uint32_t theBank, uint32_t theInstrNum, const std::wstring& name = L"Instrument");
	virtual ~VabInstr(void);

	virtual bool LoadInstr();

public:
	ProgAtr  attr;
	uint8_t masterVol;
};


// ******
// VabRgn
// ******

class VabRgn
	: public VGMRgn
{
public:
	VabRgn(VabInstr* instr, uint32_t offset);
	//virtual ~WDRgn(void) {}
	//virtual bool OnSelected(void);

	virtual bool LoadRgn();

public:
	uint16_t ADSR1;				//raw ps2 ADSR1 value (articulation data)
	uint16_t ADSR2;				//raw ps2 ADSR2 value (articulation data)
	uint8_t bStereoRegion;
	uint8_t StereoPairOrder;
	uint8_t bFirstRegion;
	uint8_t bLastRegion;
	uint8_t bUnknownFlag2;
	uint32_t sample_offset;

	VagAtr attr;
};
