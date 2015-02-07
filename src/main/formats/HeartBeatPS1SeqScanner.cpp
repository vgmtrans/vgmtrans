#include "stdafx.h"
#include "HeartBeatPS1SeqScanner.h"
#include "HeartBeatPS1Format.h"
#include "HeartBeatPS1Seq.h"
#include "Vab.h"
#include "PSXSPU.h"

#define SRCH_BUF_SIZE 0x20000

HeartBeatPS1SeqScanner::HeartBeatPS1SeqScanner(void)
{
}

HeartBeatPS1SeqScanner::~HeartBeatPS1SeqScanner(void)
{
}


void HeartBeatPS1SeqScanner::Scan(RawFile* file, void* info)
{
	SearchForHeartBeatPS1Seq(file);
	return;
}

std::vector<HeartBeatPS1Seq*> HeartBeatPS1SeqScanner::SearchForHeartBeatPS1Seq (RawFile* file)
{
	uint32_t nFileLength = file->size();
	std::vector<HeartBeatPS1Seq*> loadedFiles;
	for (uint32_t i=0; i+4<nFileLength; i++)
	{
		if ((*file)[i] == 'q' && (*file)[i+1] == 'Q' && (*file)[i+2] == 'E' && (*file)[i+3] == 'S')
		{
			HeartBeatPS1Seq* newHeartBeatPS1Seq = new HeartBeatPS1Seq(file, i);
			if (newHeartBeatPS1Seq->LoadVGMFile())
			{
				loadedFiles.push_back(newHeartBeatPS1Seq);
			}
			else
			{
				delete newHeartBeatPS1Seq;
			}
		}
	}
	return loadedFiles;
}
