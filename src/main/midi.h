#pragma once

#include <stdint.h>

#include "buffer.h"

/* Get a MIDI note from a float frequency */
uint8_t midi_freq_get_note(float freq);

/* Get a pitch bend value from a float frequency */
int16_t midi_freq_get_bend(float freq);

const char *midi_note_name(int note);
int midi_note_octave(int note);

struct midi_track {
	struct buffer buffer;
	uint8_t last_cmd;
};
void midi_track_init(struct midi_track *t);

struct midi_file {
	int type, ticks_per_quarter_note;
	int num_tracks;
	struct midi_track *tracks;
};
struct midi_file *midi_file_new(int type, int ticks_per_quarter_note);
void midi_file_init(struct midi_file *f, int type, int ticks_per_quarter_note);
struct midi_track *midi_file_add_track(struct midi_file *f);
struct midi_track *midi_file_add_tracks(struct midi_file *f, int num_tracks);
int midi_file_save(struct midi_file *midi_file, char *filename);

/* Pushing events */
void midi_track_write_delta_time(struct midi_track *track, uint32_t t);
void midi_track_write_cmd(struct midi_track *track, uint8_t cmd);
void midi_track_write_cmd_no_reuse(struct midi_track *track, uint8_t cmd);
void midi_track_write_note_on(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint8_t note, uint8_t velocity);
void midi_track_write_note_off(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint8_t note, uint8_t velocity);
void midi_track_write_meta_event_buf(struct midi_track *track, uint32_t deltaTime, uint8_t ev, uint8_t len, uint8_t *buf);
void midi_track_write_meta_event(struct midi_track *track, uint32_t deltaTime, uint8_t ev, uint8_t len, ...);
void midi_track_write_tempo(struct midi_track *track, uint32_t deltaTime, uint32_t tempo);
void midi_track_write_time_signature(struct midi_track *track, uint32_t deltaTime, uint8_t numerator, uint8_t denominator, uint8_t ticksPerClick, uint8_t quarterNote32ndNotes);
void midi_track_write_track_end(struct midi_track *track, uint32_t deltaTime);
void midi_track_write_program_change(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint8_t program);
void midi_track_write_text_event(struct midi_track *track, uint8_t event, uint32_t deltaTime, const char *text, int len);
void midi_track_write_text(struct midi_track *track, uint32_t deltaTime, const char *text, int len);
void midi_track_write_track_name(struct midi_track *track, uint32_t deltaTime, const char *text, int len);
void midi_track_write_pitch_bend(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t bend);
void midi_track_write_control_change(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint8_t controller, uint8_t value);
void midi_track_write_nrpn_msb(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t number, uint8_t value_msb);
void midi_track_write_nrpn_msb_lsb(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t number, uint8_t value_msb, uint8_t value_lsb);
void midi_track_write_rpn_msb(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t number, uint8_t value_msb);
void midi_track_write_rpn_msb_lsb(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t number, uint8_t value_msb, uint8_t value_lsb);
void midi_track_write_bank_select(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t bank);
