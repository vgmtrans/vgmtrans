#include "pch.h"
#include "KonamiGXScanner.h"
#include "KonamiGXSeq.h"
#include "MAMELoader.h"

void KonamiGXScanner::Scan(RawFile* file, void* info)
{
	MAMEGameEntry* gameentry = (MAMEGameEntry*)info;
	MAMERomGroupEntry* seqRomGroupEntry = gameentry->GetRomGroupOfType("soundcpu");
	MAMERomGroupEntry* sampsRomGroupEntry = gameentry->GetRomGroupOfType("shared");
	if (!seqRomGroupEntry || !sampsRomGroupEntry)
		return;
	uint32_t seq_table_offset;
	//uint32_t instr_table_offset;
	//uint32_t samp_table_offset;
	if (!seqRomGroupEntry->file || !sampsRomGroupEntry->file ||
		!seqRomGroupEntry->GetHexAttribute("seq_table", &seq_table_offset))// ||
		//!seqRomGroupEntry->GetHexAttribute("samp_table", &samp_table_offset))
		return;

	LoadSeqTable(seqRomGroupEntry->file, seq_table_offset);
	return;
}


void KonamiGXScanner::LoadSeqTable(RawFile* file, uint32_t offset)
{
	uint32_t nFileLength;
	nFileLength = file->size();
	while (offset < nFileLength)
	{
		uint32_t seqOffset = file->GetWordBE(offset);
		if (seqOffset == 0 || seqOffset >= nFileLength)
			break;
		KonamiGXSeq* newSeq = new KonamiGXSeq(file, seqOffset);
		if (!newSeq->LoadVGMFile())
			delete newSeq;
		offset += 12;
	}
}
