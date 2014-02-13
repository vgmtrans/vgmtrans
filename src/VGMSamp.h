#pragma once
#include "VGMItem.h"
#include "Loop.h"
#include "Menu.h"

class VGMSampColl;




enum WAVE_TYPE {
	WT_UNDEFINED,
	WT_PCM8,
	WT_PCM16
};







class VGMSamp :
	public VGMItem
{
public:
	BEGIN_MENU(VGMSamp)
		MENU_ITEM(VGMSamp, OnSaveAsWav, L"Convert to WAV file")
	END_MENU()

	

	VGMSamp(VGMSampColl* sampColl, ULONG offset = 0, ULONG length = 0,
						ULONG dataOffset = 0, ULONG dataLength = 0, BYTE channels = 1, USHORT bps = 16,
						ULONG rate = 0, std::wstring name = L"Sample");
	virtual ~VGMSamp();

	virtual Icon GetIcon() { return ICON_SAMP; };

	virtual double GetCompressionRatio(); // ratio of space conserved.  should generally be > 1
								  // used to calculate both uncompressed sample size and loopOff after conversion
	virtual void ConvertToStdWave(BYTE* buf);

	inline void SetWaveType(WAVE_TYPE type) { waveType = type; }
	inline void SetBPS(USHORT theBPS) { bps = theBPS; }
	inline void SetRate(ULONG theRate) { rate = theRate; }
	inline void SetNumChannels(BYTE nChannels) { channels = nChannels; }
	inline void SetDataOffset(ULONG theDataOff) { dataOff = theDataOff; }
	inline void SetDataLength(ULONG theDataLength) { dataLength = theDataLength; }
	inline int  GetLoopStatus() { return loop.loopStatus; }
	inline void SetLoopStatus(int loopStat) { loop.loopStatus = loopStat; }
	inline void SetLoopOffset(ULONG loopStart) { loop.loopStart = loopStart; }
	inline int  GetLoopLength() { return loop.loopLength; }
	inline void SetLoopLength(ULONG theLoopLength) { loop.loopLength = theLoopLength; }
	inline void SetLoopStartMeasure(LoopMeasure measure) { loop.loopStartMeasure = measure; }
	inline void SetLoopLengthMeasure(LoopMeasure measure) { loop.loopLengthMeasure = measure; }

	bool OnSaveAsWav();
	bool SaveAsWav(const wchar_t* filepath);

public:
	WAVE_TYPE waveType;
	ULONG dataOff;	//offset of original sample data
	ULONG dataLength;
	USHORT bps;		//bits per sample
	ULONG rate;		//sample rate in herz (samples per second)
	BYTE channels;	//mono or stereo?
	ULONG ulUncompressedSize;
		
	bool bPSXLoopInfoPrioritizing;
	Loop loop;

	BYTE unityKey;
	short fineTune;
	double volume;	//as percent of full volume.  This will be converted to attenuation for SynthFile

	long pan;

	VGMSampColl* parSampColl;
	std::wstring sampName;
};