/* FluidSynth - A Software Synthesizer
 *
 * Copyright (C) 2003  Peter Hanappe and others.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include "vgmtrans_fluid_midi.h"

/* Read the entire contents of a file into memory, allocating enough memory
 * for the file, and returning the length and the buffer.
 * Note: This rewinds the file to the start before reading.
 * Returns NULL if there was an error reading or allocating memory.
 */
static char*vgmtrans_fluid_file_read_full(fluid_file fp, size_t *length);


/***************************************************************
*
*                      MIDIFILE
*/

/**
* Return a new MIDI file handle for parsing an already-loaded MIDI file.
* @internal
* @param buffer Pointer to full contents of MIDI file (borrows the pointer).
*  The caller must not free buffer until after the fluid_midi_file is deleted.
* @param length Size of the buffer in bytes.
* @return New MIDI file handle or NULL on error.
*/
fluid_midi_file *
vgmtrans_new_fluid_midi_file(const char *buffer, size_t length)
{
    fluid_midi_file *mf;

    mf = FLUID_NEW(fluid_midi_file);
    if (mf == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }
    FLUID_MEMSET(mf, 0, sizeof(fluid_midi_file));

    mf->c = -1;
    mf->running_status = -1;

    mf->buffer = buffer;
    mf->buf_len = length;
    mf->buf_pos = 0;
    mf->eof = FALSE;

    if (vgmtrans_fluid_midi_file_read_mthd(mf) != FLUID_OK) {
        FLUID_FREE(mf);
        return NULL;
    }
    return mf;
}

static char*
vgmtrans_fluid_file_read_full(fluid_file fp, size_t *length)
{
    size_t buflen;
    char* buffer;
    size_t n;
    /* Work out the length of the file in advance */
    if (FLUID_FSEEK(fp, 0, SEEK_END) != 0)
    {
        FLUID_LOG(FLUID_ERR, "File load: Could not seek within file");
        return NULL;
    }
    buflen = ftell(fp);
    if (FLUID_FSEEK(fp, 0, SEEK_SET) != 0)
    {
        FLUID_LOG(FLUID_ERR, "File load: Could not seek within file");
        return NULL;
    }
    FLUID_LOG(FLUID_DBG, "File load: Allocating %d bytes", buflen);
    buffer = FLUID_MALLOC(buflen);
    if (buffer == NULL) {
        FLUID_LOG(FLUID_PANIC, "Out of memory");
        return NULL;
    }
    n = FLUID_FREAD(buffer, 1, buflen, fp);
    if (n != buflen) {
        FLUID_LOG(FLUID_ERR, "Only read %d bytes; expected %d", n,
                buflen);
        FLUID_FREE(buffer);
        return NULL;
    };
    *length = n;
    return buffer;
}

/**
* Delete a MIDI file handle.
* @internal
* @param mf MIDI file handle to close and free.
*/
void
vgmtrans_delete_fluid_midi_file(fluid_midi_file *mf)
{
    if (mf == NULL) {
        return;
    }
    FLUID_FREE(mf);
    return;
}

/*
 * Gets the next byte in a MIDI file, taking into account previous running status.
 *
 * returns FLUID_FAILED if EOF or read error
 */
int
vgmtrans_fluid_midi_file_getc(fluid_midi_file *mf)
{
    unsigned char c;
    if (mf->c >= 0) {
        c = mf->c;
        mf->c = -1;
    } else {
        if (mf->buf_pos >= mf->buf_len) {
            mf->eof = TRUE;
            return FLUID_FAILED;
        }
        c = mf->buffer[mf->buf_pos++];
        mf->trackpos++;
    }
    return (int) c;
}

/*
 * Saves a byte to be returned the next time fluid_midi_file_getc() is called,
 * when it is necessary according to running status.
 */
int
vgmtrans_fluid_midi_file_push(fluid_midi_file *mf, int c)
{
    mf->c = c;
    return FLUID_OK;
}

