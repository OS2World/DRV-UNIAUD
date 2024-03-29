/*
 *   ALSA sequencer Priority Queue
 *   Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@home.nl>
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
#include "seq_prioq.h"
#include "seq_timer.h"


/* Implementation is a simple linked list for now...

   This priority queue orders the events on timestamp. For events with an
   equeal timestamp the queue behaves as a FIFO. 

   *
   *           +-------+
   *  Head --> | first |
   *           +-------+
   *                 |next
   *           +-----v-+
   *           |       |
   *           +-------+
   *                 |
   *           +-----v-+
   *           |       |
   *           +-------+
   *                 |
   *           +-----v-+
   *  Tail --> | last  |
   *           +-------+
   *

 */



/* create new prioq (constructor) */
prioq_t *snd_seq_prioq_new(void)
{
	prioq_t *f;

	f = snd_kcalloc(sizeof(prioq_t), GFP_KERNEL);
	if (f == NULL) {
		snd_printd("oops: malloc failed for snd_seq_prioq_new()\n");
		return NULL;
	}
	
	spin_lock_init(&f->lock);
	f->head = NULL;
	f->tail = NULL;
	f->cells = 0;
	
	return f;
}

/* delete prioq (destructor) */
void snd_seq_prioq_delete(prioq_t **fifo)
{
	prioq_t *f = *fifo;
	*fifo = NULL;

	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_delete() called with NULL prioq\n");
		return;
	}

	/* release resources...*/
	/*....................*/
	
	if (f->cells > 0) {
		/* drain prioQ */
		int max = 0;
		
		while (f->cells > 0 && max++ < SNDRV_SEQ_MAX_EVENTS)
			snd_seq_cell_free(snd_seq_prioq_cell_out(f));
		if (max >= SNDRV_SEQ_MAX_EVENTS)
			printk("oops: snd_seq_prioq_delete() drain trouble\n");
	}
	
	kfree(f);
}




/* compare timestamp between events */
/* return 1 if a >= b; 0 */
static inline int compare_timestamp(snd_seq_event_t * a, snd_seq_event_t * b)
{
	if ((a->flags & SNDRV_SEQ_TIME_STAMP_MASK) == SNDRV_SEQ_TIME_STAMP_TICK) {
		/* compare ticks */
		return (snd_seq_compare_tick_time(&a->time.tick, &b->time.tick));
	} else {
		/* compare real time */
		return (snd_seq_compare_real_time(&a->time.time, &b->time.time));
	}
}

/* compare timestamp between events */
/* return negative if a < b;
 *        zero     if a = b;
 *        positive if a > b;
 */
static inline int compare_timestamp_rel(snd_seq_event_t *a, snd_seq_event_t *b)
{
	if ((a->flags & SNDRV_SEQ_TIME_STAMP_MASK) == SNDRV_SEQ_TIME_STAMP_TICK) {
		/* compare ticks */
		if (a->time.tick > b->time.tick)
			return 1;
		else if (a->time.tick == b->time.tick)
			return 0;
		else
			return -1;
	} else {
		/* compare real time */
		if (a->time.time.tv_sec > b->time.time.tv_sec)
			return 1;
		else if (a->time.time.tv_sec == b->time.time.tv_sec) {
			if (a->time.time.tv_nsec > b->time.time.tv_nsec)
				return 1;
			else if (a->time.time.tv_nsec == b->time.time.tv_nsec)
				return 0;
			else
				return -1;
		} else
			return -1;
	}
}

