// Many thanks to bsnes and snes9x.

#include "stdafx.h"
#include "SNESDSP.h"
#include "RawFile.h"
#include "Root.h"

// ************
// SNESSampColl
// ************

SNESSampColl::SNESSampColl(const string& format, RawFile* rawfile, U32 offset) :
	VGMSampColl(format, rawfile, offset, 0),
	spcDirAddr(offset)
{
	SetDefaultTargets();
}

SNESSampColl::SNESSampColl(const string& format, VGMInstrSet* instrset, U32 offset) :
	VGMSampColl(format, instrset->rawfile, instrset, offset, 0),
	spcDirAddr(offset)
{
	SetDefaultTargets();
}

SNESSampColl::SNESSampColl(const string& format, RawFile* rawfile, U32 offset,
		const std::vector<BYTE>& targetSRCNs, std::wstring name) :
	VGMSampColl(format, rawfile, offset, 0, name),
	spcDirAddr(offset),
	targetSRCNs(targetSRCNs)
{
}

SNESSampColl::SNESSampColl(const string& format, VGMInstrSet* instrset, U32 offset,
		const std::vector<BYTE>& targetSRCNs, std::wstring name) :
	VGMSampColl(format, instrset->rawfile, instrset, offset, 0, name),
	spcDirAddr(offset),
	targetSRCNs(targetSRCNs)
{
}

SNESSampColl::~SNESSampColl()
{
}

void SNESSampColl::SetDefaultTargets()
{
	// fix table length
	UINT expectedLength = 4 * 256;

	// target all samples
	for (UINT i = 0; i < 256; i++)
	{
		if (i * 4 > expectedLength)
		{
			break;
		}

		targetSRCNs.push_back(i);
	}
}

bool SNESSampColl::GetSampleInfo()
{
	for (std::vector<BYTE>::iterator itr = this->targetSRCNs.begin(); itr != this->targetSRCNs.end(); ++itr)
	{
		BYTE srcn = (*itr);

		UINT offDirEnt = spcDirAddr + (srcn * 4);
		if (offDirEnt + 4 > 0x10000)
		{
			continue;
		}

		uint16_t addrSampStart = GetShort(offDirEnt);
		uint16_t addrSampLoop = GetShort(offDirEnt + 2);
		if (addrSampStart > addrSampLoop)
		{
			continue;
		}

		UINT length = SNESSamp::GetSampleLength(GetRawFile(), addrSampStart);
		if (length == 0)
		{
			continue;
		}

		wostringstream name;
		name << L"Sample " << srcn;
		SNESSamp* samp = new SNESSamp(this, addrSampStart, length, addrSampStart, length, addrSampLoop, name.str());
		samples.push_back(samp);
	}
	return samples.size() != 0;
}

//  ********
//  SNESSamp
//  ********

SNESSamp::SNESSamp(VGMSampColl* sampColl, ULONG offset, ULONG length, ULONG dataOffset,
				 ULONG dataLen, ULONG loopOffset, std::wstring name)
: VGMSamp(sampColl, offset, length, dataOffset, dataLen, 1, 16, 32000, name),
	brrLoopOffset(loopOffset)
{
}

SNESSamp::~SNESSamp()
{
}

ULONG SNESSamp::GetSampleLength(RawFile * file, ULONG offset)
{
	ULONG currOffset = offset;
	while (currOffset + 9 <= file->size())
	{
		BYTE flag = file->GetByte(currOffset);
		currOffset += 9;

		// end?
		if ((flag & 1) != 0 || (flag & 2) != 0)
		{
			break;
		}
	}
	return (currOffset - offset);
}

double SNESSamp::GetCompressionRatio()
{
	return ((16.0/9.0)*2); //aka 3.55...;
}

