/**
 * Standard MIDI File main class
 * 
 * This is part of the GBA SongRiper (c) 2012 by Bregalad
 * This is free and open source software.
 * 
 * Notes : I tried to separate the GBA-related stuff and MIDI related stuff as much as possible in different classes
 * that way anyone can re-use this program for building MIDIs out of different data.
 * 
 * SMF is a sequenced music file format based on MIDI interface. This class (and related classes) serves to create
 * data to build a format 0 MIDI file. I restricted it to format 0 MIDIS because it was simpler to only have one
 * single track to deal with. It wouldn't be too hard to upgrade this class to support format 1 MIDIs, though.
 * 
 * All MIDI classes contains a field named midi that links to the MIDI main class to know which
 * MIDI file they relates to. (this would make it possible to build multiple SF2 files at a time).
 * Adding MIDI events is made extremely simple by a set of functions in this class. However if this is not sufficient
 * it is always possible to use the general purpose add_event() function, or adding data bytes directly to output.
 * 
 * The MIDI standard uses the concept of delta_times before events. Since this is very much unpractical, this
 * class supposes the user to simply increment the time_ctr variable when needed, and not worry about delta
 * time values anmore.
 * 
 * The building of a MIDI file is done in two passes :
 * - Creating the sequence data which is cached in memory
 * - Writing sequence data from cache to output file
 */

#include <string.h>
#include "midi.hpp"

// Constructor : Initialise the MIDI object
MIDI::MIDI(uint16_t delta_time)
{
	delta_time_per_beat = delta_time;
	for(int i=15; i >= 0; --i)
	{
		last_rpn_type[i] = -1;
		last_nrpn_type[i] = -1;
		last_type[i] = -1;
		chn_reorder[i] = i;
	}
	last_chanel = -1;
	time_ctr = 0;
}

//Write cached data to midi file
void MIDI::write(FILE *out)
{
	//Add end-of-track meta event
	data.push_back(0x00);
	data.push_back(0xff);
	data.push_back(0x2f);
	data.push_back(0x00);

	//Write MIDI header
	struct
	{
		char name[4];
		uint32_t hdr_len;
		uint16_t format;
		uint16_t ntracks;
		uint16_t division;
	} mthd_chunk =
	{
		{'M','T','h','d'},
		// 6,		// Length of header in bytes (always 6)
		// 0,		// We use MIDI format 0
		// 1,		// There is one single track

		// Swap the endianness
		0x06000000,
		0x0000,
		0x0100,
		(delta_time_per_beat << 8) | (delta_time_per_beat >> 8)
	};
	fwrite(&mthd_chunk, 1, 14, out);

	//Write MIDI track data
	//we use SMF-0 standard therefore there is only a single track :)
	uint32_t s = data.size();
	struct
	{
		char name[4];
		uint32_t size;
	} trdata =
	{
		{'M','T','r','k'},
		// Again, swap endianness
		(s << 24) | ((s & 0x0000ff00) << 8) | ((s & 0x00ff0000) >> 8) | (s >> 24)
	};
	fwrite(&trdata, 1, 8, out);

	//Write the track itself
	fwrite(&data[0], data.size(), 1, out);

	fclose(out);
}

//Add delta time in MIDI variable length coding
void MIDI::add_vlength_code(int code)
{
	char word1 = code & 0x7f;
	char word2 = (code >> 7) & 0x7f;
	char word3 = (code >> 14) & 0x7f;
	char word4 = (code >> 21) & 0x7f;

	if(word4 != 0)
	{
		data.push_back(word4 | 0x80);
		data.push_back(word3 | 0x80);
		data.push_back(word2 | 0x80);
	}
	else if (word3 != 0)
	{
		data.push_back(word3 | 0x80);
		data.push_back(word2 | 0x80);
	}
	else if (word2 != 0)
	{
		data.push_back(word2 | 0x80);
	}
	data.push_back(word1);
}

//Add delta time event
void MIDI::add_delta_time()
{
	add_vlength_code(time_ctr);
	//Reset time counter to zero
	time_ctr = 0;
}

