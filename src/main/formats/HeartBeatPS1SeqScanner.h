#pragma once
#include "Scanner.h"
#include "HeartBeatPS1Seq.h"
#include "Vab.h"

class HeartBeatPS1SeqScanner :
	public VGMScanner
{
public:
	HeartBeatPS1SeqScanner(void);
	virtual ~HeartBeatPS1SeqScanner(void);

	virtual void Scan(RawFile* file, void* info = 0);
	std::vector<HeartBeatPS1Seq*> SearchForHeartBeatPS1Seq (RawFile* file);
	std::vector<Vab*> SearchForVab (RawFile* file);
};