void SNESSamp::ConvertToStdWave(BYTE* buf)
{
	BRRBlk theBlock;
	int32_t prev1 = 0;
	int32_t prev2 = 0;

	// loopStatus is initiated to -1.  We should default it now to not loop
	SetLoopStatus(0);

	assert(dataLength % 9 == 0);
	for (UINT k = 0; k + 9 <= dataLength; k += 9)				//for every adpcm chunk
	{
		if (dwOffset + k + 9 > GetRawFile()->size())
		{
			wchar_t log[512];
			wsprintf(log,  L"\"%s\" unexpected EOF.", name.c_str());
			pRoot->AddLogItem(new LogItem(log, LOG_LEVEL_WARN, L"SNESSamp"));
			break;
		}

		theBlock.flag.range = (GetByte(dwOffset + k) & 0xf0) >> 4;
		theBlock.flag.filter = (GetByte(dwOffset + k) & 0x0c) >> 2;
		theBlock.flag.end = (GetByte(dwOffset + k) & 0x01) != 0; 
		theBlock.flag.loop = (GetByte(dwOffset+k) & 0x02) != 0;

		GetRawFile()->GetBytes(dwOffset + k + 1, 8, theBlock.brr);
		DecompBRRBlk((int16_t*)(&buf[k * 32 / 9]), &theBlock, &prev1, &prev2);	//each decompressed pcm block is 52 bytes   EDIT: (wait, isn't it 56 bytes? or is it 28?)

		if (theBlock.flag.loop && !theBlock.flag.end)
		{
			if (brrLoopOffset < k)
			{
				SetLoopOffset(brrLoopOffset);
				SetLoopLength((k + 9) - brrLoopOffset);
				SetLoopStatus(1);
			}
		}
		if (theBlock.flag.loop || theBlock.flag.end)
		{
			break;
		}
	}
}

static inline int32_t absolute (int32_t x)
{
	return ((x < 0) ? -x : x);
}

static inline int32_t sclip15 (int32_t x)
{
	return ((x & 16384) ? (x | ~16383) : (x & 16383));
}

static inline int32_t sclamp8 (int32_t x)
{
	return ((x > 127) ? 127 : (x < -128) ? -128 : x);
}

static inline int32_t sclamp15 (int32_t x)
{
	return ((x > 16383) ? 16383 : (x < -16384) ? -16384 : x);
}

static inline int32_t sclamp16 (int32_t x)
{
	return ((x > 32767) ? 32767 : (x < -32768) ? -32768 : x);
}

void SNESSamp::DecompBRRBlk(int16_t *pSmp, BRRBlk *pVBlk, int32_t *prev1, int32_t *prev2)
{
	int32_t out, S1, S2;
	int8_t  sample1, sample2;
	bool    validHeader;
	int     i, nybble;

	validHeader = (pVBlk->flag.range < 0xD);

	S1 = *prev1;
	S2 = *prev2;

	for (i = 0; i < 8; i++)
	{
		sample1 = pVBlk->brr[i];
		sample2 = sample1 << 4;
		sample1 >>= 4;
		sample2 >>= 4;

		for (nybble = 0; nybble < 2; nybble++)
		{
			out = nybble ? (int32_t) sample2 : (int32_t) sample1;
			out = validHeader ? ((out << pVBlk->flag.range) >> 1) : (out & ~0x7FF);

			switch (pVBlk->flag.filter)
			{
				case 0: // Direct
					break;

				case 1: // 15/16
					out += S1 + ((-S1) >> 4);
					break;

				case 2: // 61/32 - 15/16
					out += (S1 << 1) + ((-((S1 << 1) + S1)) >> 5) - S2 + (S2 >> 4);
					break;

				case 3: // 115/64 - 13/16
					out += (S1 << 1) + ((-(S1 + (S1 << 2) + (S1 << 3))) >> 6) - S2 + (((S2 << 1) + S2) >> 4);
					break;
			}

			out = sclip15(sclamp16(out));

			S2 = S1;
			S1 = out;

			pSmp[i * 2 + nybble] = out << 1;
		}
	}

	*prev1 = S1;
	*prev2 = S2;
}