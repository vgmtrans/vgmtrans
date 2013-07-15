#include "stdafx.h"
#include "MP2kScanner.h"
#include "MP2k.h"

#define SRCH_BUF_SIZE 0x20000

MP2kScanner::MP2kScanner(void)
{
	USE_EXTENSION(L"gba")
}

MP2kScanner::~MP2kScanner(void)
{
}

void MP2kScanner::Scan(RawFile* file, void* info)
{
	BYTE testByte, testByte2;
	UINT testWord, testWord2;
	USHORT testShort, testShort2;
	//CString text;
	BOOL bFirstTest = FALSE;

	UINT nFileLength = file->size();
	for (UINT i=0; i+8<nFileLength; i++)
	{
		testByte = (*file)[i+3];
		testByte2 = (*file)[i+3+4];
		if ((testByte == 2 || testByte == 3) && (testByte2 == 2 || testByte2 == 3))
		{
			testWord =  file->GetWord(i);
			testWord2 = file->GetWord(i+4);
			if (((testWord >= 0x2000000 && testWord <= 0x203FFFF) || (testWord >= 0x3000000 && testWord <= 0x3007FFF)) && 
				((testWord2 >= 0x2000000 && testWord2 <= 0x203FFFF) || (testWord2 >= 0x3000000 && testWord2 <= 0x3007FFF)))
			{
				testShort = file->GetShort(i+8);
				testShort2 = file->GetShort(i+10);
				if (testShort <= 0xFF && testShort <= testShort2 <= 0xFF)
				{
					testWord =  file->GetWord(i+12);
					testWord2 = file->GetWord(i+16);
					if (((testWord >= 0x2000000 && testWord <= 0x203FFFF) || (testWord >= 0x3000000 && testWord <= 0x3007FFF)) && 
					((testWord2 >= 0x2000000 && testWord2 <= 0x203FFFF) || (testWord2 >= 0x3000000 && testWord2 <= 0x3007FFF)))
					{
						testShort = file->GetShort(i+20);
						testShort2 = file->GetShort(i+22);
						if (testShort <= 0xFF && testShort <= testShort2 <= 0xFF)
							bFirstTest = TRUE;
					}
				}
			}
			if (bFirstTest == TRUE)
			{
				bFirstTest = FALSE;
				testWord = file->GetWord(i+24);
				testShort = file->GetShort(i+28);
				testShort2 = file->GetShort(i+30);
				testWord2 = file->GetWord(i+32);
				/*if (testWord >= 0x8000000 && testWord <= 0x9FFFFFF && testWord2 >= 0x8000000 && testWord2 <= 0x9FFFFFF && testShort <= 0xFF && testShort2 <= 0xFF)
				{
					text.Format(_T("Found Sapphire Seq Table at %X"), i);
					pDoc->SetDLLProgText(text);
					for (int k =0; k<10; k++)		//for number of sequences
					{	
						CSeqFile* NewSeqFile = new CSeqFile();
						aSeqFiles.Add(NewSeqFile);
						if (!NewSeqFile->LoadSeqFile(pDoc, pDoc->GetWord(pDoc, i+24 + k*8)-0x8000000))
						{
							delete NewSeqFile;
							aSeqFiles.RemoveAt(aSeqFiles.GetCount()-1);
						}
					}
				}*/
				if (testWord >= 0x8000000 && testWord <= 0x9FFFFFF && testWord2 >= 0x8000000 && testWord2 <= 0x9FFFFFF && testShort <= 0xFF && testShort2 <= 0xFF)
				{
					//text.Format(_T("Found MP2000 Seq Table at %X"), i);
					//pDoc->SetDLLProgText(text);

					//for (int k =0; k<10; k++) //for number of sequences

					int k=0;
					int l=0;
					int seqcount=0;

					while (testWord >= 0x8000000 && testWord <= 0x9FFFFFF && testShort <= 0xFF && testShort2 <= 0xFF)
					{
						seqcount++;
						testWord = file->GetWord(i+24 + seqcount*8);
						testShort = file->GetShort(i+28 + seqcount*8);
						testShort2 = file->GetShort(i+30 + seqcount*8);
					}

					testWord = file->GetWord(i+24);
					testShort = file->GetShort(i+28);
					testShort2 = file->GetShort(i+30);

					while (testWord >= 0x8000000 && testWord <= 0x9FFFFFF && testShort <= 0xFF && testShort2 <= 0xFF)
					{
						if(k>0)
						{
							for (l=0;l<k;l++)
							{
								testWord2 = file->GetWord(i+24 + l*8);
								if (testWord == testWord2)
									break;
							}

						}

						if(k==l)
						{
							//text.Format(_T("MP2000 Sequence #%d/%d"),k,seqcount);
							//pDoc->SetDLLProgText(text);
							ULONG seqOffset = file->GetWord(i+24 + k*8)-0x8000000;
							MP2kSeq* NewMP2kSeq = new MP2kSeq(file, seqOffset);//this, pDoc, pDoc->GetWord(i+24 + k*8)-0x8000000);
							//aMP2kSeqs.push_back(NewMP2kSeq);
							NewMP2kSeq->LoadVGMFile();
						}

						k++;
						testWord = file->GetWord(i+24 + k*8);
						testShort = file->GetShort(i+28 + k*8);
						testShort2 = file->GetShort(i+30 + k*8);
					}
				}
			}
		}
	}
	return;
}