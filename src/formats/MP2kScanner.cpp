/**
 * GBA MusicPlayer2000 (Sappy)
 *
 * Special Thanks / Respects To:
 * GBAMusRiper (c) 2012 by Bregalad
 * http://www.romhacking.net/utilities/881/
 * http://www.romhacking.net/documents/%5B462%5Dsappy.txt
 */

#ifdef _WIN32
	#include "stdafx.h"
#endif
#include "MP2kScanner.h"
#include "MP2k.h"

#define SRCH_BUF_SIZE 0x20000

MP2kScanner::MP2kScanner(void)
{
//	USE_EXTENSION(L"gba")
}

MP2kScanner::~MP2kScanner(void)
{
}

void MP2kScanner::Scan(RawFile* file, void* info)
{
	uint32_t mp2k_offset;

	// detect the engine
	if (!Mp2kDetector(file, mp2k_offset))
	{
		return;
	}

	// check address range
	if (mp2k_offset + 36 > file->size())
	{
		return;
	}

	// compute address of song table
	uint32_t song_levels = file->GetWord(mp2k_offset + 28);	// Read # of song levels
	uint32_t song_tbl_ptr = (file->GetWord(mp2k_offset + 32) & 0x1FFFFFF) + 12 * song_levels;

	// Ignores entries which are made of 0s at the start of the song table
	// this fix was necessarily for the game Fire Emblem
	uint32_t song_pointer;
	while(true)
	{
		// check address range
		if (song_tbl_ptr + 4 > file->size())
		{
			return;
		}

		song_pointer = file->GetWord(song_tbl_ptr);
		if(song_pointer != 0) break;
		song_tbl_ptr += 4;
	}

	unsigned int i = 0;
	while(true)
	{
		uint32_t song_entry = song_tbl_ptr + (i * 8);
		song_pointer = file->GetWord(song_entry) & 0x1FFFFFF;

		// Stop as soon as we met with an invalid pointer
		if(song_pointer == 0 || song_pointer >= file->size())
		{
			break;
		}

		MP2kSeq* NewMP2kSeq = new MP2kSeq(file, song_pointer);
		if (!NewMP2kSeq->LoadVGMFile())
		{
			delete NewMP2kSeq;
		}

		i++;
	};

	return;
}

// Sappy sound engine detector (c) 2013 by Bregalad
bool MP2kScanner::Mp2kDetector(RawFile* file, uint32_t & mp2k_offset)
{
	/* Get the size of the input GBA file */
	const uint32_t inGBA_length = file->size();

	uint32_t data = 0, data_l1, dummy, offset = 0;
	bool detected = false;

	/* This loops try to look for particular opcodes in the whole ROM
	   and this enables the program to detect the sound engine's location */

	for(uint32_t i=0; i+36 < inGBA_length && !detected; i+=4)
	{
		/* Shift history of read data */
		data_l1 = data;
		/* Read 32-bit value from input file (exit loop if end of file or read error) */
		data = file->GetWord(i);

		/* Look for a particular opcode sequence... */
		if(data == 0x4700bc01 || data == 0x4700)
		{
			if(data_l1 == 0xbc70d1f1 || data_l1 == 0xbc01bcf0 || data_l1 == 0xbc70ddf0)
			{
				dummy = file->GetWord(i+12);
				if(dummy == 0x4000100) ;//printf("Sound engine detected : Stereo version\n");
				else if(dummy == 0x4000200) ;//printf("Sound engine detected : Pokemon stereo version\n");
				else if(dummy == 0x40000e0) ;//printf("Sound engine detected : Mono version\n");
				else continue;

				/* At this point, we PROBABLY detected the sound engine but it might
				  still be a false positive... */
				dummy = file->GetWord(i+24);

				/* Get engine's (supposed ?) main parameters */
				unsigned int polyphony = (dummy & 0x000F00) >> 8;
				unsigned int main_vol = (dummy & 0x00F000) >> 12;
				unsigned int sampling_rate_index = (dummy & 0x0F0000) >> 16;
				unsigned int dac_bits = 17-((dummy & 0xF00000) >> 20);

				dummy = file->GetWord(i+28);			//Read # of song levels
				//printf("# of song levels : %d\n", dummy);
				uint32_t ram_tbl_adr, song_tbl_adr;
				ram_tbl_adr = file->GetWord(i+32);
				/* Compute (supposed ?) address of song table */
				song_tbl_adr = (ram_tbl_adr & 0x3FFFFFF) + 12 * dummy;

				/* Prevent illegal values for all fields */
				if(main_vol == 0 || polyphony > 12 || dac_bits < 6 || dac_bits > 9
				|| sampling_rate_index < 1 || sampling_rate_index > 12 || song_tbl_adr >= inGBA_length || dummy >= 256)
				{
					continue;
				}

				//At this point we can be almost certain we detected the real thing.
				//printf(
				//	"Engine parameters :\n"
				//	"Main Volume : %u Polyphony : %u channels, Dac : %u bits, Sampling rate : %s\n"
				//	"Song table located at : 0x%x\n"
				//, main_vol, polyphony, dac_bits, sr_lookup[sampling_rate_index], song_tbl_adr);

				detected = true;
				offset = i;
			}
		}
	}

	if (detected)
	{
		mp2k_offset = offset;
	}
	return detected;
}