void MIDI::add_event(MIDIEventType type, int chn, char param1, char param2)
{
	add_delta_time();
	if(chn != last_chanel || type != last_event_type)
	{
		last_chanel = chn;
		last_event_type = type;
		data.push_back((type << 4) | chn_reorder[chn]);
	}
	data.push_back(param1);
	data.push_back(param2);
}

void MIDI::add_event(MIDIEventType type, int chn, char param)
{
	add_delta_time();
	if(chn != last_chanel || type != last_event_type)
	{
		last_chanel = chn;
		last_event_type = type;
		data.push_back((type << 4) | chn_reorder[chn]);
	}
	data.push_back(param);
}

//Add key on event
void MIDI::add_note_on(int chn, char key, char vel)
{
	add_event(NOTEON, chn, key, vel);
}

//Add key off event
void MIDI::add_note_off(int chn, char key, char vel)
{
	add_event(NOTEOFF, chn, key, vel);
}

//Add controller event
void MIDI::add_controller(int chn, char ctrl, char value)
{
	add_event(CONTROLLER, chn, ctrl, value);
}

//Add channel aftertouch
void MIDI::add_chanaft(int chn, char value)
{
	add_event(CHNAFT, chn, value);
}

//Add conventional program change event
void MIDI::add_pchange(int chn, char number)
{
	add_event(PCHANGE, chn, number);
}

//Add pitch bend event
void MIDI::add_pitch_bend(int chn, int16_t value)
{
	char lo = value & 0x7f;
	char hi = (value>>7) & 0x7f;
	add_event(PITCHBEND, chn, lo, hi);
}

//Add pitch bend event with only the MSB used
void MIDI::add_pitch_bend(int chn, char value)
{
	add_event(PITCHBEND, chn, 0, value);
}

//Add RPN event
void MIDI::add_RPN(int chn, int16_t type, int16_t value)
{
	if(last_rpn_type[chn] != type || last_type[chn] != 0)
	{
		last_rpn_type[chn] = type;
		last_type[chn] = 0;
		add_event(CONTROLLER, chn, 101, type>>7);
		add_event(CONTROLLER, chn, 100, type&0x7f);
	}
	add_event(CONTROLLER, chn, 6, value >> 7);

	if((value & 0x7f) != 0)
		add_event(CONTROLLER, chn, 38, value & 0x7f);
}

//Add NRPN event
void MIDI::add_NRPN(int chn, int16_t type, int16_t value)
{
	if(last_nrpn_type[chn] != type || last_type[chn] != 1)
	{
		last_nrpn_type[chn] = type;
		last_type[chn] = 1;
		add_event(CONTROLLER, chn, 99, type>>7);
		add_event(CONTROLLER, chn, 98, type&0x7f);
	}
	add_event(CONTROLLER, chn, 6, value >> 7);
	if((value & 0x7f) != 0)
		add_event(CONTROLLER, chn, 38, value & 0x7f);
}

void MIDI::add_marker(const char *text)
{
	add_delta_time();
	data.push_back(-1);
	//Add text meta event if marker is false, marker meta even if true
	data.push_back(6);
	size_t len = strlen(text);
	add_vlength_code(len);
	//Add text itself
	data.insert(data.end(), text, text+len);
}

void MIDI::add_sysex(const char sysex_data[], size_t len)
{
	add_delta_time();
	data.push_back(0xf0);
	//Actually variable length code
	add_vlength_code(len + 1);
	
	data.insert(data.end(), sysex_data, sysex_data+len);
	data.push_back(0xf7);
}

void MIDI::add_tempo(double tempo)
{
	int t = int(60000000.0 / tempo);
	char t1 = char(t);
	char t2 = char(t>>8);
	char t3 = char(t>>16);

	add_delta_time();
	data.push_back(0xff);
	data.push_back(0x51);
	data.push_back(0x03);
	data.push_back(t3);
	data.push_back(t2);
	data.push_back(t1);
}