/*
 * vgmtrans_fluid_midi_file_read
 */
int
vgmtrans_fluid_midi_file_read(fluid_midi_file *mf, void *buf, int len)
{
    int num = len < mf->buf_len - mf->buf_pos
            ? len : mf->buf_len - mf->buf_pos;
    if (num != len) {
        mf->eof = TRUE;
    }
    if (num < 0) {
        num = 0;
    }
    /* Note: Read bytes, even if there aren't enough, but only increment
     * trackpos if successful (emulates old behaviour of vgmtrans_fluid_midi_file_read)
     */
    FLUID_MEMCPY(buf, mf->buffer+mf->buf_pos, num);
    mf->buf_pos += num;
    if (num == len)
        mf->trackpos += num;
#if DEBUG
    else
        FLUID_LOG(FLUID_DBG, "Could not read the requested number of bytes");
#endif
    return (num != len) ? FLUID_FAILED : FLUID_OK;
}

/*
 * vgmtrans_fluid_midi_file_skip
 */
int
vgmtrans_fluid_midi_file_skip(fluid_midi_file *mf, int skip)
{
    int new_pos = mf->buf_pos + skip;
    /* Mimic the behaviour of fseek: Error to seek past the start of file, but
     * OK to seek past end (this just puts it into the EOF state). */
    if (new_pos < 0) {
        FLUID_LOG(FLUID_ERR, "Failed to seek position in file");
        return FLUID_FAILED;
    }
    /* Clear the EOF flag, even if moved past the end of the file (this is
     * consistent with the behaviour of fseek). */
    mf->eof = FALSE;
    mf->buf_pos = new_pos;
    return FLUID_OK;
}

/*
 * vgmtrans_fluid_midi_file_eof
 */
int vgmtrans_fluid_midi_file_eof(fluid_midi_file *mf)
{
    /* Note: This does not simply test whether the file read pointer is past
     * the end of the file. It mimics the behaviour of feof by actually
     * testing the stateful EOF condition, which is set to TRUE if getc or
     * fread have attempted to read past the end (but not if they have
     * precisely reached the end), but reset to FALSE upon a successful seek.
     */
    return mf->eof;
}

/*
 * vgmtrans_fluid_midi_file_read_mthd
 */
int
vgmtrans_fluid_midi_file_read_mthd(fluid_midi_file *mf)
{
    char mthd[15];
    if (vgmtrans_fluid_midi_file_read(mf, mthd, 14) != FLUID_OK) {
        return FLUID_FAILED;
    }
    if ((FLUID_STRNCMP(mthd, "MThd", 4) != 0) || (mthd[7] != 6)
            || (mthd[9] > 2)) {
        FLUID_LOG(FLUID_ERR,
                "Doesn't look like a MIDI file: invalid MThd header");
        return FLUID_FAILED;
    }
    mf->type = mthd[9];
    mf->ntracks = (unsigned) mthd[11];
    mf->ntracks += (unsigned int) (mthd[10]) << 16;
    if ((mthd[12]) < 0) {
        mf->uses_smpte = 1;
        mf->smpte_fps = -mthd[12];
        mf->smpte_res = (unsigned) mthd[13];
        FLUID_LOG(FLUID_ERR, "File uses SMPTE timing -- Not implemented yet");
        return FLUID_FAILED;
    } else {
        mf->uses_smpte = 0;
        mf->division = (mthd[12] << 8) | (mthd[13] & 0xff);
        FLUID_LOG(FLUID_DBG, "Division=%d", mf->division);
    }
    return FLUID_OK;
}

/*
 * vgmtrans_fluid_midi_file_load_tracks
 */
int
vgmtrans_fluid_midi_file_load_tracks(fluid_midi_file *mf, fluid_player_t *player)
{
    int i;
    for (i = 0; i < mf->ntracks; i++) {
        if (vgmtrans_fluid_midi_file_read_track(mf, player, i) != FLUID_OK) {
            return FLUID_FAILED;
        }
    }
    return FLUID_OK;
}


