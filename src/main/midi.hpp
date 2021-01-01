#ifndef MIDI_HPP
#define MIDI_HPP

#include <stdio.h>
#include <cstdint>
#include <vector>

enum MIDIEventType
{
	NOTEOFF=8,
	NOTEON=9,
	NOTEAFT=10,
	CONTROLLER=11,
	PCHANGE=12,
	CHNAFT=13,
	PITCHBEND=14
};

class MIDI
{
	uint16_t delta_time_per_beat;
	int16_t last_rpn_type[16];
	int16_t last_nrpn_type[16];
	int last_type[16];

	// Last event type and channel, used for compression
	int last_chanel;
	MIDIEventType last_event_type;
	// Time counter
	unsigned int time_ctr;
	// Track data
	std::vector<char> data;

	void add_vlength_code(int code);	// Add delta time in MIDI variable length coding
	// Add delta time event
	void add_delta_time();
	// Add any MIDI event
	void add_event(MIDIEventType type, int chn, char param1, char param2);
	void add_event(MIDIEventType type, int chn, char param1);

public:
	char chn_reorder[16];			// User can change the order of the channels

	MIDI(uint16_t delta_time);			// Construct a MIDI object
	void write(FILE*);					// Write cached data to midi file

	// Increment time by one clock
	inline void clock()
	{
		time_ctr += 1;
	}

	// Add an MIDI event to the stream
	void add_note_on(int chn, char key, char vel);
	void add_note_off(int chn, char key, char vel);
	void add_controller(int chn, char ctrl, char value);
	void add_chanaft(int chn, char value);
	void add_pchange(int chn, char number);
	void add_pitch_bend(int chn, int16_t value);
	void add_pitch_bend(int chn, char value);
	void add_RPN(int chn, int16_t type, int16_t value);
	//Add RPN event with only the MSB of value used
	inline void add_RPN(int chn, int16_t type, char value)
	{
		add_RPN(chn, type, int16_t(value<<7));
	}
	void add_NRPN(int chn, int16_t type, int16_t value);
	//Add NRPN event with only the MSB of value used
	inline void add_NRPN(int chn, int16_t type, char value)
	{
		add_NRPN(chn, type, int16_t(value<<7));
	}
	void add_marker(const char *text);
	void add_sysex(const char sysex_data[], size_t len);
	void add_tempo(double tempo);
};

#endif