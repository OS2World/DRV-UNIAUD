/*
 *  Advanced Linux Sound Architecture - ALSA
 *
 *  Interface file between ALSA driver & user space
 *  Copyright (c) 1994-98 by Jaroslav Kysela <perex@suse.cz>,
 *                           4Front Technologies
 *
 *  Direct FM control
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
 *
 */

#ifndef __ASOUND_FM_H
#define __ASOUND_FM_H

#define SNDRV_DM_FM_MODE_OPL2	0x00
#define SNDRV_DM_FM_MODE_OPL3	0x01

typedef struct snd_dm_fm_info {
	unsigned char fm_mode;		/* OPL mode, see SNDRV_DM_FM_MODE_XXX */
	unsigned char rhythm;		/* percussion mode flag */
} snd_dm_fm_info_t;

/*
 *  Data structure composing an FM "note" or sound event.
 */

typedef struct snd_dm_fm_voice {
	unsigned char op;		/* operator cell (0 or 1) */
	unsigned char voice;		/* FM voice (0 to 17) */

	unsigned char am;		/* amplitude modulation */
	unsigned char vibrato;		/* vibrato effect */
	unsigned char do_sustain;	/* sustain phase */
	unsigned char kbd_scale;	/* keyboard scaling */
	unsigned char harmonic;		/* 4 bits: harmonic and multiplier */
	unsigned char scale_level;	/* 2 bits: decrease output freq rises */
	unsigned char volume;		/* 6 bits: volume */

	unsigned char attack;		/* 4 bits: attack rate */
	unsigned char decay;		/* 4 bits: decay rate */
	unsigned char sustain;		/* 4 bits: sustain level */
	unsigned char release;		/* 4 bits: release rate */

	unsigned char feedback;		/* 3 bits: feedback for op0 */
	unsigned char connection;	/* 0 for serial, 1 for parallel */
	unsigned char left;		/* stereo left */
	unsigned char right;		/* stereo right */
	unsigned char waveform;		/* 3 bits: waveform shape */
} snd_dm_fm_voice_t;

/*
 *  This describes an FM note by its voice, octave, frequency number (10bit)
 *  and key on/off.
 */

typedef struct snd_dm_fm_note {
	unsigned char voice;	/* 0-17 voice channel */
	unsigned char octave;	/* 3 bits: what octave to play */
	unsigned int fnum;	/* 10 bits: frequency number */
	unsigned char key_on;	/* set for active, clear for silent */
} snd_dm_fm_note_t;

/*
 *  FM parameters that apply globally to all voices, and thus are not "notes"
 */

typedef struct snd_dm_fm_params {
	unsigned char am_depth;		/* amplitude modulation depth (1=hi) */
	unsigned char vib_depth;	/* vibrato depth (1=hi) */
	unsigned char kbd_split;	/* keyboard split */
	unsigned char rhythm;		/* percussion mode select */

	/* This block is the percussion instrument data */
	unsigned char bass;
	unsigned char snare;
	unsigned char tomtom;
	unsigned char cymbal;
	unsigned char hihat;
} snd_dm_fm_params_t;

/*
 *  FM mode ioctl settings
 */

#define SNDRV_DM_FM_IOCTL_INFO		_IOR('H', 0x20, snd_dm_fm_info_t)
#define SNDRV_DM_FM_IOCTL_RESET		_IO ('H', 0x21)
#define SNDRV_DM_FM_IOCTL_PLAY_NOTE	_IOW('H', 0x22, snd_dm_fm_note_t)
#define SNDRV_DM_FM_IOCTL_SET_VOICE	_IOW('H', 0x23, snd_dm_fm_voice_t)
#define SNDRV_DM_FM_IOCTL_SET_PARAMS	_IOW('H', 0x24, snd_dm_fm_params_t)
#define SNDRV_DM_FM_IOCTL_SET_MODE	_IOW('H', 0x25, int)
/* for OPL3 only */
#define SNDRV_DM_FM_IOCTL_SET_CONNECTION	_IOW('H', 0x26, int)

#ifdef __SND_OSS_COMPAT__

#define SNDRV_DM_FM_OSS_IOCTL_RESET		0x20
#define SNDRV_DM_FM_OSS_IOCTL_PLAY_NOTE		0x21
#define SNDRV_DM_FM_OSS_IOCTL_SET_VOICE		0x22
#define SNDRV_DM_FM_OSS_IOCTL_SET_PARAMS	0x23
#define SNDRV_DM_FM_OSS_IOCTL_SET_MODE		0x24
#define SNDRV_DM_FM_OSS_IOCTL_SET_OPL		0x25

#endif

#endif				/* __ASOUND_FM_H */