/*
 * vgmtrans_fluid_midi_file_read_tracklen
 */
int
vgmtrans_fluid_midi_file_read_tracklen(fluid_midi_file *mf)
{
    unsigned char length[5];
    if (vgmtrans_fluid_midi_file_read(mf, length, 4) != FLUID_OK) {
        return FLUID_FAILED;
    }
    mf->tracklen = fluid_getlength(length);
    mf->trackpos = 0;
    mf->eot = 0;
    return FLUID_OK;
}

/*
 * vgmtrans_fluid_midi_file_eot
 */
int
vgmtrans_fluid_midi_file_eot(fluid_midi_file *mf)
{
#if DEBUG
    if (mf->trackpos > mf->tracklen) {
        printf("track overrun: %d > %d\n", mf->trackpos, mf->tracklen);
    }
#endif
    return mf->eot || (mf->trackpos >= mf->tracklen);
}

/*
 * vgmtrans_fluid_midi_file_read_track
 */
int
vgmtrans_fluid_midi_file_read_track(fluid_midi_file *mf, fluid_player_t *player, int num)
{
    fluid_track_t *track;
    unsigned char id[5], length[5];
    int found_track = 0;
    int skip;

    if (vgmtrans_fluid_midi_file_read(mf, id, 4) != FLUID_OK) {
        return FLUID_FAILED;
    }
    id[4] = '\0';
    mf->dtime = 0;

    while (!found_track) {

        if (fluid_isasciistring((char *) id) == 0) {
            FLUID_LOG(FLUID_ERR,
                    "An non-ascii track header found, corrupt file");
            return FLUID_FAILED;

        } else if (strcmp((char *) id, "MTrk") == 0) {

            found_track = 1;

            if (vgmtrans_fluid_midi_file_read_tracklen(mf) != FLUID_OK) {
                return FLUID_FAILED;
            }

            track = new_fluid_track(num);
            if (track == NULL) {
                FLUID_LOG(FLUID_ERR, "Out of memory");
                return FLUID_FAILED;
            }

            while (!vgmtrans_fluid_midi_file_eot(mf)) {
                if (vgmtrans_fluid_midi_file_read_event(mf, track) != FLUID_OK) {
                    delete_fluid_track(track);
                    return FLUID_FAILED;
                }
            }

            /* Skip remaining track data, if any */
            if (mf->trackpos < mf->tracklen)
                vgmtrans_fluid_midi_file_skip(mf, mf->tracklen - mf->trackpos);

            fluid_player_add_track(player, track);

        } else {
            found_track = 0;
            if (vgmtrans_fluid_midi_file_read(mf, length, 4) != FLUID_OK) {
                return FLUID_FAILED;
            }
            skip = fluid_getlength(length);
            /* fseek(mf->fp, skip, SEEK_CUR); */
            if (vgmtrans_fluid_midi_file_skip(mf, skip) != FLUID_OK) {
                return FLUID_FAILED;
            }
        }
    }
    if (vgmtrans_fluid_midi_file_eof(mf)) {
        FLUID_LOG(FLUID_ERR, "Unexpected end of file");
        return FLUID_FAILED;
    }
    return FLUID_OK;
}

/*
 * vgmtrans_fluid_midi_file_read_varlen
 */
int
vgmtrans_fluid_midi_file_read_varlen(fluid_midi_file *mf)
{
    int i;
    int c;
    mf->varlen = 0;
    for (i = 0;; i++) {
        if (i == 4) {
            FLUID_LOG(FLUID_ERR, "Invalid variable length number");
            return FLUID_FAILED;
        }
        c = vgmtrans_fluid_midi_file_getc(mf);
        if (c < 0) {
            FLUID_LOG(FLUID_ERR, "Unexpected end of file");
            return FLUID_FAILED;
        }
        if (c & 0x80) {
            mf->varlen |= (int) (c & 0x7F);
            mf->varlen <<= 7;
        } else {
            mf->varlen += c;
            break;
        }
    }
    return FLUID_OK;
}

