#include "stdafx.h"
#include "common.h"
#include "NDSScanner.h"
#include "NDSSeq.h"
#include "NDSInstrSet.h"
#include "NDSFormat.h"

#define SRCH_BUF_SIZE 0x20000


void NDSScanner::Scan(RawFile* file, void* info)
{
	SearchForSDAT(file);
	return;
}

void NDSScanner::SearchForSDAT (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+4<nFileLength; i++)
	{
		if ((*file)[i] == 'S' && (*file)[i+1] == 'D' && (*file)[i+2] == 'A' && (*file)[i+3] == 'T'
			 && (*file)[i+4] == 0xFF && (*file)[i+5] == 0xFE && (*file)[i+6] == 0
			 && (*file)[i+7] == 0x01 && (file->GetShort(i+12) < 0x100)
			 && (file->GetWord(i+0x10) < 0x10000) )
		{
			i += LoadFromSDAT(file, i);
		}
	}
}


// The following is pretty god-awful messy.  I should have created structs for the different
// blocks and loading the entire blocks at a time.  
ULONG NDSScanner::LoadFromSDAT(RawFile* file, ULONG baseOff)
{
	ULONG SYMBoff;
	ULONG INFOoff;
	ULONG FAToff; 
	ULONG nSeqs;
	ULONG nBnks;
	ULONG nWAs;
	vector<wstring> seqNames;
	vector<wstring> bnkNames;
	vector<wstring> waNames;
	vector<USHORT> seqFileIDs;
	vector<USHORT> bnkFileIDs;
	vector<USHORT> waFileIDs;
	vector<USHORT> seqFileBnks;
	vector<vector<USHORT> > bnkWAs;
	vector<NDSWaveArch*> WAs;
	vector <pair<USHORT, NDSInstrSet*> > BNKs;

	ULONG SDATLength = file->GetWord(baseOff+8) + 8;

	SYMBoff = file->GetWord(baseOff+0x10) + baseOff;
	INFOoff = file->GetWord(baseOff+0x18) + baseOff;
	FAToff = file->GetWord(baseOff+0x20) + baseOff;
	bool hasSYMB = (SYMBoff != baseOff);

	nSeqs = file->GetWord(file->GetWord(INFOoff + 0x08) + INFOoff);
	nBnks = file->GetWord(file->GetWord(INFOoff + 0x10) + INFOoff);
	nWAs  = file->GetWord(file->GetWord(INFOoff + 0x14) + INFOoff);

	ULONG pSeqNamePtrList;
	ULONG pBnkNamePtrList;
	ULONG pWANamePtrList;
	if(hasSYMB)
	{
		pSeqNamePtrList = file->GetWord(SYMBoff + 0x08) + SYMBoff;		//get pointer to list of sequence name pointers
		pBnkNamePtrList = file->GetWord(SYMBoff + 0x10) + SYMBoff;		//get pointer to list of bank name pointers
		pWANamePtrList  = file->GetWord(SYMBoff + 0x14) + SYMBoff;		//get pointer to list of wavearchive name pointers
	}

	for (ULONG i=0; i<nSeqs; i++)
	{
		char temp[32];		//that 32 is totally arbitrary, i should change it
		wchar_t wtemp[32];
		if(hasSYMB)
		{
			file->GetBytes(file->GetWord(pSeqNamePtrList+4+i*4)+SYMBoff, 32, temp);
			mbstowcs(wtemp, temp, 32);
		}
		else
		{
			wsprintf(wtemp, L"SSEQ_%04d", i);
		}
		seqNames.push_back(wtemp);
	}

	for (ULONG i=0; i<nBnks; i++)
	{
		char temp[32];		//that 32 is totally arbitrary, i should change it
		wchar_t wtemp[32];

		if(hasSYMB)
		{
			file->GetBytes(file->GetWord(pBnkNamePtrList+4+i*4)+SYMBoff, 32, temp);
			mbstowcs(wtemp, temp, 32);
		}
		else
		{
			wsprintf(wtemp, L"SBNK_%04d", i);
		}
		bnkNames.push_back(wtemp);
	}

	for (ULONG i=0; i<nWAs; i++)
	{
		char temp[32];		//that 32 is totally arbitrary, i should change it
		wchar_t wtemp[32];

		if(hasSYMB)
		{
			file->GetBytes(file->GetWord(pWANamePtrList+4+i*4)+SYMBoff, 32, temp);
			mbstowcs(wtemp, temp, 32);
		}
		else
		{
			wsprintf(wtemp, L"SWAR_%04d", i);
		}
		waNames.push_back(wtemp);
	}

	ULONG pSeqInfoPtrList = file->GetWord(INFOoff + 8) + INFOoff;
	//ULONG seqInfoPtrListLength = file->GetWord(INFOoff + 12);
	ULONG nSeqInfos = file->GetWord(pSeqInfoPtrList);
	for (ULONG i=0; i<nSeqInfos; i++)
	{
		ULONG pSeqInfoUnadjusted = file->GetWord(pSeqInfoPtrList+4+ i*4);
		ULONG pSeqInfo = INFOoff + pSeqInfoUnadjusted;
		if (pSeqInfoUnadjusted == 0)
			seqFileIDs.push_back((USHORT)-1);
		else
			seqFileIDs.push_back(file->GetShort(pSeqInfo));
		seqFileBnks.push_back(file->GetShort(pSeqInfo+4));
		//next bytes would be vol, cpr, ppr, and ply respectively, whatever the heck those last 3 stand for
	}

	ULONG pBnkInfoPtrList = file->GetWord(INFOoff+ 0x10) + INFOoff;
	ULONG nBnkInfos = file->GetWord(pBnkInfoPtrList);
	for (ULONG i=0; i<nBnkInfos; i++)
	{
		ULONG pBnkInfoUnadjusted = file->GetWord(pBnkInfoPtrList+4+ i*4);
		ULONG pBnkInfo = INFOoff + pBnkInfoUnadjusted;
		if (pBnkInfoUnadjusted == 0)
			bnkFileIDs.push_back((USHORT)-1);
		else
			bnkFileIDs.push_back(file->GetShort(pBnkInfo));
		bnkWAs.push_back(vector<USHORT>());
		vector<vector<USHORT> >::reference ref = bnkWAs.back();
		for (int j=0; j<4; j++)
		{
			USHORT WANum = file->GetShort(pBnkInfo+4+(j*2));
			//if (WANum > 0x200)			//insanity check
			if (WANum >= nWAs)
				ref.push_back(0xFFFF);
			else
				ref.push_back(WANum);
		}
	}

	ULONG pWAInfoList = file->GetWord(INFOoff + 0x14) + INFOoff;
	ULONG nWAInfos = file->GetWord(pWAInfoList);
	for (ULONG i=0; i<nWAInfos; i++)
	{
		ULONG pWAInfoUnadjusted = file->GetWord(pWAInfoList+4+ i*4);
		ULONG pWAInfo = INFOoff + pWAInfoUnadjusted;
		if (pWAInfoUnadjusted == 0)
			waFileIDs.push_back((USHORT)-1);
		else
			waFileIDs.push_back(file->GetShort(pWAInfo));
	}

	{
		vector<USHORT> vUniqueWAs;// = vector<USHORT>(bnkWAs);
		for (UINT i=0; i<bnkWAs.size(); i++)
			vUniqueWAs.insert(vUniqueWAs.end(), bnkWAs[i].begin(), bnkWAs[i].end());
		sort(vUniqueWAs.begin(), vUniqueWAs.end());
		vector<USHORT>::iterator new_end = unique(vUniqueWAs.begin(), vUniqueWAs.end());

		vector<bool> valid;
		valid.resize(nWAs);
		for (vector<USHORT>::iterator iter = vUniqueWAs.begin(); iter != new_end; iter++)
		{
			if ((*iter != (USHORT)-1) && (*iter < valid.size()))
				valid[*iter] = 1;
		}

		for (ULONG i=0; i<nWAs; i++)
		//for (vector<USHORT>::iterator iter = vUniqueWAs.begin(); iter != new_end; iter++)
		{
			if (valid[i] != 1  || waFileIDs[i] == (USHORT)-1)
			{
				WAs.push_back(NULL);
				continue;
			}
			ULONG offset = FAToff+ 12 + waFileIDs[i]*0x10;
			ULONG pWAFatData = file->GetWord(offset) + baseOff;
			offset += 4;
			ULONG fileSize = file->GetWord(offset);
			NDSWaveArch* NewNDSwa = new NDSWaveArch(file, pWAFatData, fileSize, waNames[i]);
			if (!NewNDSwa->LoadVGMFile())
			{
				pRoot->AddLogItem(new LogItem(FormatString<wstring>(L"Failed to load NDSWaveArch at 0x%08x\n", pWAFatData).c_str(), LOG_LEVEL_ERR, L"NDSScanner"));
				WAs.push_back(NULL);
				delete NewNDSwa;
				continue;
			}
			WAs.push_back(NewNDSwa);
		}
	}

	{
		vector<USHORT> vUniqueBanks = vector<USHORT>(seqFileBnks);
		sort(vUniqueBanks.begin(), vUniqueBanks.end());
		vector<USHORT>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

		//for (ULONG i=0; i<nBnks; i++)
		//for (ULONG i=0; i<seqFileBnks.size(); i++)
		for (vector<USHORT>::iterator iter = vUniqueBanks.begin(); iter != new_end; iter++)
		{
			if (*iter >= bnkFileIDs.size() /*0x1000*/ || bnkFileIDs[*iter] == (USHORT)-1)	// > 0x1000 is idiot test for Phoenix Wright, which had many 0x1C80 values, as if they were 0xFFFF
				continue;
			ULONG offset = FAToff+ 12 + bnkFileIDs[*iter]*0x10;
			ULONG pBnkFatData = file->GetWord(offset) + baseOff;
			offset += 4;
			ULONG fileSize = file->GetWord(offset);
			//if (bnkWAs[*iter][0] == (USHORT)-1 || numWAs != 1)
			//	continue;
			NDSInstrSet* NewNDSInstrSet = new NDSInstrSet(file, pBnkFatData, fileSize,
											  bnkNames[*iter]/*, WAs[bnkWAs[*iter][0]]*/);
			for (int i = 0; i < 4; i++)		//use first WA found.  Ideally, should load all WAs
			{
				short WAnum = bnkWAs[*iter][i];
				if (WAnum != -1)
					NewNDSInstrSet->sampCollWAList.push_back(WAs[WAnum]);
				else
					NewNDSInstrSet->sampCollWAList.push_back(NULL);
			}
			if (!NewNDSInstrSet->LoadVGMFile())
			{
				pRoot->AddLogItem(new LogItem(FormatString<wstring>(L"Failed to load NDSInstrSet at 0x%08x\n", pBnkFatData).c_str(), LOG_LEVEL_ERR, L"NDSScanner"));
			}
			pair<USHORT, NDSInstrSet*> theBank(*iter, NewNDSInstrSet);
			BNKs.push_back(theBank);
		}
	}

	{
		//vector<USHORT> vUniqueSeqs = vector<USHORT>(seqFileIDs);
		//sort(vUniqueSeqs.begin(), vUniqueSeqs.end());
		//vector<USHORT>::iterator new_end = unique(vUniqueBanks.begin(), vUniqueBanks.end());

		for (ULONG i=0; i<nSeqs; i++)
		{
			if (seqFileIDs[i] == (USHORT)-1)
				continue;
			ULONG offset = FAToff+ 12 + seqFileIDs[i]*0x10;
			ULONG pSeqFatData = file->GetWord(offset) + baseOff;
			offset += 4;
			ULONG fileSize = file->GetWord(offset);
			NDSSeq* NewNDSSeq = new NDSSeq(file, pSeqFatData, fileSize, seqNames[i]);
			if (!NewNDSSeq->LoadVGMFile())
			{
				pRoot->AddLogItem(new LogItem(FormatString<wstring>(L"Failed to load NDSSeq at 0x%08x\n", pSeqFatData).c_str(), LOG_LEVEL_ERR, L"NDSScanner"));
			}

			VGMColl* coll = new VGMColl(seqNames[i]);
			coll->UseSeq(NewNDSSeq);
			UINT bnkIndex = 0;
			for (UINT j=0; j<BNKs.size(); j++)
			{
				if (seqFileBnks[i] == BNKs[j].first)
				{
					bnkIndex = j;		
					break;
				}
			}
			NDSInstrSet* instrset = BNKs[bnkIndex].second;
			coll->AddInstrSet(BNKs[bnkIndex].second);
			for(int j=0; j<4; j++)
			{
				short WAnum = bnkWAs[seqFileBnks[i]][j];
				if (WAnum != -1)
					coll->AddSampColl(WAs[WAnum]);
			}
			if (!coll->Load())
			{
				delete coll;
			}
		}
	}
	return SDATLength;
}




/*void NDSScanner::SearchForNDSSeq (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+4<nFileLength; i++)
	{
		if ((*file)[i] == 'S' && (*file)[i+1] == 'S' && (*file)[i+2] == 'E' && (*file)[i+3] == 'Q')
		{
			//if (file->GetShort(i+10) == 0 && file->GetShort(i+16) == 0)
			//{
				NDSSeq* NewNDSSeq = new NDSSeq(file, i);
				NewNDSSeq->Load();
			//}
		}
	}
}*/