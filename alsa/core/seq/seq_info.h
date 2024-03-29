/*
 *   ALSA sequencer /proc info
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@home.nl>
 *
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
#ifndef __SND_SEQ_INFO_H
#define __SND_SEQ_INFO_H

#include <sound/info.h>
#include <sound/seq_kernel.h>

extern void snd_seq_info_clients_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer);
extern void snd_seq_info_memory_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer);
extern void snd_seq_info_timer_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer);
extern void snd_seq_info_queues_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer);


extern int snd_seq_info_init( void );
extern int snd_seq_info_done( void );


#endif