/*
 * vgmtrans_fluid_midi_file_read_event
 */
int
vgmtrans_fluid_midi_file_read_event(fluid_midi_file *mf, fluid_track_t *track)
{
    int status;
    int type;
    int tempo;
    unsigned char *metadata = NULL;
    unsigned char *dyn_buf = NULL;
    unsigned char static_buf[256];
    int nominator, denominator, clocks, notes;
    vgmtrans_fluid_midi_event_t *evt;
    int channel = 0;
    int param1 = 0;
    int param2 = 0;
    int size;

    /* read the delta-time of the event */
    if (vgmtrans_fluid_midi_file_read_varlen(mf) != FLUID_OK) {
        return FLUID_FAILED;
    }
    mf->dtime += mf->varlen;

    /* read the status byte */
    status = vgmtrans_fluid_midi_file_getc(mf);
    if (status < 0) {
        FLUID_LOG(FLUID_ERR, "Unexpected end of file");
        return FLUID_FAILED;
    }

    /* not a valid status byte: use the running status instead */
    if ((status & 0x80) == 0) {
        if ((mf->running_status & 0x80) == 0) {
            FLUID_LOG(FLUID_ERR, "Undefined status and invalid running status");
            return FLUID_FAILED;
        }
        vgmtrans_fluid_midi_file_push(mf, status);
        status = mf->running_status;
    }

    /* check what message we have */

    mf->running_status = status;

    if ((status == MIDI_SYSEX)) { /* system exclusif */
        /* read the length of the message */
        if (vgmtrans_fluid_midi_file_read_varlen(mf) != FLUID_OK) {
            return FLUID_FAILED;
        }

        if (mf->varlen) {
            FLUID_LOG(FLUID_DBG, "%s: %d: alloc metadata, len = %d", __FILE__,
                    __LINE__, mf->varlen);
            metadata = FLUID_MALLOC(mf->varlen + 1);

            if (metadata == NULL) {
                FLUID_LOG(FLUID_PANIC, "Out of memory");
                return FLUID_FAILED;
            }

            /* read the data of the message */
            if (vgmtrans_fluid_midi_file_read(mf, metadata, mf->varlen) != FLUID_OK) {
                FLUID_FREE (metadata);
                return FLUID_FAILED;
            }

            evt = vgmtrans_new_fluid_midi_event();
            if (evt == NULL) {
                FLUID_LOG(FLUID_ERR, "Out of memory");
                FLUID_FREE (metadata);
                return FLUID_FAILED;
            }

            evt->track = track;
            evt->dtime = mf->dtime;
            size = mf->varlen;

            if (metadata[mf->varlen - 1] == MIDI_EOX)
                size--;

            /* Add SYSEX event and indicate that its dynamically allocated and should be freed with event */
            fluid_midi_event_set_sysex(evt, metadata, size, TRUE);
            fluid_track_add_event(track, evt);
            mf->dtime = 0;
        }
        return FLUID_OK;

    } else if (status == MIDI_META_EVENT) { /* meta events */

        int result = FLUID_OK;

        /* get the type of the meta message */
        type = vgmtrans_fluid_midi_file_getc(mf);
        if (type < 0) {
            FLUID_LOG(FLUID_ERR, "Unexpected end of file");
            return FLUID_FAILED;
        }

        /* get the length of the data part */
        if (vgmtrans_fluid_midi_file_read_varlen(mf) != FLUID_OK) {
            return FLUID_FAILED;
        }

        if (mf->varlen < 255) {
            metadata = &static_buf[0];
        } else {
            FLUID_LOG(FLUID_DBG, "%s: %d: alloc metadata, len = %d", __FILE__,
                    __LINE__, mf->varlen);
            dyn_buf = FLUID_MALLOC(mf->varlen + 1);
            if (dyn_buf == NULL) {
                FLUID_LOG(FLUID_PANIC, "Out of memory");
                return FLUID_FAILED;
            }
            metadata = dyn_buf;
        }

        /* read the data */
        if (mf->varlen) {
            if (vgmtrans_fluid_midi_file_read(mf, metadata, mf->varlen) != FLUID_OK) {
                if (dyn_buf) {
                    FLUID_FREE(dyn_buf);
                }
                return FLUID_FAILED;
            }
        }

        /* handle meta data */
        switch (type) {

            case MIDI_COPYRIGHT:
                metadata[mf->varlen] = 0;
                break;

            case MIDI_TRACK_NAME:
                metadata[mf->varlen] = 0;
                fluid_track_set_name(track, (char *) metadata);
                break;

            case MIDI_INST_NAME:
                metadata[mf->varlen] = 0;
                break;

            case MIDI_LYRIC:
                break;

            case MIDI_MARKER:
                break;

            case MIDI_CUE_POINT:
                break; /* don't care much for text events */

            case MIDI_EOT:
                if (mf->varlen != 0) {
                    FLUID_LOG(FLUID_ERR, "Invalid length for EndOfTrack event");
                    result = FLUID_FAILED;
                    break;
                }
                mf->eot = 1;
                evt = vgmtrans_new_fluid_midi_event();
                if (evt == NULL) {
                    FLUID_LOG(FLUID_ERR, "Out of memory");
                    result = FLUID_FAILED;
                    break;
                }
                evt->track = track;
                evt->dtime = mf->dtime;
                evt->type = MIDI_EOT;
                fluid_track_add_event(track, evt);
                mf->dtime = 0;
                break;

            case MIDI_SET_TEMPO:
                if (mf->varlen != 3) {
                    FLUID_LOG(FLUID_ERR,
                            "Invalid length for SetTempo meta event");
                    result = FLUID_FAILED;
                    break;
                }
                tempo = (metadata[0] << 16) + (metadata[1] << 8) + metadata[2];
                evt = vgmtrans_new_fluid_midi_event();
                if (evt == NULL) {
                    FLUID_LOG(FLUID_ERR, "Out of memory");
                    result = FLUID_FAILED;
                    break;
                }
                evt->track = track;
                evt->dtime = mf->dtime;
                evt->type = MIDI_SET_TEMPO;
                evt->channel = 0;
                evt->param1 = tempo;
                evt->param2 = 0;
                fluid_track_add_event(track, evt);
                mf->dtime = 0;
                break;

            case MIDI_SMPTE_OFFSET:
                if (mf->varlen != 5) {
                    FLUID_LOG(FLUID_ERR,
                            "Invalid length for SMPTE Offset meta event");
                    result = FLUID_FAILED;
                    break;
                }
                break; /* we don't use smtp */

            case MIDI_TIME_SIGNATURE:
                if (mf->varlen != 4) {
                    FLUID_LOG(FLUID_ERR,
                            "Invalid length for TimeSignature meta event");
                    result = FLUID_FAILED;
                    break;
                }
                nominator = metadata[0];
                denominator = pow(2.0, (double) metadata[1]);
                clocks = metadata[2];
                notes = metadata[3];

                FLUID_LOG(FLUID_DBG,
                        "signature=%d/%d, metronome=%d, 32nd-notes=%d",
                        nominator, denominator, clocks, notes);

                break;

            case MIDI_KEY_SIGNATURE:
                if (mf->varlen != 2) {
                    FLUID_LOG(FLUID_ERR,
                            "Invalid length for KeySignature meta event");
                    result = FLUID_FAILED;
                    break;
                }
                /* We don't care about key signatures anyway */
                /* sf = metadata[0];
                mi = metadata[1]; */
                break;

            case MIDI_SEQUENCER_EVENT:
                break;

            default:
                break;
        }

        if (dyn_buf) {
            FLUID_LOG(FLUID_DBG, "%s: %d: free metadata", __FILE__, __LINE__);
            FLUID_FREE(dyn_buf);
        }

        return result;

    } else { /* channel messages */

        type = status & 0xf0;
        channel = status & 0x0f;

        /* all channel message have at least 1 byte of associated data */
        if ((param1 = vgmtrans_fluid_midi_file_getc(mf)) < 0) {
            FLUID_LOG(FLUID_ERR, "Unexpected end of file");
            return FLUID_FAILED;
        }

        switch (type) {

            case NOTE_ON:
                if ((param2 = vgmtrans_fluid_midi_file_getc(mf)) < 0) {
                    FLUID_LOG(FLUID_ERR, "Unexpected end of file");
                    return FLUID_FAILED;
                }
                break;

            case NOTE_OFF:
                if ((param2 = vgmtrans_fluid_midi_file_getc(mf)) < 0) {
                    FLUID_LOG(FLUID_ERR, "Unexpected end of file");
                    return FLUID_FAILED;
                }
                break;

            case KEY_PRESSURE:
                if ((param2 = vgmtrans_fluid_midi_file_getc(mf)) < 0) {
                    FLUID_LOG(FLUID_ERR, "Unexpected end of file");
                    return FLUID_FAILED;
                }
                break;

            case CONTROL_CHANGE:
                if ((param2 = vgmtrans_fluid_midi_file_getc(mf)) < 0) {
                    FLUID_LOG(FLUID_ERR, "Unexpected end of file");
                    return FLUID_FAILED;
                }
                break;

            case PROGRAM_CHANGE:
                break;

            case CHANNEL_PRESSURE:
                break;

            case PITCH_BEND:
                if ((param2 = vgmtrans_fluid_midi_file_getc(mf)) < 0) {
                    FLUID_LOG(FLUID_ERR, "Unexpected end of file");
                    return FLUID_FAILED;
                }

                param1 = ((param2 & 0x7f) << 7) | (param1 & 0x7f);
                param2 = 0;
                break;

            default:
                /* Can't possibly happen !? */
                FLUID_LOG(FLUID_ERR, "Unrecognized MIDI event");
                return FLUID_FAILED;
        }
        evt = vgmtrans_new_fluid_midi_event();
        if (evt == NULL) {
            FLUID_LOG(FLUID_ERR, "Out of memory");
            return FLUID_FAILED;
        }
        evt->track = track;
        evt->dtime = mf->dtime;
        evt->type = type;
        evt->channel = channel;
        evt->param1 = param1;
        evt->param2 = param2;
        fluid_track_add_event(track, evt);
        mf->dtime = 0;
    }
    return FLUID_OK;
}

