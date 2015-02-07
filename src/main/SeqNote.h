/**
 * Structure for a MIDI note.
 * This class is used by SeqVoiceAllocator class.
 */

#pragma once

#include <stdint.h>
#include "MidiFile.h"

class SeqNote
{
public:
	SeqNote(int8_t track, int8_t channel, int32_t time, int8_t key, int8_t velocity, int32_t duration) :
		track(track),
		channel(channel),
		time(time),
		key(key),
		velocity(velocity),
		duration(duration)
	{
	}

	virtual ~SeqNote()
	{
	}

	void AddNoteOn(MidiFile * midiFile)
	{
		midiFile->aTracks[track]->AddNoteOn(channel, key, velocity);
	}

	void AddNoteOff(MidiFile * midiFile)
	{
		midiFile->aTracks[track]->AddNoteOff(channel, key);
	}

	bool operator == (const SeqNote & obj) const {
		return time == obj.time && channel == obj.channel && key == obj.key;
	}

	bool operator != (const SeqNote & obj) const {
		return !(*this == obj);
	}

	bool operator < (const SeqNote & obj) const {
		if (time != obj.time) {
			return time < obj.time;
		}
		else if (channel != obj.channel) {
			return channel < obj.channel;
		}
		else {
			return key < obj.key;
		}
	}

	bool operator >= (const SeqNote & obj) const {
		return !(*this < obj);
	}

	bool operator > (const SeqNote & obj) const {
		return obj < *this;
	}

	bool operator <= (const SeqNote & obj) const {
		return !(obj < *this);
	}

public:
	/** Absolute timing (in ticks) */
	int32_t time;

	/** MIDI track index */
	int8_t track;

	/** MIDI channel number */
	int8_t channel;

	/** Key (0..127) */
	int8_t key;

	/** MIDI Velocity (0..127) */
	int8_t velocity;

	/** Duration (in ticks) */
	int32_t duration;
};
