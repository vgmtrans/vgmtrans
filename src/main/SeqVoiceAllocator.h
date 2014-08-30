
#pragma once

#include <vector>
#include "SeqNote.h"

class SeqVoiceAllocator
{
public:
	SeqVoiceAllocator();
	virtual ~SeqVoiceAllocator();

	/** Maximum number of tracks */
	enum { TRACK_COUNT = 128 };

	/** Channel specific parameters */
	struct SeqVoiceChannel {
		SeqVoiceChannel() :
			tied(false)
		{
		}

		bool tied;
	} tracks[TRACK_COUNT];

	/** Get output MIDI file */
	MidiFile * GetMidiFile();
	/** Set output MIDI file */
	void SetMidiFile(MidiFile * midiFile);

	/** Get current time */
	int32_t GetTime();
	/** Increment current time */
	void Tick();

	/** Add new note */
	void NoteOn(int8_t track, int8_t channel, int8_t key, int8_t velocity);
	/** Add new note with duration */
	void NoteByDur(int8_t track, int8_t channel, int8_t key, int8_t velocity, int32_t duration);
	/** Remove a note */
	void NoteOff(int8_t track, int8_t channel, int8_t key);
	/** Remove all notes of specific track */
	void TrackNotesOff(int8_t track);
	/** Remove all notes */
	void AllNotesOff();
	/** Remove all notes and reset timer */
	void Clear();
	/** Add duration to the notes of a track */
	void AddDuration(int8_t track, int32_t delta_time);

private:
	/** Remove a note from list */
	void NoteOff(std::list<SeqNote>::iterator note);

	/** Current time in ticks */
	int32_t time;
	/** Output MIDI file */
	MidiFile * midiFile;
	/** MIDI notes */
	std::list<SeqNote> notes;
};
