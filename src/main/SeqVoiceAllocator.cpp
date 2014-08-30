
#ifdef _WIN32
	#include "stdafx.h"
#endif
#include "SeqVoiceAllocator.h"

SeqVoiceAllocator::SeqVoiceAllocator() :
	time(0),
	midiFile(NULL)
{
}

SeqVoiceAllocator::~SeqVoiceAllocator()
{
}

MidiFile * SeqVoiceAllocator::GetMidiFile()
{
	return midiFile;
}

void SeqVoiceAllocator::SetMidiFile(MidiFile * midiFile)
{
	this->midiFile = midiFile;
}

int32_t SeqVoiceAllocator::GetTime()
{
	return time;
}

void SeqVoiceAllocator::Tick()
{
	std::list<SeqNote>::iterator note;
	std::list<SeqNote>::iterator nextNote;

	note = notes.begin();
	while (note != notes.end()) {
		nextNote = note;
		++nextNote;

		// remove expired note
		if (note->duration != 0 && note->time + note->duration >= time) {
			NoteOff(note);
		}

		note = nextNote;
	}

	time++;
}

void SeqVoiceAllocator::NoteOn(int8_t track, int8_t channel, int8_t key, int8_t velocity)
{
	SeqNote note(track, channel, time, key, velocity, 0);

	notes.push_back(note);
	if (midiFile != NULL) {
		note.AddNoteOn(midiFile);
	}
}

void SeqVoiceAllocator::NoteByDur(int8_t track, int8_t channel, int8_t key, int8_t velocity, int32_t duration)
{
	SeqNote note(track, channel, time, key, velocity, duration);

	notes.push_back(note);
	if (midiFile != NULL) {
		note.AddNoteOn(midiFile);
	}
}

void SeqVoiceAllocator::NoteOff(std::list<SeqNote>::iterator note)
{
	if (midiFile != NULL) {
		note->AddNoteOff(midiFile);
	}
	notes.erase(note);
}

void SeqVoiceAllocator::NoteOff(int8_t track, int8_t channel, int8_t key)
{
	std::list<SeqNote>::iterator note;
	std::list<SeqNote>::iterator nextNote;

	note = notes.begin();
	while (note != notes.end()) {
		nextNote = note;
		++nextNote;

		if (note->track == track && note->channel == channel && note->key == key)
		{
			NoteOff(note);
			break;
		}

		note = nextNote;
	}
}

void SeqVoiceAllocator::TrackNotesOff(int8_t track)
{
	std::list<SeqNote>::iterator note;
	std::list<SeqNote>::iterator nextNote;

	note = notes.begin();
	while (note != notes.end()) {
		nextNote = note;
		++nextNote;

		if (note->track == track) {
			NoteOff(note);
		}

		note = nextNote;
	}
}

void SeqVoiceAllocator::AllNotesOff()
{
	std::list<SeqNote>::iterator note;
	std::list<SeqNote>::iterator nextNote;

	note = notes.begin();
	while (note != notes.end()) {
		nextNote = note;
		++nextNote;

		NoteOff(note);

		note = nextNote;
	}
}

void SeqVoiceAllocator::Clear()
{
	AllNotesOff();
	time = 0;
}

void SeqVoiceAllocator::AddDuration(int8_t track, int32_t delta_time)
{
	if (delta_time <= 0) {
		return;
	}

	for (std::list<SeqNote>::iterator note = notes.begin(); note != notes.end(); ++note)
	{
		if (note->track == track && note->duration != 0) {
			note->duration += delta_time;
		}
	}
}
