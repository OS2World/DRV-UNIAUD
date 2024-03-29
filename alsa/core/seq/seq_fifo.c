/*
 *   ALSA sequencer FIFO
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

#include <sound/driver.h>
#include "seq_fifo.h"
#include "seq_lock.h"


/* FIFO */

/* create new fifo */
fifo_t *snd_seq_fifo_new(int poolsize)
{
	fifo_t *f;

	f = snd_kcalloc(sizeof(fifo_t), GFP_KERNEL);
	if (f == NULL) {
		snd_printd("malloc failed for snd_seq_fifo_new() \n");
		return NULL;
	}

	f->pool = snd_seq_pool_new(poolsize);
	if (f->pool == NULL) {
		kfree(f);
		return NULL;
	}
	if (snd_seq_pool_init(f->pool) < 0) {
		snd_seq_pool_delete(&f->pool);
		kfree(f);
		return NULL;
	}

	spin_lock_init(&f->lock);
	snd_use_lock_init(&f->use_lock);
	init_waitqueue_head(&f->input_sleep);
	atomic_set(&f->overflow, 0);

	f->head = NULL;
	f->tail = NULL;
	f->cells = 0;
	
	return f;
}

void snd_seq_fifo_delete(fifo_t **fifo)
{
	fifo_t *f;

	snd_assert(fifo != NULL, return);
	f = *fifo;
	snd_assert(f != NULL, return);
	*fifo = NULL;

	snd_seq_fifo_clear(f);

	/* wake up clients if any */
	if (waitqueue_active(&f->input_sleep))
		wake_up(&f->input_sleep);

	/* release resources...*/
	/*....................*/

	if (f->pool) {
		snd_seq_pool_done(f->pool);
		snd_seq_pool_delete(&f->pool);
	}
	
	kfree(f);
}

static snd_seq_event_cell_t *fifo_cell_out(fifo_t *f);

/* clear queue */
void snd_seq_fifo_clear(fifo_t *f)
{
	snd_seq_event_cell_t *cell;
	unsigned long flags;

	/* clear overflow flag */
	atomic_set(&f->overflow, 0);

	snd_use_lock_sync(&f->use_lock);
	spin_lock_irqsave(&f->lock, flags);
	/* drain the fifo */
	while ((cell = fifo_cell_out(f)) != NULL) {
		snd_seq_cell_free(cell);
	}
	spin_unlock_irqrestore(&f->lock, flags);
}


/* enqueue event to fifo */
int snd_seq_fifo_event_in(fifo_t *f, snd_seq_event_t *event)
{
	snd_seq_event_cell_t *cell;
	unsigned long flags;
	int err;

	snd_assert(f != NULL, return -EINVAL);

	snd_use_lock_use(&f->use_lock);
	err = snd_seq_event_dup(f->pool, event, &cell, 1, NULL); /* always non-blocking */
	if (err < 0) {
		if (err == -ENOMEM)
			atomic_inc(&f->overflow);
		snd_use_lock_free(&f->use_lock);
		return err;
	}
		
	/* append new cells to fifo */
	spin_lock_irqsave(&f->lock, flags);
	if (f->tail != NULL)
		f->tail->next = cell;
	f->tail = cell;
	if (f->head == NULL)
		f->head = cell;
	f->cells++;
	spin_unlock_irqrestore(&f->lock, flags);

	/* wakeup client */
	if (waitqueue_active(&f->input_sleep))
		wake_up(&f->input_sleep);

	snd_use_lock_free(&f->use_lock);

	return 0; /* success */

}

/* dequeue cell from fifo */
static snd_seq_event_cell_t *fifo_cell_out(fifo_t *f)
{
	snd_seq_event_cell_t *cell;

	if ((cell = f->head) != NULL) {
		f->head = cell->next;

		/* reset tail if this was the last element */
		if (f->tail == cell)
			f->tail = NULL;

		cell->next = NULL;
		f->cells--;
	}

	return cell;
}

/* dequeue cell from fifo and copy on user space */
int snd_seq_fifo_cell_out(fifo_t *f, snd_seq_event_cell_t **cellp, int nonblock)
{
	snd_seq_event_cell_t *cell;
	unsigned long flags;

	snd_assert(f != NULL, return -EINVAL);

	*cellp = NULL;
	spin_lock_irqsave(&f->lock, flags);
	while ((cell = fifo_cell_out(f)) == NULL) {
		if (nonblock) {
			/* non-blocking - return immediately */
			spin_unlock_irqrestore(&f->lock, flags);
			return -EAGAIN;
		}
		snd_seq_sleep_in_lock(&f->input_sleep, &f->lock);

		if (signal_pending(current)) {
			spin_unlock_irqrestore(&f->lock, flags);
			return -ERESTARTSYS;
		}
	}
	*cellp = cell;
	spin_unlock_irqrestore(&f->lock, flags);

	return 0;
}


void snd_seq_fifo_cell_putback(fifo_t *f, snd_seq_event_cell_t *cell)
{
	unsigned long flags;

	if (cell) {
		spin_lock_irqsave(&f->lock, flags);
		cell->next = f->head;
		f->head = cell;
		f->cells++;
		spin_unlock_irqrestore(&f->lock, flags);
	}
}


/* polling; return non-zero if queue is available */
int snd_seq_fifo_poll_wait(fifo_t *f, struct file *file, poll_table *wait)
{
	poll_wait(file, &f->input_sleep, wait);
	return (f->cells > 0);
}

/* change the size of pool; all old events are removed */
int snd_seq_fifo_resize(fifo_t *f, int poolsize)
{
	unsigned long flags;
	pool_t *newpool, *oldpool;
	snd_seq_event_cell_t *cell, *next, *oldhead;

	snd_assert(f != NULL && f->pool != NULL, return -EINVAL);

	/* allocate new pool */
	newpool = snd_seq_pool_new(poolsize);
	if (newpool == NULL)
		return -ENOMEM;
	if (snd_seq_pool_init(newpool) < 0) {
		snd_seq_pool_delete(&newpool);
		return -ENOMEM;
	}

	spin_lock_irqsave(&f->lock, flags);
	/* remember old pool */
	oldpool = f->pool;
	oldhead = f->head;
	/* exchange pools */
	f->pool = newpool;
	f->head = NULL;
	f->tail = NULL;
	f->cells = 0;
	/* NOTE: overflow flag is not cleared */
	spin_unlock_irqrestore(&f->lock, flags);

	/* release cells in old pool */
	for (cell = oldhead; cell; cell = next) {
		next = cell->next;
		snd_seq_cell_free(cell);
	}
	snd_seq_pool_delete(&oldpool);

	return 0;
}