/*
 * vgmtrans_fluid_midi_file_get_division
 */
int
vgmtrans_fluid_midi_file_get_division(fluid_midi_file *midifile)
{
    return midifile->division;
}

/******************************************************
*
*     fluid_track_t
*/

/**
* Create a MIDI event structure.
* @return New MIDI event structure or NULL when out of memory.
*/
fluid_midi_event_t *
vgmtrans_new_fluid_midi_event ()
{
    vgmtrans_fluid_midi_event_t* evt;
    evt = FLUID_NEW(vgmtrans_fluid_midi_event_t);
    if (evt == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }
    evt->dtime = 0;
    evt->type = 0;
    evt->channel = 0;
    evt->param1 = 0;
    evt->param2 = 0;
    evt->next = NULL;
    evt->paramptr = NULL;
    evt->track = NULL;
    return evt;
}

/**
* Get the parent track of a MIDI event structure.
* @param evt MIDI event structure
* @return Pointer to the parent fluid_track_t structure
*/
fluid_track_t *
vgmtrans_fluid_midi_event_get_track(vgmtrans_fluid_midi_event_t *evt)
{
    return evt->track;
}

/******************************************************
*
*     fluid_track_t
*/

/*
 * new_fluid_track
 */
fluid_track_t *
vgmtrans_new_fluid_track(int num)
{
    fluid_track_t *track;
    track = FLUID_NEW(fluid_track_t);
    if (track == NULL) {
        return NULL;
    }
    track->name = NULL;
    track->first = NULL;
    track->cur = NULL;
    track->last = NULL;
    track->ticks = 0;
    track->num = num;
    return track;
}


