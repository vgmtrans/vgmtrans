#include "pch.h"
#include "TamSoftPS1Scanner.h"
#include "TamSoftPS1Seq.h"
#include "TamSoftPS1Instr.h"
#include "SNESDSP.h"

void TamSoftPS1Scanner::Scan(RawFile* file, void* info)
{
	std::wstring basename(RawFile::removeExtFromPath(file->GetFileName()));
	std::wstring extension(StringToLower(file->GetExtension()));

	if (extension == L"tsq") {
		for (uint8_t songIndex = 0; songIndex < TAMSOFTPS1_MAX_SONGS; songIndex++) {
			std::wstringstream seqname;
			seqname << basename << L" (" << songIndex << L")";

			TamSoftPS1Seq * newSeq = new TamSoftPS1Seq(file, 0, songIndex, seqname.str());
			if (newSeq->LoadVGMFile()) {
				newSeq->unLength = file->size();
			}
			else {
				delete newSeq;
			}
		}
	}
	else if (extension == L"tvb") {
		TamSoftPS1InstrSet * newInstrSet = new TamSoftPS1InstrSet(file, 0, basename);
		if (newInstrSet->LoadVGMFile()) {
			newInstrSet->unLength = file->size();
		}
		else {
			delete newInstrSet;
		}
	}

	return;
}
