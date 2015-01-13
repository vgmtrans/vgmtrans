#pragma once

#include "VGMSeq.h"
#include "VGMSeqSection.h"

class VGMMultiSectionSeq :
	public VGMSeq
{
public:
	VGMMultiSectionSeq(const std::string& format, RawFile* file, uint32_t offset, uint32_t length = 0, std::wstring name = L"VGM Sequence");
	virtual ~VGMMultiSectionSeq();

	virtual void ResetVars();
	virtual bool LoadMain();

	void AddSection(VGMSeqSection* section);
	VGMSeqSection* GetSectionFromOffset(uint32_t offset);

protected:
	virtual bool LoadTracks(ReadMode readMode, long stopTime = 1000000);
	virtual bool LoadSection(VGMSeqSection* section, long stopTime = 1000000);
	virtual bool IsOffsetUsed(uint32_t offset);
	virtual bool ReadEvent();

public:
	uint32_t dwStartOffset;
	uint32_t curOffset;

	std::vector<VGMSeqSection*> aSections;

protected:
	int foreverLoops;

private:
	virtual bool GetTrackPointers(void) { return false; }
};
