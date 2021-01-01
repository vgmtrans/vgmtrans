// MIDI Output Routines
// --------------------
// written by Valley Bell
// to be included as header file

// Note: MidiDelayCallback can be used to inject additional events.
//       IMPORTANT: You must not call any of the Write*Event() functions or
//       WriteMidiDelay() within this function.

#include <stdlib.h>
#include <string.h>

#include <stdtype.h>

typedef struct _midi_track_state
{
	UINT32 trkBase;
	UINT32 curDly;	// delay until next event
	UINT8 midChn;
} MID_TRK_STATE;

typedef struct file_information
{
	UINT32 alloc;	// allocated bytes
	UINT32 pos;		// current file offset
	UINT8* data;	// file data
} FILE_INF;


#ifndef INLINE
#define INLINE static
#endif

static void WriteMidiDelay(FILE_INF* fInf, UINT32* delay);
static void WriteEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 evt, UINT8 val1, UINT8 val2);
static void WriteLongEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 evt, UINT32 dataLen, const void* data);
static void WriteMetaEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 metaType, UINT32 dataLen, const void* data);
static void WriteMidiValue(FILE_INF* fInf, UINT32 value);
static void File_CheckRealloc(FILE_INF* FileInf, UINT32 bytesNeeded);
static void WriteMidiHeader(FILE_INF* fInf, UINT16 format, UINT16 tracks, UINT16 resolution);
static void WriteMidiTrackStart(FILE_INF* fInf, MID_TRK_STATE* MTS);
static void WriteMidiTrackEnd(FILE_INF* fInf, MID_TRK_STATE* MTS);

INLINE void WriteBE32(UINT8* buffer, UINT32 value);
INLINE void WriteBE16(UINT8* buffer, UINT16 value);


// optional callback for injecting raw data before writing delays
// Returning nonzero makes it skip writing the delay.
static UINT8 (*MidiDelayCallback)(FILE_INF* fInf, UINT32* delay) = NULL;

static void WriteMidiDelay(FILE_INF* fInf, UINT32* delay)
{
	if (MidiDelayCallback != NULL && MidiDelayCallback(fInf, delay))
		return;
	
	WriteMidiValue(fInf, *delay);
	*delay = 0;
	
	return;
}

static void WriteEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 evt, UINT8 val1, UINT8 val2)
{
	WriteMidiDelay(fInf, &MTS->curDly);
	
	File_CheckRealloc(fInf, 0x03);
	switch(evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		fInf->data[fInf->pos + 0x00] = evt | MTS->midChn;
		fInf->data[fInf->pos + 0x01] = val1;
		fInf->data[fInf->pos + 0x02] = val2;
		fInf->pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		fInf->data[fInf->pos + 0x00] = evt | MTS->midChn;
		fInf->data[fInf->pos + 0x01] = val1;
		fInf->pos += 0x02;
		break;
	case 0xF0:	// for Meta Event: Track End
		fInf->data[fInf->pos + 0x00] = evt;
		fInf->data[fInf->pos + 0x01] = val1;
		fInf->data[fInf->pos + 0x02] = val2;
		fInf->pos += 0x03;
		break;
	default:
		break;
	}
	
	return;
}

static void WriteLongEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 evt, UINT32 dataLen, const void* data)
{
	WriteMidiDelay(fInf, &MTS->curDly);
	
	File_CheckRealloc(fInf, 0x01 + 0x04 + dataLen);	// worst case: 4 bytes of data length
	fInf->data[fInf->pos + 0x00] = evt;
	fInf->pos += 0x01;
	WriteMidiValue(fInf, dataLen);
	memcpy(&fInf->data[fInf->pos], data, dataLen);
	fInf->pos += dataLen;
	
	return;
}

static void WriteMetaEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 metaType, UINT32 dataLen, const void* data)
{
	WriteMidiDelay(fInf, &MTS->curDly);
	
	File_CheckRealloc(fInf, 0x02 + 0x05 + dataLen);	// worst case: 5 bytes of data length
	fInf->data[fInf->pos + 0x00] = 0xFF;
	fInf->data[fInf->pos + 0x01] = metaType;
	fInf->pos += 0x02;
	WriteMidiValue(fInf, dataLen);
	memcpy(&fInf->data[fInf->pos], data, dataLen);
	fInf->pos += dataLen;
	
	return;
}

static void WriteMidiValue(FILE_INF* fInf, UINT32 value)
{
	UINT8 valSize;
	UINT8* valData;
	UINT32 tempLng;
	UINT32 curPos;
	
	valSize = 0x00;
	tempLng = value;
	do
	{
		tempLng >>= 7;
		valSize ++;
	} while(tempLng);
	
	File_CheckRealloc(fInf, valSize);
	valData = &fInf->data[fInf->pos];
	curPos = valSize;
	tempLng = value;
	do
	{
		curPos --;
		valData[curPos] = 0x80 | (tempLng & 0x7F);
		tempLng >>= 7;
	} while(tempLng);
	valData[valSize - 1] &= 0x7F;
	
	fInf->pos += valSize;
	
	return;
}

static void File_CheckRealloc(FILE_INF* fInf, UINT32 bytesNeeded)
{
#define REALLOC_STEP	0x8000	// 32 KB block
	UINT32 minPos;
	
	minPos = fInf->pos + bytesNeeded;
	if (minPos <= fInf->alloc)
		return;
	
	while(minPos > fInf->alloc)
		fInf->alloc += REALLOC_STEP;
	fInf->data = (UINT8*)realloc(fInf->data, fInf->alloc);
	
	return;
}

static void WriteMidiHeader(FILE_INF* fInf, UINT16 format, UINT16 tracks, UINT16 resolution)
{
	File_CheckRealloc(fInf, 0x08 + 0x06);
	
	WriteBE32(&fInf->data[fInf->pos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&fInf->data[fInf->pos + 0x04], 0x00000006);	// Header Length
	fInf->pos += 0x08;
	
	WriteBE16(&fInf->data[fInf->pos + 0x00], format);		// MIDI Format (0/1/2)
	WriteBE16(&fInf->data[fInf->pos + 0x02], tracks);		// number of tracks
	WriteBE16(&fInf->data[fInf->pos + 0x04], resolution);	// Ticks per Quarter
	fInf->pos += 0x06;
	
	return;
}

static void WriteMidiTrackStart(FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	File_CheckRealloc(fInf, 0x08);
	
	WriteBE32(&fInf->data[fInf->pos + 0x00], 0x4D54726B);	// write 'MTrk'
	WriteBE32(&fInf->data[fInf->pos + 0x04], 0x00000000);	// write dummy length
	fInf->pos += 0x08;
	
	MTS->trkBase = fInf->pos;
	MTS->curDly = 0;
	
	return;
}

static void WriteMidiTrackEnd(FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	UINT32 trkLen;
	
	trkLen = fInf->pos - MTS->trkBase;
	WriteBE32(&fInf->data[MTS->trkBase - 0x04], trkLen);	// write Track Length
	
	return;
}


INLINE void WriteBE32(UINT8* buffer, UINT32 value)
{
	buffer[0x00] = (value >> 24) & 0xFF;
	buffer[0x01] = (value >> 16) & 0xFF;
	buffer[0x02] = (value >>  8) & 0xFF;
	buffer[0x03] = (value >>  0) & 0xFF;
	
	return;
}

INLINE void WriteBE16(UINT8* buffer, UINT16 value)
{
	buffer[0x00] = (value >> 8) & 0xFF;
	buffer[0x01] = (value >> 0) & 0xFF;
	
	return;
}