/* enqueue cell to prioq */
void snd_seq_prioq_cell_in(prioq_t * f, snd_seq_event_cell_t * cell)
{
	snd_seq_event_cell_t *cur, *prev;
	unsigned long flags;
	int prior;

	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_cell_in() called with NULL prioq\n");
		return;
	}
	if (cell == NULL) {
		snd_printd("oops: snd_seq_prioq_cell_in() called with NULL cell\n");
		return;
	}
	
	/* check flags */
	prior = (cell->event.flags & SNDRV_SEQ_PRIORITY_MASK);

	spin_lock_irqsave(&f->lock, flags);

	/* check if this element needs to inserted at the end (ie. ordered 
	   data is inserted) This will be very likeley if a sequencer 
	   application or midi file player is feeding us (sequential) data */
	if (f->tail && !prior) {
		if (compare_timestamp(&cell->event, &f->tail->event)) {
			/* add new cell to tail of the fifo */
			f->tail->next = cell;
			f->tail = cell;
			cell->next = NULL;
			f->cells++;
			spin_unlock_irqrestore(&f->lock, flags);
			return;
		}
	}
	/* traverse list of elements to find the place where the new cell is
	   to be inserted... Note that this is a order n process ! */

	prev = NULL;		/* previous cell */
	cur = f->head;		/* cursor */

	while (cur != NULL) {
		/* compare timestamps */
		int rel = compare_timestamp_rel(&cell->event, &cur->event);
		if (rel < 0)
			/* new cell has earlier schedule time, */
			break;
		else if (rel == 0 && prior)
			/* equal schedule time and prior to others */
			break;
		/* new cell has equal or larger schedule time, */
		/* move cursor to next cell */
		prev = cur;
		cur = cur->next;
	}

	/* insert it before cursor */
	if (prev != NULL)
		prev->next = cell;
	cell->next = cur;

	if (f->head == cur) /* this is the first cell, set head to it */
		f->head = cell;
	if (cur == NULL) /* reached end of the list */
		f->tail = cell;
	f->cells++;
	spin_unlock_irqrestore(&f->lock, flags);
}

/* dequeue cell from prioq */
snd_seq_event_cell_t *snd_seq_prioq_cell_out(prioq_t * f)
{
	snd_seq_event_cell_t *cell;
	unsigned long flags;

	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_cell_in() called with NULL prioq\n");
		return NULL;
	}
	spin_lock_irqsave(&f->lock, flags);

	cell = f->head;
	if (cell) {
		f->head = cell->next;

		/* reset tail if this was the last element */
		if (f->tail == cell)
			f->tail = NULL;

		cell->next = NULL;
		f->cells--;
	}

	spin_unlock_irqrestore(&f->lock, flags);
	return cell;
}

/* return number of events available in prioq */
int snd_seq_prioq_avail(prioq_t * f)
{
	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_cell_in() called with NULL prioq\n");
		return 0;
	}
	return f->cells;
}


/* peek at cell at the head of the prioq */
snd_seq_event_cell_t *snd_seq_prioq_cell_peek(prioq_t * f)
{
	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_cell_in() called with NULL prioq\n");
		return NULL;
	}
	return f->head;
}


static inline int prioq_match(snd_seq_event_cell_t *cell, int client, int timestamp)
{
	if (cell->event.source.client == client ||
	    cell->event.dest.client == client)
		return 1;
	if (!timestamp)
		return 0;
	switch (cell->event.flags & SNDRV_SEQ_TIME_STAMP_MASK) {
	case SNDRV_SEQ_TIME_STAMP_TICK:
		if (cell->event.time.tick)
			return 1;
		break;
	case SNDRV_SEQ_TIME_STAMP_REAL:
		if (cell->event.time.time.tv_sec ||
		    cell->event.time.time.tv_nsec)
			return 1;
		break;
	}
	return 0;
}

