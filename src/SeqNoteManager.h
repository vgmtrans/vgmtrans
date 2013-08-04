
#pragma once

#include <vector>
#include "SeqNoteParam.h"

typedef void (*SeqNoteParamCallback)(SeqNoteParam&);

class SeqNoteManager
{
public:
	void AddNote(SeqNoteParam note);
	void AddNote(int channel, int time, int length, int key, int volume);
	void Clear();
	void RemoveRange(int timeBegin, int timeEnd, int channel = -1, SeqNoteParamCallback callback = NULL);
	void TruncateAt(int time, int channel = -1);

private:
	std::vector<SeqNoteParam> notes;
};
