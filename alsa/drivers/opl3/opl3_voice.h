#ifndef __OPL3_VOICE_H
#define __OPL3_VOICE_H

/*
 *  Copyright (c) 2000 Uros Bizjak <uros@kss-loka.si>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <sound/opl3.h>

/* Prototypes for opl3_seq.c */
int snd_opl3_synth_use_inc(opl3_t * opl3);
void snd_opl3_synth_use_dec(opl3_t * opl3);
int snd_opl3_synth_setup(opl3_t * opl3);
void snd_opl3_synth_cleanup(opl3_t * opl3);

/* Prototypes for opl3_midi.c */
void snd_opl3_note_on(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_opl3_note_off(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_opl3_key_press(void *p, int note, int vel, struct snd_midi_channel *chan);
void snd_opl3_terminate_note(void *p, int note, snd_midi_channel_t *chan);
void snd_opl3_control(void *p, int type, struct snd_midi_channel *chan);
void snd_opl3_nrpn(void *p, snd_midi_channel_t *chan, snd_midi_channel_set_t *chset);
void snd_opl3_sysex(void *p, unsigned char *buf, int len, int parsed, snd_midi_channel_set_t *chset);

void snd_opl3_calc_volume(unsigned char *reg, int vel, snd_midi_channel_t *chan);
void snd_opl3_timer_func(unsigned long data);

/* Prototypes for opl3_drums.c */
void snd_opl3_load_drums(opl3_t *opl3);
void snd_opl3_drum_switch(opl3_t *opl3, int note, int on_off, int vel, snd_midi_channel_t *chan);

/* Prototypes for opl3_oss.c */
#ifdef CONFIG_SND_OSSEMUL
void snd_opl3_init_seq_oss(opl3_t *opl3, char *name);
void snd_opl3_free_seq_oss(opl3_t *opl3);
#endif

#endif
