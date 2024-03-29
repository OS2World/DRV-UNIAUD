/*
 *   ALSA sequencer Timer
 *   Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@home.nl>
 *                              Jaroslav Kysela <perex@suse.cz>
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

#include <sound/driver.h>
#include "seq_timer.h"
#include "seq_queue.h"
#include "seq_info.h"

extern int snd_seq_default_timer_class;
extern int snd_seq_default_timer_sclass;
extern int snd_seq_default_timer_card;
extern int snd_seq_default_timer_device;
extern int snd_seq_default_timer_subdevice;
extern int snd_seq_default_timer_resolution;

void snd_seq_timer_set_tick_resolution(seq_timer_tick_t *tick, int tempo, int ppq, int nticks)
{
	if (tempo < 1000000)
		tick->resolution = (tempo * 1000) / ppq;
	else {
		/* might overflow.. */
		unsigned int s;
		s = tempo % ppq;
		s = (s * 1000) / ppq;
		tick->resolution = (tempo / ppq) * 1000;
		tick->resolution += s;
	}
	if (tick->resolution <= 0)
		tick->resolution = 1;
	tick->resolution *= nticks;
	snd_seq_timer_update_tick(tick, 0);
}

/* create new timer (constructor) */
seq_timer_t *snd_seq_timer_new(void)
{
	seq_timer_t *tmr;
	
	tmr = snd_kcalloc(sizeof(seq_timer_t), GFP_KERNEL);
	if (tmr == NULL) {
		snd_printd("malloc failed for snd_seq_timer_new() \n");
		return NULL;
	}
	spin_lock_init(&tmr->lock);

	/* reset setup to defaults */
	snd_seq_timer_defaults(tmr);
	
	/* reset time */
	snd_seq_timer_reset(tmr);
	
	return tmr;
}

/* delete timer (destructor) */
void snd_seq_timer_delete(seq_timer_t **tmr)
{
	seq_timer_t *t = *tmr;
	*tmr = NULL;

	if (t == NULL) {
		snd_printd("oops: snd_seq_timer_delete() called with NULL timer\n");
		return;
	}
	t->running = 0;

	/* reset time */
	snd_seq_timer_stop(t);
	snd_seq_timer_reset(t);

	kfree(t);
}

void snd_seq_timer_defaults(seq_timer_t * tmr)
{
	/* setup defaults */
	tmr->ppq = 96;		/* 96 PPQ */
	tmr->tempo = 500000;	/* 120 BPM */
	snd_seq_timer_set_tick_resolution(&tmr->tick, tmr->tempo, tmr->ppq, 1);
	tmr->running = 0;
	tmr->ticks = 1;

	tmr->type = SNDRV_SEQ_TIMER_ALSA;
	tmr->alsa_id.dev_class = snd_seq_default_timer_class;
	tmr->alsa_id.dev_sclass = snd_seq_default_timer_sclass;
	tmr->alsa_id.card = snd_seq_default_timer_card;
	tmr->alsa_id.device = snd_seq_default_timer_device;
	tmr->alsa_id.subdevice = snd_seq_default_timer_subdevice;
	tmr->resolution = snd_seq_default_timer_resolution;
	if (tmr->resolution > 0)
		tmr->base_period = 1000000000 / tmr->resolution;
	else
		tmr->base_period = HZ;
	tmr->period = tmr->base_period;
}

void snd_seq_timer_reset(seq_timer_t * tmr)
{
	unsigned long flags;

	spin_lock_irqsave(&tmr->lock, flags);

	/* reset time & songposition */
	tmr->cur_time.tv_sec = 0;
	tmr->cur_time.tv_nsec = 0;

	tmr->tick.cur_tick = 0;
	tmr->tick.fraction = 0;

#ifdef SNDRV_SEQ_SYNC_SUPPORT
	tmr->sync_start = 0;
#endif

	spin_unlock_irqrestore(&tmr->lock, flags);
}


/* called by timer interrupt routine. the period time since previous invocation is passed */
static void snd_seq_timer_interrupt(snd_timer_instance_t *timeri,
				    unsigned long resolution,
				    unsigned long ticks, void *data)
{
	unsigned long flags;
	queue_t *q = (queue_t *)data;
	seq_timer_t *tmr;

	if (q == NULL)
		return;
	tmr = q->timer;
	if (tmr == NULL)
		return;
	if (!tmr->running)
		return;

	resolution = tmr->period;

	spin_lock_irqsave(&tmr->lock, flags);

	/* update timer */
	snd_seq_inc_time_nsec(&tmr->cur_time, resolution);

	/* calculate current tick */
	snd_seq_timer_update_tick(&tmr->tick, resolution);

	/* register actual time of this timer update  */
	do_gettimeofday(&tmr->last_update);

	spin_unlock_irqrestore(&tmr->lock, flags);

#ifdef SNDRV_SEQ_SYNC_SUPPORT
	/* check sync */
	snd_seq_sync_check(q, resolution, 1, 0);
#endif

	/* check queues and dispatch events */
	snd_seq_check_queue(q, 1, 0);
}