/******************************************************
*
*     fluid_player
*/

/**
* Create a new MIDI player.
* @param synth Fluid synthesizer instance to create player for
* @return New MIDI player instance or NULL on error (out of memory)
*/
fluid_player_t *
new_vgmtrans_fluid_player(fluid_synth_t *synth)
{
    int i;
    fluid_player_t *player;
    player = FLUID_NEW(fluid_player_t);
    if (player == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }
    player->status = FLUID_PLAYER_READY;
    player->loop = 1;
    player->ntracks = 0;
    for (i = 0; i < MAX_NUMBER_OF_TRACKS; i++) {
        player->track[i] = NULL;
    }
    player->synth = synth;
    player->system_timer = NULL;
    player->sample_timer = NULL;
    player->playlist = NULL;
    player->currentfile = NULL;
    player->division = 0;
    player->send_program_change = 1;
    player->miditempo = 480000;
    player->deltatime = 4.0;
    player->cur_msec = 0;
    player->cur_ticks = 0;
    fluid_player_set_playback_callback(player, fluid_synth_handle_midi_event, synth);

    player->use_system_timer = FALSE;
    player->reset_synth_between_songs = TRUE;

    return player;
}

/*
 * vgmtrans_fluid_player_load
 */
int
vgmtrans_fluid_player_load(fluid_player_t *player, fluid_playlist_item *item)
{
    fluid_midi_file *midifile;
    char* buffer;
    size_t buffer_length;
    int buffer_owned;

    if (item->filename != NULL)
    {
        fluid_file fp;
        /* This file is specified by filename; load the file from disk */
        FLUID_LOG(FLUID_DBG, "%s: %d: Loading midifile %s", __FILE__, __LINE__,
                item->filename);
        /* Read the entire contents of the file into the buffer */
        fp = FLUID_FOPEN(item->filename, "rb");
        if (fp == NULL) {
            FLUID_LOG(FLUID_ERR, "Couldn't open the MIDI file");
            return FLUID_FAILED;
        }
        buffer = vgmtrans_fluid_file_read_full(fp, &buffer_length);
        if (buffer == NULL)
        {
            FLUID_FCLOSE(fp);
            return FLUID_FAILED;
        }
        buffer_owned = 1;
        FLUID_FCLOSE(fp);
    }
    else
    {
        /* This file is specified by a pre-loaded buffer; load from memory */
        FLUID_LOG(FLUID_DBG, "%s: %d: Loading midifile from memory (%p)",
                __FILE__, __LINE__, item->buffer);
        buffer = (char *) item->buffer;
        buffer_length = item->buffer_len;
        /* Do not free the buffer (it is owned by the playlist) */
        buffer_owned = 0;
    }

    midifile = vgmtrans_new_fluid_midi_file(buffer, buffer_length);
    if (midifile == NULL) {
        if (buffer_owned) {
            FLUID_FREE(buffer);
        }
        return FLUID_FAILED;
    }
    player->division = vgmtrans_fluid_midi_file_get_division(midifile);
    fluid_player_set_midi_tempo(player, player->miditempo); // Update deltatime
    /*FLUID_LOG(FLUID_DBG, "quarter note division=%d\n", player->division); */

    if (vgmtrans_fluid_midi_file_load_tracks(midifile, player) != FLUID_OK) {
        if (buffer_owned) {
            FLUID_FREE(buffer);
        }
        vgmtrans_delete_fluid_midi_file(midifile);
        return FLUID_FAILED;
    }
    vgmtrans_delete_fluid_midi_file(midifile);
    if (buffer_owned) {
        FLUID_FREE(buffer);
    }
    return FLUID_OK;
}