/* remove cells for left client */
void snd_seq_prioq_leave(prioq_t * f, int client, int timestamp)
{
	register snd_seq_event_cell_t *cell, *next;
	unsigned long flags;
	snd_seq_event_cell_t *prev = NULL;
	snd_seq_event_cell_t *freefirst = NULL, *freeprev = NULL, *freenext;

	/* collect all removed cells */
	spin_lock_irqsave(&f->lock, flags);
	cell = f->head;
	while (cell) {
		next = cell->next;
		if (prioq_match(cell, client, timestamp)) {
			/* remove cell from prioq */
			if (cell == f->head) {
				f->head = cell->next;
			} else {
				prev->next = cell->next;
			}
			if (cell == f->tail)
				f->tail = cell->next;
			f->cells--;
			/* add cell to free list */
			cell->next = NULL;
			if (freefirst == NULL) {
				freefirst = cell;
			} else {
				freeprev->next = cell;
			}
			freeprev = cell;
		} else {
#if 0
			printk("type = %i, source = %i, dest = %i, client = %i\n",
				cell->event.type,
				cell->event.source.client,
				cell->event.dest.client,
				client);
#endif
			prev = cell;
		}
		cell = next;		
	}
	spin_unlock_irqrestore(&f->lock, flags);	

	/* remove selected cells */
	while (freefirst) {
		freenext = freefirst->next;
		snd_seq_cell_free(freefirst);
		freefirst = freenext;
	}
}

static int prioq_remove_match(snd_seq_remove_events_t *info,
	snd_seq_event_t *ev)
{
	int res;

	if (info->remove_mode & SNDRV_SEQ_REMOVE_DEST) {
		if (ev->dest.client != info->dest.client ||
				ev->dest.port != info->dest.port)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_DEST_CHANNEL) {
		if (! snd_seq_ev_is_channel_type(ev))
			return 0;
		/* data.note.channel and data.control.channel are identical */
		if (ev->data.note.channel != info->channel)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_TIME_AFTER) {
		if (info->remove_mode & SNDRV_SEQ_REMOVE_TIME_TICK)
			res = snd_seq_compare_tick_time(&ev->time.tick, &info->time.tick);
		else
			res = snd_seq_compare_real_time(&ev->time.time, &info->time.time);
		if (!res)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_TIME_BEFORE) {
		if (info->remove_mode & SNDRV_SEQ_REMOVE_TIME_TICK)
			res = snd_seq_compare_tick_time(&ev->time.tick, &info->time.tick);
		else
			res = snd_seq_compare_real_time(&ev->time.time, &info->time.time);
		if (res)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_EVENT_TYPE) {
		if (ev->type != info->type)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_IGNORE_OFF) {
		/* Do not remove off events */
		switch (ev->type) {
		case SNDRV_SEQ_EVENT_NOTEOFF:
		/* case SNDRV_SEQ_EVENT_SAMPLE_STOP: */
			return 0;
		default:
			break;
		}
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_TAG_MATCH) {
		if (info->tag != ev->tag)
			return 0;
	}

	return 1;
}

/* remove cells matching remove criteria */
void snd_seq_prioq_remove_events(prioq_t * f, int client,
	snd_seq_remove_events_t *info)
{
	register snd_seq_event_cell_t *cell, *next;
	unsigned long flags;
	snd_seq_event_cell_t *prev = NULL;
	snd_seq_event_cell_t *freefirst = NULL, *freeprev = NULL, *freenext;

	/* collect all removed cells */
	spin_lock_irqsave(&f->lock, flags);
	cell = f->head;

	while (cell) {
		next = cell->next;
		if (cell->event.source.client == client &&
			prioq_remove_match(info, &cell->event)) {

			/* remove cell from prioq */
			if (cell == f->head) {
				f->head = cell->next;
			} else {
				prev->next = cell->next;
			}

			if (cell == f->tail)
				f->tail = cell->next;
			f->cells--;

			/* add cell to free list */
			cell->next = NULL;
			if (freefirst == NULL) {
				freefirst = cell;
			} else {
				freeprev->next = cell;
			}

			freeprev = cell;
		} else {
			prev = cell;
		}
		cell = next;		
	}
	spin_unlock_irqrestore(&f->lock, flags);	

	/* remove selected cells */
	while (freefirst) {
		freenext = freefirst->next;
		snd_seq_cell_free(freefirst);
		freefirst = freenext;
	}
}


