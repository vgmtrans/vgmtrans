
#include "stdafx.h"

#include <vector>
#include <iterator>
#include <algorithm>

#include "SeqNoteManager.h"

static bool CompareNote(const SeqNoteParam& left, const SeqNoteParam& right)
{
	if (left.channel != right.channel)
		return left.channel < right.channel;
	else if (left.time != right.time)
		return left.time < right.time;
	else
		return left.key < right.key;
}

void SeqNoteManager::AddNote(SeqNoteParam note)
{
	notes.push_back(note);
}

void SeqNoteManager::AddNote(int channel, int time, int length, int key, int volume)
{
	SeqNoteParam note(channel, time, length, key, volume);
	AddNote(note);
}

void SeqNoteManager::Clear()
{
	notes.clear();
}

void SeqNoteManager::RemoveRange(int timeBegin, int timeEnd, int channel, SeqNoteParamCallback callback)
{
	sort(notes.begin(), notes.end(), CompareNote);

	if (timeBegin > timeEnd)
	{
		std::swap(timeBegin, timeEnd);
	}

	std::vector<SeqNoteParam>::iterator pNote = notes.begin();
	while (pNote != notes.end())
	{
		if (channel != -1 && pNote->channel != channel)
		{
			++pNote;
			continue;
		}

		int noteEndTime = pNote->time + pNote->length;
		if (pNote->time >= timeBegin && noteEndTime <= timeEnd)
		{
			if (callback != NULL)
			{
				callback(*pNote);
			}
			pNote = notes.erase(pNote);
		}
		else
		{
			++pNote;
		}
	}
}

void SeqNoteManager::TruncateAt(int time, int channel)
{
	std::vector<SeqNoteParam>::iterator pNote = notes.begin();
	while (pNote != notes.end())
	{
		if (channel != -1 && pNote->channel != channel)
		{
			++pNote;
			continue;
		}

		if (pNote->time >= time)
		{
			pNote = notes.erase(pNote);
		}
		else
		{
			if (pNote->time + pNote->length > time)
			{
				pNote->length = time - pNote->time;
			}
			++pNote;
		}
	}
}