int snd_seq_timer_set_tempo(seq_timer_t * tmr, int tempo)
{
	unsigned long flags;

	if (tmr && tempo > 0) {
		spin_lock_irqsave(&tmr->lock, flags);
		if (tempo != tmr->tempo) {
			tmr->tempo = tempo;
			snd_seq_timer_set_tick_resolution(&tmr->tick, tmr->tempo, tmr->ppq, 1);
		}
		spin_unlock_irqrestore(&tmr->lock, flags);
		return 0;
	}
	return -EINVAL;
}


int snd_seq_timer_set_ppq(seq_timer_t * tmr, int ppq)
{
	unsigned long flags;

	if (tmr && ppq > 0) {
		if (tmr->running && (ppq != tmr->ppq)) {
			/* refuse to change ppq on running timers */
			/* because it will upset the song position (ticks) */
			snd_printd("seq: cannot change ppq of a running timer\n");
			return -EBUSY;
		} else {
			spin_lock_irqsave(&tmr->lock, flags);
			tmr->ppq = ppq;
			snd_seq_timer_set_tick_resolution(&tmr->tick, tmr->tempo, tmr->ppq, 1);
			spin_unlock_irqrestore(&tmr->lock, flags);		
		}
		return 0;
	}
	return -EINVAL;
}


extern int snd_seq_timer_set_position_tick(seq_timer_t *tmr, snd_seq_tick_time_t position)
{
	unsigned long flags;

	if (tmr) {
		if (tmr->running)
			return -EBUSY;
		spin_lock_irqsave(&tmr->lock, flags);
		/* set position */
		tmr->tick.cur_tick = position;
		tmr->tick.fraction = 0;
		spin_unlock_irqrestore(&tmr->lock, flags);
		return 0;
	}
	return -EINVAL;
}


extern int snd_seq_timer_set_position_time(seq_timer_t *tmr, snd_seq_real_time_t position)
{
	unsigned long flags;

	if (tmr) {
		if (tmr->running)
			return -EBUSY;
		spin_lock_irqsave(&tmr->lock, flags);
		/* set position */
		snd_seq_sanity_real_time(&position);
		tmr->cur_time = position;
		spin_unlock_irqrestore(&tmr->lock, flags);
		return 0;
	}
	return -EINVAL;
}


int snd_seq_timer_open(queue_t *q)
{
	snd_timer_instance_t *t;
	seq_timer_t *tmr;
	unsigned int tmp, tmp1;
	char str[32];

	tmr = q->timer;
	snd_assert(tmr != NULL, return -EINVAL);
	if (tmr->timeri)
		return -EBUSY;
	sprintf(str, "sequencer queue %i", q->queue);
	if (tmr->type == SNDRV_SEQ_TIMER_ALSA) {	/* standard ALSA timer */
		t = snd_timer_open(str, &tmr->alsa_id);
		if (t == NULL && tmr->alsa_id.dev_class != SNDRV_TIMER_CLASS_SLAVE) {
			if (tmr->alsa_id.dev_class != SNDRV_TIMER_CLASS_GLOBAL &&
			    tmr->alsa_id.device != SNDRV_TIMER_GLOBAL_SYSTEM) {
				snd_timer_id_t tid;
				memset(&tid, 0, sizeof(tid));
				tid.dev_class = SNDRV_TIMER_CLASS_GLOBAL;
				tid.device = SNDRV_TIMER_GLOBAL_SYSTEM;
				t = snd_timer_open(str, &tid);
			}
			if (t == NULL) {
				snd_printk("fatal error: cannot create timer\n");
				return -ENODEV;
			}
		}
		t->slave_class = SNDRV_TIMER_SCLASS_SEQUENCER;
		t->slave_id = q->queue;
	} else {
		return -EINVAL;
	}
	t->callback = snd_seq_timer_interrupt;
	t->callback_data = q;
	tmp = 1000000000UL / tmr->resolution;
	if (t->timer->hw.flags & SNDRV_TIMER_HW_SLAVE) {
		tmr->ticks = 1;
		tmr->period = tmr->base_period = tmp;
	} else {
		tmp1 = snd_timer_resolution(t);
		if (tmp1 >= tmp) {
			tmr->ticks = 1;
		} else {
			tmr->ticks = tmp / tmp1;
		}
		tmr->period = tmr->base_period = tmp1 * tmr->ticks;
	}
	t->flags |= SNDRV_TIMER_IFLG_AUTO;
	tmr->timeri = t;
	return 0;
}

