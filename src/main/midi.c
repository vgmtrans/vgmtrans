#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#include "midi.h"

uint8_t midi_freq_get_note(float freq) {
	uint8_t n = 69 + round(12 * log2(freq/440.0));
	return n;
}

int16_t midi_freq_get_bend(float freq) {
	return 0;
}

static const char *midi_note_names[12] = {
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

const char *midi_note_name(int note) {
	return midi_note_names[note % 12];
}

int midi_note_octave(int note) {
	return note / 12 - 1;
}


void midi_track_init(struct midi_track *t) {
	memset(t, 0, sizeof(*t));

	buffer_init(&t->buffer, 0);
}

struct midi_file *midi_file_new(int type, int ticks_per_quarter_note) {
	struct midi_file *f = malloc(sizeof(struct midi_file));
	if(!f) return 0;
	midi_file_init(f, type, ticks_per_quarter_note);
	return f;
}

void midi_file_init(struct midi_file *f, int type, int ticks_per_quarter_note) {
	f->type = type;
	f->ticks_per_quarter_note = ticks_per_quarter_note;
	f->num_tracks = 0;
	f->tracks = 0;
}

int midi_file_save(struct midi_file *midi_file, char *filename) {
	FILE *f = fopen(filename, "wb");
	if(!f) return -1;

	uint8_t buf[] = {
		'M', 'T', 'h', 'd',
		0x00, 0x00, 0x00, 0x06, /* Length = 6 */
		0x00, 0x01, /* Type = 1 */
		midi_file->num_tracks >> 8, midi_file->num_tracks, /* Number of tracks */
		midi_file->ticks_per_quarter_note >> 8, midi_file->ticks_per_quarter_note
	};
	fwrite(buf, 1, sizeof(buf), f);

	for(int i = 0; i < midi_file->num_tracks; i++) {
		struct buffer *b = &midi_file->tracks[i].buffer;
		uint8_t buf[] = {
			'M', 'T', 'r', 'k',
			b->size >> 24, b->size >> 16, b->size >> 8, b->size
		};
		fwrite(buf, 1, sizeof(buf), f);
		fwrite(midi_file->tracks[i].buffer.data, 1, midi_file->tracks[i].buffer.size, f);
	}

	fclose(f);

	return 0;
}

struct midi_track *midi_file_add_track(struct midi_file *f) {
	return midi_file_add_tracks(f, 1);
}

struct midi_track *midi_file_add_tracks(struct midi_file *f, int num_tracks) {
	f->tracks = realloc(f->tracks, num_tracks * sizeof(struct midi_track));
	if(!f->tracks) return 0;

	for(int i = f->num_tracks; i < num_tracks; i++) {
		midi_track_init(&f->tracks[i]);
	}

	struct midi_track *r = &f->tracks[f->num_tracks];
	f->num_tracks = num_tracks;
	return r;
}

void midi_track_write_delta_time(struct midi_track *track, uint32_t t) {
	if(t > 0x1fffff) buffer_append_byte(&track->buffer, 0x80 | ((t >> 21) & 0x7f));
	if(t > 0x3fff) buffer_append_byte(&track->buffer, 0x80 | ((t >> 14) & 0x7f));
	if(t > 0x7f) buffer_append_byte(&track->buffer, 0x80 | ((t >> 7) & 0x7f));
	buffer_append_byte(&track->buffer, t & 0x7f);
}

void midi_track_write_cmd(struct midi_track *track, uint8_t cmd) {
	if(track->last_cmd != cmd) {
		track->last_cmd = cmd;
		buffer_append_byte(&track->buffer, cmd);
	}
}

void midi_track_write_cmd_no_reuse(struct midi_track *track, uint8_t cmd) {
	track->last_cmd = cmd;
	buffer_append_byte(&track->buffer, cmd);
}

void midi_track_write_note_on(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint8_t note, uint8_t velocity) {
	midi_track_write_delta_time(track, deltaTime);
	midi_track_write_cmd(track, 0x90 | (channel & 0x0f));
	buffer_append_byte(&track->buffer, note & 0x7f);
	buffer_append_byte(&track->buffer, velocity & 0x7f);
}

void midi_track_write_note_off(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint8_t note, uint8_t velocity) {
	midi_track_write_delta_time(track, deltaTime);
	midi_track_write_cmd(track, 0x80 | (channel & 0x0f));
	buffer_append_byte(&track->buffer, note & 0x7f);
	buffer_append_byte(&track->buffer, velocity & 0x7f);
}

void midi_track_write_meta_event_buf(struct midi_track *track, uint32_t deltaTime, uint8_t ev, uint8_t len, uint8_t *buf) {
	midi_track_write_delta_time(track, deltaTime);
	midi_track_write_cmd_no_reuse(track, 0xff);
	buffer_append_byte(&track->buffer, ev);
	buffer_append_byte(&track->buffer, len);
	buffer_append(&track->buffer, buf, len);
}

void midi_track_write_meta_event(struct midi_track *track, uint32_t deltaTime, uint8_t ev, uint8_t len, ...) {
	midi_track_write_delta_time(track, deltaTime);
	midi_track_write_cmd_no_reuse(track, 0xff);
	buffer_append_byte(&track->buffer, ev);
	buffer_append_byte(&track->buffer, len);
	if(len > 0) {
		va_list ap;
		va_start(ap, len);
		for(int i = 0; i < len; i++) buffer_append_byte(&track->buffer, va_arg(ap, unsigned int));
		va_end(ap);
	}
}

void midi_track_write_tempo(struct midi_track *track, uint32_t deltaTime, uint32_t tempo) {
	midi_track_write_meta_event(track, deltaTime, 0x51, 3, (tempo >> 16) & 0xff, (tempo >> 8) & 0xff, (tempo >> 0) & 0xff);
}

void midi_track_write_time_signature(struct midi_track *track, uint32_t deltaTime, uint8_t numerator, uint8_t denominator, uint8_t ticksPerClick, uint8_t quarterNote32ndNotes) {
	if(quarterNote32ndNotes > 0 || ticksPerClick > 0)
		midi_track_write_meta_event(track, deltaTime, 0x58, 4, numerator, denominator, ticksPerClick, quarterNote32ndNotes);
	else
		midi_track_write_meta_event(track, deltaTime, 0x58, 2, numerator, denominator);
}

void midi_track_write_track_end(struct midi_track *track, uint32_t deltaTime) {
	midi_track_write_meta_event(track, deltaTime, 0x2f, 0);
}

void midi_track_write_program_change(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint8_t program) {
	midi_track_write_delta_time(track, deltaTime);
	uint8_t cmd = 0xc0 | (channel & 0x0f);
	midi_track_write_cmd(track, cmd);
	buffer_append_byte(&track->buffer, program & 0x7f);
}

void midi_track_write_text_event(struct midi_track *track, uint8_t event, uint32_t deltaTime, const char *text, int len) {
	if(len < 0) len = strlen(text);
	if(len > 127) len = 127;
	midi_track_write_meta_event_buf(track, deltaTime, event, len, (uint8_t *)text);
}

void midi_track_write_text(struct midi_track *track, uint32_t deltaTime, const char *text, int len) {
	midi_track_write_text_event(track, 0x01, deltaTime, text, len);
}

void midi_track_write_track_name(struct midi_track *track, uint32_t deltaTime, const char *text, int len) {
	midi_track_write_text_event(track, 0x03, deltaTime, text, len);
}

void midi_track_write_pitch_bend(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t bend) {
	midi_track_write_delta_time(track, deltaTime);
	midi_track_write_cmd(track, 0xe0 | (channel & 0x0f));
	buffer_append_byte(&track->buffer, bend & 0x7f);
	buffer_append_byte(&track->buffer, (bend >> 7) & 0x7f);
}

void midi_track_write_control_change(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint8_t controller, uint8_t value) {
	midi_track_write_delta_time(track, deltaTime);
	midi_track_write_cmd_no_reuse(track, 0xb0 | (channel & 0x0f));
	buffer_append_byte(&track->buffer, controller & 0x7f);
	buffer_append_byte(&track->buffer, value & 0x7f);
}

void midi_track_write_nrpn_msb(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t number, uint8_t value_msb) {
	midi_track_write_control_change(track, deltaTime, channel, 99, (number >> 7) & 0x7f);
	midi_track_write_control_change(track, 0, channel, 98, number & 0x7f);
	midi_track_write_control_change(track, 0, channel, 6, value_msb & 0x7f);
}
void midi_track_write_nrpn_msb_lsb(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t number, uint8_t value_msb, uint8_t value_lsb) {
	midi_track_write_control_change(track, deltaTime, channel, 99, (number >> 7) & 0x7f);
	midi_track_write_control_change(track, 0, channel, 98, number & 0x7f);
	midi_track_write_control_change(track, 0, channel, 6, value_msb & 0x7f);
	midi_track_write_control_change(track, 0, channel, 38, value_lsb & 0x7f);
}

void midi_track_write_rpn_msb(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t number, uint8_t value_msb) {
	midi_track_write_control_change(track, deltaTime, channel, 101, (number >> 7) & 0x7f);
	midi_track_write_control_change(track, 0, channel, 100, number & 0x7f);
	midi_track_write_control_change(track, 0, channel, 6, value_msb & 0x7f);
}

void midi_track_write_rpn_msb_lsb(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t number, uint8_t value_msb, uint8_t value_lsb) {
	midi_track_write_control_change(track, deltaTime, channel, 101, (number >> 7) & 0x7f);
	midi_track_write_control_change(track, 0, channel, 100, number & 0x7f);
	midi_track_write_control_change(track, 0, channel, 6, value_msb & 0x7f);
	midi_track_write_control_change(track, 0, channel, 38, value_lsb & 0x7f);
}

void midi_track_write_bank_select(struct midi_track *track, uint32_t deltaTime, uint8_t channel, uint16_t bank) {
	midi_track_write_control_change(track, deltaTime, channel, 0, (bank >> 7) & 0x7f);
	midi_track_write_control_change(track, 0, channel, 32, bank & 0x7f);
}