void
vgmtrans_fluid_player_playlist_load(fluid_player_t *player, unsigned int msec)
{
    fluid_playlist_item* current_playitem;
    int i;

    do {
        fluid_player_advancefile(player);
        if (player->currentfile == NULL) {
            /* Failed to find next song, probably since we're finished */
            player->status = FLUID_PLAYER_DONE;
            return;
        }

        fluid_player_reset(player);
        current_playitem = (fluid_playlist_item *) player->currentfile->data;
    } while (vgmtrans_fluid_player_load(player, current_playitem) != FLUID_OK);

    /* Successfully loaded midi file */

    player->begin_msec = msec;
    player->start_msec = msec;
    player->start_ticks = 0;
    player->cur_ticks = 0;

    if (player->reset_synth_between_songs) {
        fluid_synth_system_reset(player->synth);
    }

    for (i = 0; i < player->ntracks; i++) {
        if (player->track[i] != NULL) {
            fluid_track_reset(player->track[i]);
        }
    }
}


/*
 * fluid_player_callback
 */
int
vgmtrans_fluid_player_callback(void *data, unsigned int msec)
{
    int i;
    int loadnextfile;
    int status = FLUID_PLAYER_DONE;
    fluid_player_t *player;
    fluid_synth_t *synth;
    player = (fluid_player_t *) data;
    synth = player->synth;

    loadnextfile = player->currentfile == NULL ? 1 : 0;
    do {
        if (loadnextfile) {
            loadnextfile = 0;
            vgmtrans_fluid_player_playlist_load(player, msec);
            if (player->currentfile == NULL) {
                return 0;
            }
        }

        player->cur_msec = msec;
        player->cur_ticks = (player->start_ticks
                + (int) ((double) (player->cur_msec - player->start_msec)
                / player->deltatime));

        for (i = 0; i < player->ntracks; i++) {
            if (!fluid_track_eot(player->track[i])) {
                status = FLUID_PLAYER_PLAYING;
                if (fluid_track_send_events(player->track[i], synth, player,
                        player->cur_ticks) != FLUID_OK) {
                    /* */
                }
            }
        }

        if (status == FLUID_PLAYER_DONE) {
            FLUID_LOG(FLUID_DBG, "%s: %d: Duration=%.3f sec", __FILE__,
                    __LINE__, (msec - player->begin_msec) / 1000.0);
            loadnextfile = 1;
        }
    } while (loadnextfile);

    player->status = status;

    return 1;
}

/**
* Activates play mode for a MIDI player if not already playing.
* @param player MIDI player instance
* @return #FLUID_OK on success, #FLUID_FAILED otherwise
*/
int
vgmtrans_fluid_player_play(fluid_player_t *player)
{
    if (player->status == FLUID_PLAYER_PLAYING) {
        return FLUID_OK;
    }

    if (player->playlist == NULL) {
        return FLUID_OK;
    }

    player->status = FLUID_PLAYER_PLAYING;

    if (player->use_system_timer) {
        player->system_timer = new_fluid_timer((int) player->deltatime,
                vgmtrans_fluid_player_callback, (void *) player, TRUE, FALSE, TRUE);
        if (player->system_timer == NULL) {
            return FLUID_FAILED;
        }
    } else {
        player->sample_timer = new_fluid_sample_timer(player->synth,
                vgmtrans_fluid_player_callback, (void *) player);

        if (player->sample_timer == NULL) {
            return FLUID_FAILED;
        }
    }
    return FLUID_OK;
}