int snd_seq_timer_close(queue_t *q)
{
	seq_timer_t *tmr;
	
	tmr = q->timer;
	snd_assert(tmr != NULL, return -EINVAL);
	if (tmr->timeri) {
		snd_timer_stop(tmr->timeri);
		snd_timer_close(tmr->timeri);
		tmr->timeri = NULL;
	}
	return 0;
}

void snd_seq_timer_stop(seq_timer_t * tmr)
{
	if (!tmr->running)
		return;
	tmr->running = 0;
	snd_timer_stop(tmr->timeri);
}

void snd_seq_timer_start(seq_timer_t * tmr)
{
	if (tmr->running)
		snd_seq_timer_stop(tmr);
	snd_seq_timer_reset(tmr);
	snd_timer_start(tmr->timeri, tmr->ticks);
	tmr->running = 1;
	do_gettimeofday(&tmr->last_update);
}

void snd_seq_timer_continue(seq_timer_t * tmr)
{
	if (tmr->running)
		return;
	snd_timer_start(tmr->timeri, tmr->ticks);
	tmr->running = 1;
	do_gettimeofday(&tmr->last_update);
}

#ifdef SNDRV_SEQ_SYNC_SUPPORT
u64 snd_seq_timer_get_cur_nsec(seq_timer_t *tmr, struct timeval *tm)
{
	u64 tmp;

	tmp = (tm->tv_sec - tmr->last_update.tv_sec) * 1000000 +
		(tm->tv_usec - tmr->last_update.tv_usec);
	tmp = tmp * 1000;
	if (tmr->period != tmr->base_period){
		unsigned long ntime;
		tmp *= tmr->period;
		u64_div(tmp, tmr->base_period, ntime);
		tmp = ntime;
	}
	tmp += (u64)tmr->cur_time.tv_sec * 1000000000UL +
		(u64)tmr->cur_time.tv_nsec;

	return tmp;
}
#endif

/* return current 'real' time. use timeofday() to get better granularity. */
snd_seq_real_time_t snd_seq_timer_get_cur_time(seq_timer_t *tmr)
{
	snd_seq_real_time_t cur_time;

	cur_time = tmr->cur_time;
	if (tmr->running) { 
		struct timeval tm;
#ifdef SNDRV_SEQ_SYNC_SUPPORT
		u64 tmp;
		do_gettimeofday(&tm);
		tmp = snd_seq_timer_get_cur_nsec(tmr, &tm);
		u64_divmod(tmp, 1000000000, cur_time.tv_sec, cur_time.tv_nsec);
#else
		int usec;
		do_gettimeofday(&tm);
		usec = (int)(tm.tv_usec - tmr->last_update.tv_usec);
		if (usec < 0) {
			cur_time.tv_nsec += (1000000 + usec) * 1000;
			cur_time.tv_sec += tm.tv_sec - tmr->last_update.tv_sec - 1;
		} else {
			cur_time.tv_nsec += usec * 1000;
			cur_time.tv_sec += tm.tv_sec - tmr->last_update.tv_sec;
		}
		snd_seq_sanity_real_time(&cur_time);
#endif
	}
                
	return cur_time;	
}

/* TODO: use interpolation on tick queue (will only be usefull for very
 high PPQ values) */
snd_seq_tick_time_t snd_seq_timer_get_cur_tick(seq_timer_t *tmr)
{
	return tmr->tick.cur_tick;
}


/* exported to seq_info.c */
void snd_seq_info_timer_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	int idx;
	queue_t *q;
	seq_timer_t *tmr;
	snd_timer_instance_t *ti;
	unsigned long resolution;
	
	for (idx = 0; idx < SNDRV_SEQ_MAX_QUEUES; idx++) {
		q = queueptr(idx);
		if (q == NULL)
			continue;
		if ((tmr = q->timer) == NULL ||
		    (ti = tmr->timeri) == NULL) {
			queuefree(q);
			continue;
		}
		snd_iprintf(buffer, "Timer for queue %i : %s\n", q->queue, ti->timer->name);
		/*resolution = snd_timer_resolution(ti) * tmr->ticks;*/
		resolution = tmr->base_period;
		snd_iprintf(buffer, "  Base Period time : %lu.%09lu\n", resolution / 1000000000, resolution % 1000000000);
		resolution = tmr->period;
		snd_iprintf(buffer, "  Period time : %lu.%09lu\n", resolution / 1000000000, resolution % 1000000000);
		queuefree(q);
 	}
}
