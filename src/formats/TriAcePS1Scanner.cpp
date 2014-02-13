#include "stdafx.h"
#include "TriAcePS1Scanner.h"
#include "TriAcePS1Seq.h"
#include "TriAcePS1InstrSet.h"
#include "VGMColl.h"
#include "VGMFile.h"

using namespace std;

#define DEFAULT_UFSIZE 0x100000


TriAcePS1Scanner::TriAcePS1Scanner(void)
{
}

TriAcePS1Scanner::~TriAcePS1Scanner(void)
{
}

void TriAcePS1Scanner::Scan(RawFile* file, void* info)
{
	SearchForSLZSeq(file);
	return;
}

void TriAcePS1Scanner::SearchForSLZSeq (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+0x40<nFileLength; i++)
	{
		uint32_t sig1 = file->GetWordBE(i);

		if (sig1 != 0x534C5A01)	// "SLZ" + 0x01 in ASCII
			continue;

		uint16_t headerBytes = file->GetShort(i+0x11);

		if (headerBytes != 0xFFFF)		//First two bytes of the sequence is always 0xFFFF
			continue;

		uint32_t size1 = file->GetWord(i+4);		//unknown.  compressed size or something
		uint32_t size2 = file->GetWord(i+8);		//uncompressed file size (size of resulting file after decompression)
		uint32_t size3 = file->GetWord(i+12);	//unknown compressed file size or something

		if (size1 > 0x30000 || size2 > 0x30000 || size3 > 0x30000)	//sanity check.  Sequences won't be > 0x30000 bytes
			continue;
		if (size1 > size2 || size3 > size2)	//the compressed file size ought to be smaller than the decompressed size
			continue;
		
		TriAcePS1Seq* seq = TriAceSLZ1Decompress(file, i);
		if (!seq)
			return;
		if (i < 4 || file->GetWord(i-4) == 0)
			return;

		// The size of the compressed file is 4 bytes behind the SLZ sig.  
		// We need this to calculate the offset of the InstrSet, which comes immediately after the SLZ'd sequence.
		vector<TriAcePS1InstrSet*> instrsets;
		SearchForInstrSet(file, instrsets);

		//uint32_t cfSize = file->GetWord(i-4);
		//TriAcePS1InstrSet* instrset = new TriAcePS1InstrSet(file, i-4 + cfSize);
		//if (!instrset->LoadVGMFile())
		//{
		//	delete instrset;
		//	return;
		//}
		if (!instrsets.size())
			return;

		VGMColl* coll = new VGMColl(_T("TriAce Song"));
		coll->UseSeq(seq);
		for (UINT i=0; i<instrsets.size(); i++)
			coll->AddInstrSet(instrsets[i]);
		if (!coll->Load())
		{
			delete coll;
		}
	}
}

void TriAcePS1Scanner::SearchForInstrSet (RawFile* file, vector<TriAcePS1InstrSet*>& instrsets)
{
	UINT nFileLength = file->size();
	for (UINT i=4; i+0x800<nFileLength; i++)
	{
		uint8_t precedingByte = file->GetByte(i+3);
		if (precedingByte != 0)
			continue;
		
		//The 32 byte value at offset 8 seems to be bank #.  In practice, i don't think it is ever > 1
		if (file->GetWord(i+8) > 0xFF)
			continue;

		uint16_t instrSectSize = file->GetShort(i+4);
		//The instrSectSize should be more than the size of one instrdata block and not insanely large
		if (instrSectSize <= 0x20 || instrSectSize > 0x4000)
			continue;
		//Make sure the size doesn't point beyond the end of the file
		if (i + instrSectSize > file->size() - 0x100)
			continue;

		
		uint32_t instrSetSize = file->GetWord(i);
		if (instrSetSize < 0x1000)
			continue;
		//The entire InstrSet size must be larger than the instrdata region, of course
		if (instrSetSize <= instrSectSize)
			continue;

		//First 0x10 bytes of sample section should be 0s
		if (file->GetWord(i+instrSectSize) != 0 || file->GetWord(i+instrSectSize+4) != 0 ||
			file->GetWord(i+instrSectSize+8) != 0 || file->GetWord(i+instrSectSize+12) != 0)
			continue;
		//Last 4 bytes of Instr section should be 0xFFFFFFFF
		if (file->GetWord(i+instrSectSize-4) != 0xFFFFFFFF)
			continue;


		TriAcePS1InstrSet* instrset = new TriAcePS1InstrSet(file, i);
		if (!instrset->LoadVGMFile())
		{
			delete instrset;
			continue;
		}
		instrsets.push_back(instrset);
	}
	return;
}



//file is RawFile containing the compressed seq.  cfOff is the compressed file offset.
TriAcePS1Seq* TriAcePS1Scanner::TriAceSLZ1Decompress(RawFile* file, ULONG cfOff)
{
	uint32_t cfSize = file->GetWord(cfOff+4);			//compressed file size
	uint32_t ufSize = file->GetWord(cfOff+8);			//uncompressed file size (size of resulting file after decompression)
	uint32_t blockSize = file->GetWord(cfOff+12);		//size of entire compressed block (slightly larger than cfSize

	if (ufSize == 0)
		ufSize = DEFAULT_UFSIZE;

	BYTE* uf = new BYTE[ufSize];

	bool bDone = false;
	ULONG ufOff = 0;
	cfOff += 0x10;
	while (ufOff < ufSize && !bDone)
	{
		BYTE cFlags = file->GetByte(cfOff++);
		for (int i=0; (i<8) && (ufOff < ufSize); i++, cFlags>>=1)
		{
			if (cFlags & 1)				//uncompressed byte, just copy it over
				uf[ufOff++] = file->GetByte(cfOff++);
			else						//compressed section
			{
				BYTE byte1 = file->GetByte(cfOff);
				BYTE byte2 = file->GetByte(cfOff+1);

				if (byte1 == 0 && byte2 == 0)
				{
					bDone = true;
					break;
				}

				ULONG backPtr = ufOff - (((byte2 & 0x0F)<<8) + byte1);
				BYTE bytesToRead = (byte2 >> 4) + 3;
				
				for (; bytesToRead > 0; bytesToRead--)
					uf[ufOff++] = uf[backPtr++];
				cfOff += 2;
			}
		}		
	}
	if (ufOff > ufSize)
		Alert(L"ufOff > ufSize!");

	//If we had to use DEFAULT_UFSIZE because the uncompressed file size was not given (Valkyrie Profile),
	//then create a new buffer of the correct size now that we know it, and delete the old one.
	if (ufSize == DEFAULT_UFSIZE)
	{
		BYTE* newUF = new BYTE[ufOff];
		memcpy(newUF, uf, ufOff);
		delete[] uf;
		uf = newUF;
	}
	//pRoot->UI_WriteBufferToFile(L"uncomp.raw", uf, ufOff);

	//Create the new virtual file, and analyze the sequence
	VirtFile* newVirtFile = newVirtFile = new VirtFile(uf, ufOff, L"TriAce Seq", file->GetParRawFileFullPath().c_str());

	TriAcePS1Seq* newSeq = new TriAcePS1Seq(newVirtFile, 0);
	bool bLoadSucceed = newSeq->LoadVGMFile();

	newVirtFile->DontUseLoaders();
	newVirtFile->DontUseScanners();
	pRoot->SetupNewRawFile(newVirtFile);

	if (bLoadSucceed)
		return newSeq;
	else
	{
		delete newSeq;
		return NULL;
	}
}