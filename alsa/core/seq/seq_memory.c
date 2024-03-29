/*
 *  ALSA sequencer Memory Manager
 *  Copyright (c) 1998 by Frank van de Pol <fvdpol@home.nl>
 *                        Jaroslav Kysela <perex@suse.cz>
 *                2000 by Takashi Iwai <tiwai@suse.de>
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

#include <sound/seq_kernel.h>
#include "seq_memory.h"
#include "seq_queue.h"
#include "seq_info.h"
#include "seq_lock.h"

/* semaphore in struct file record */
#define semaphore_of(fp)	((fp)->f_dentry->d_inode->i_sem)


inline static int snd_seq_pool_available(pool_t *pool)
{
	return pool->total_elements - atomic_read(&pool->counter);
}

inline static int snd_seq_output_ok(pool_t *pool)
{
	return snd_seq_pool_available(pool) >= pool->room;
}

/*
 * Variable length event:
 * The event like sysex uses variable length type.
 * The external data may be stored in three different formats.
 * 1) kernel space
 *    This is the normal case.
 *      ext.data.len = length
 *      ext.data.ptr = buffer pointer
 * 2) user space
 *    When an event is generated via read(), the external data is
 *    kept in user space until expanded.
 *      ext.data.len = length | SNDRV_SEQ_EXT_USRPTR
 *      ext.data.ptr = userspace pointer
 * 3) chained cells
 *    When the variable length event is enqueued (in prioq or fifo),
 *    the external data is decomposed to several cells.
 *      ext.data.len = length | SNDRV_SEQ_EXT_CHAINED
 *      ext.data.ptr = the additiona cell head
 *         -> cell.next -> cell.next -> ..
 */

/*
 * exported:
 * expand the variable length event to linear buffer space.
 */

int snd_seq_expand_var_event(const snd_seq_event_t *event, int count, char *buf, int in_kernel, int size_aligned)
{
	int len, newlen;
	snd_seq_event_cell_t *cell;

	if ((event->flags & SNDRV_SEQ_EVENT_LENGTH_MASK) != SNDRV_SEQ_EVENT_LENGTH_VARIABLE)
		return -EINVAL;

	len = event->data.ext.len & ~SNDRV_SEQ_EXT_MASK;
	newlen = len;
	if (size_aligned > 0)
		newlen = ((len + size_aligned - 1) / size_aligned) * size_aligned;
	if (count < newlen)
		return -EAGAIN;

	if (event->data.ext.len & SNDRV_SEQ_EXT_USRPTR) {
		if (! in_kernel)
			return -EINVAL;
		if (copy_from_user(buf, event->data.ext.ptr, len) < 0)
			return -EFAULT;
		return newlen;
	} if (! (event->data.ext.len & SNDRV_SEQ_EXT_CHAINED)) {
		if (in_kernel)
			memcpy(buf, event->data.ext.ptr, len);
		else {
			if (copy_to_user(buf, event->data.ext.ptr, len))
				return -EFAULT;
		}
		return newlen;
	}

	cell = (snd_seq_event_cell_t*)event->data.ext.ptr;
	for (; len > 0 && cell; cell = cell->next) {
		int size = sizeof(snd_seq_event_t);
		if (len < size)
			size = len;
		if (in_kernel)
			memcpy(buf, &cell->event, size);
		else {
			if (copy_to_user(buf, &cell->event, size))
				return -EFAULT;
		}
		buf += size;
		len -= size;
	}
	return newlen;
}



/*
 * exported:
 * call dump function to expand external data.
 */

int snd_seq_dump_var_event(const snd_seq_event_t *event, snd_seq_dump_func_t func, void *private_data)
{
	int len, err;
	snd_seq_event_cell_t *cell;

	if ((event->flags & SNDRV_SEQ_EVENT_LENGTH_MASK) != SNDRV_SEQ_EVENT_LENGTH_VARIABLE)
		return -EINVAL;

	len = event->data.ext.len & ~SNDRV_SEQ_EXT_MASK;
	if (len <= 0)
		return 0;
	if (event->data.ext.len & SNDRV_SEQ_EXT_USRPTR) {
		char buf[32];
		char *curptr = event->data.ext.ptr;
		while (len > 0) {
			int size = sizeof(buf);
			if (len < size)
				size = len;
			if (copy_from_user(buf, curptr, size) < 0)
				return -EFAULT;
			err = func(private_data, buf, size);
			if (err < 0)
				return err;
			curptr += size;
			len -= size;
		}
		return 0;
	} if (! (event->data.ext.len & SNDRV_SEQ_EXT_CHAINED)) {
		return func(private_data, event->data.ext.ptr, len);
	}

	cell = (snd_seq_event_cell_t*)event->data.ext.ptr;
	for (; len > 0 && cell; cell = cell->next) {
		int size = sizeof(snd_seq_event_t);
		if (len < size)
			size = len;
		err = func(private_data, &cell->event, size);
		if (err < 0)
			return err;
		len -= size;
	}
	return 0;
}

/*
 * release this cell, free extended data if available
 */

static inline void free_cell(pool_t *pool, snd_seq_event_cell_t *cell)
{
	cell->next = pool->free;
	pool->free = cell;
	atomic_dec(&pool->counter);
}

void snd_seq_cell_free(snd_seq_event_cell_t * cell)
{
	unsigned long flags;
	pool_t *pool;

	snd_assert(cell != NULL, return);
	pool = cell->pool;
	snd_assert(pool != NULL, return);

	spin_lock_irqsave(&pool->lock, flags);
	free_cell(pool, cell);
	if (snd_seq_ev_is_variable(&cell->event)) {
		if (cell->event.data.ext.len & SNDRV_SEQ_EXT_CHAINED) {
			snd_seq_event_cell_t *curp, *nextptr;
			curp = cell->event.data.ext.ptr;
			for (; curp; curp = nextptr) {
				nextptr = curp->next;
				curp->next = pool->free;
				free_cell(pool, curp);
			}
		}
	}
	if (waitqueue_active(&pool->output_sleep)) {
		/* has enough space now? */
		if (snd_seq_output_ok(pool))
			wake_up(&pool->output_sleep);
	}
	spin_unlock_irqrestore(&pool->lock, flags);
}


/*
 * allocate an event cell.
 */
int snd_seq_cell_alloc(pool_t *pool, snd_seq_event_cell_t **cellp, int nonblock, struct file *file)
{
	snd_seq_event_cell_t *cell;
	unsigned long flags;
	int err = -EAGAIN;

	if (pool == NULL)
		return -EINVAL;

	*cellp = NULL;

	spin_lock_irqsave(&pool->lock, flags);
	if (pool->ptr == NULL) {	/* not initialized */
		snd_printd("seq: pool is not initialized\n");
		err = -EINVAL;
		goto __error;
	}
	while (pool->free == NULL && ! nonblock && ! pool->closing) {
		/* change semaphore to allow other clients
		   to access device file */
		if (file)
			up(&semaphore_of(file));

		snd_seq_sleep_in_lock(&pool->output_sleep, &pool->lock);

		/* restore semaphore again */
		if (file)
			down(&semaphore_of(file));

		/* interrupted? */
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			goto __error;
		}
	}
	if (pool->closing) { /* closing.. */
		err = -ENOMEM;
		goto __error;
	}

	cell = pool->free;
	if (cell) {
		int used;
		pool->free = cell->next;
		atomic_inc(&pool->counter);
		used = atomic_read(&pool->counter);
		if (pool->max_used < used)
			pool->max_used = used;
		pool->event_alloc_success++;
		/* clear cell pointers */
		cell->next = NULL;
		err = 0;
	} else
		pool->event_alloc_failures++;
	*cellp = cell;

__error:
	spin_unlock_irqrestore(&pool->lock, flags);
	return err;
}


/*
 * duplicate the event to a cell.
 * if the event has external data, the data is decomposed to additional
 * cells.
 */
int snd_seq_event_dup(pool_t *pool, snd_seq_event_t *event, snd_seq_event_cell_t **cellp, int nonblock, struct file *file)
{
	int ncells, err;
	unsigned int extlen;
	snd_seq_event_cell_t *cell;

	*cellp = NULL;

	ncells = 0;
	extlen = 0;
	if (snd_seq_ev_is_variable(event)) {
		extlen = event->data.ext.len & ~SNDRV_SEQ_EXT_MASK;
		ncells = (extlen + sizeof(snd_seq_event_t) - 1) / sizeof(snd_seq_event_t);
	}
	if (ncells >= pool->total_elements)
		return -ENOMEM;

	err = snd_seq_cell_alloc(pool, &cell, nonblock, file);
	if (err < 0)
		return err;

	/* copy the event */
	cell->event = *event;

	/* decompose */
	if (snd_seq_ev_is_variable(event)) {
		int len = extlen;
		int is_chained = event->data.ext.len & SNDRV_SEQ_EXT_CHAINED;
		int is_usrptr = event->data.ext.len & SNDRV_SEQ_EXT_USRPTR;
		snd_seq_event_cell_t *src, *tmp, *tail;
		char *buf;

		cell->event.data.ext.len = extlen | SNDRV_SEQ_EXT_CHAINED;
		cell->event.data.ext.ptr = NULL;

		src = (snd_seq_event_cell_t*)event->data.ext.ptr;
		buf = (char *)event->data.ext.ptr;
		tail = NULL;

		while (ncells-- > 0) {
			int size = sizeof(snd_seq_event_t);
			if (len < size)
				size = len;
			err = snd_seq_cell_alloc(pool, &tmp, nonblock, file);
			if (err < 0)
				goto __error;
			if (cell->event.data.ext.ptr == NULL)
				cell->event.data.ext.ptr = tmp;
			if (tail)
				tail->next = tmp;
			tail = tmp;
			/* copy chunk */
			if (is_chained && src) {
				tmp->event = src->event;
				src = src->next;
			} else if (is_usrptr) {
				if (copy_from_user(&tmp->event, buf, size)) {
					err = -EFAULT;
					goto __error;
				}
			} else {
				memcpy(&tmp->event, buf, size);
			}
			buf += size;
			len -= size;
		}
	}

	*cellp = cell;
	return 0;

__error:
	snd_seq_cell_free(cell);
	return err;
}
  

/* poll wait */
int snd_seq_pool_poll_wait(pool_t *pool, struct file *file, poll_table *wait)
{
	poll_wait(file, &pool->output_sleep, wait);
	return snd_seq_output_ok(pool);
}


/* return number of unused (free) cells */
int snd_seq_unused_cells(pool_t *pool)
{
	if (pool == NULL)
		return 0;
	return (pool->total_elements - atomic_read(&pool->counter));
}


/* return total number of allocated cells */
int snd_seq_total_cells(pool_t *pool)
{
	if (pool == NULL)
		return 0;
	return (pool->total_elements);
}

/* allocate event chunk */
static snd_seq_event_chunk_t *snd_seq_chunk_allocate(int numcells)
{
	snd_seq_event_chunk_t *chunkptr;

	chunkptr = snd_kcalloc(sizeof(snd_seq_event_chunk_t) +
			       sizeof(snd_seq_event_cell_t) * numcells, GFP_KERNEL);
	if (chunkptr)
		chunkptr->cells = numcells;
	return chunkptr;
}

/* allocate room specified number of events */
int snd_seq_pool_init(pool_t *pool)
{
	int chunks_req, chunks_got;
	int last_chunk_cells;
	snd_seq_event_chunk_t *chunkptr;

	int cell;
	snd_seq_event_cell_t *ptr, *cellptr;
	unsigned long flags;
	int events_req, events_got;

	snd_assert(pool != NULL, return -EINVAL);
	if (pool->ptr)			/* should be atomic? */
		return 0;

	events_req = pool->size;

	chunks_req = events_req / SNDRV_SEQ_DEFAULT_CHUNK_EVENTS;
	last_chunk_cells = events_req % SNDRV_SEQ_DEFAULT_CHUNK_EVENTS;

	chunkptr = NULL;
	chunks_got = 0;

	/* allocate memory */
	if (last_chunk_cells) {
		chunkptr = snd_seq_chunk_allocate(last_chunk_cells);
		if (chunkptr) {
			pool->ptr = chunkptr;
		} else {
			goto __nomem_out;
		}
	}
	for (; chunks_got < chunks_req; chunks_got++) {
		chunkptr = snd_seq_chunk_allocate(SNDRV_SEQ_DEFAULT_CHUNK_EVENTS);
		if (chunkptr) {
			chunkptr->next = pool->ptr;
			pool->ptr = chunkptr;
		} else {
		  	break;
		}	  
	}
__nomem_out:
	if (pool->ptr == NULL) {
		snd_printd("seq: malloc for sequencer events failed\n");
		return -ENOMEM;
	}

	events_got = chunks_got * SNDRV_SEQ_DEFAULT_CHUNK_EVENTS + last_chunk_cells;
	if (events_got != events_req) {
		snd_printk("seq: requested %d events, got %d\n", events_req, events_got);
	}
	/* add new cells to the free cell list */
	spin_lock_irqsave(&pool->lock, flags);
	pool->free = NULL;

	chunkptr = pool->ptr;
	while (chunkptr) {
		ptr = (snd_seq_event_cell_t *)((char *)chunkptr + sizeof(snd_seq_event_chunk_t));
		for (cell = 0; cell < chunkptr->cells; cell++) {
			cellptr = &ptr[cell];
			cellptr->pool = pool;
			cellptr->next = pool->free;
			pool->free = cellptr;
		}
		chunkptr = chunkptr->next;
	}
	pool->size = events_got;
	pool->room = (events_got + 1) / 2;

	/* init statistics */
	pool->max_used = 0;
	pool->total_elements = events_got;
	spin_unlock_irqrestore(&pool->lock, flags);
	return 0;
}

/* remove events */
int snd_seq_pool_done(pool_t *pool)
{
	unsigned long flags;
	snd_seq_event_chunk_t *ptr, *_ptr;	
	int max_count = 5 * HZ;

	snd_assert(pool != NULL, return -EINVAL);

	/* wait for closing all threads */
	spin_lock_irqsave(&pool->lock, flags);
	pool->closing = 1;
	spin_unlock_irqrestore(&pool->lock, flags);

	if (waitqueue_active(&pool->output_sleep))
		wake_up(&pool->output_sleep);

	while (atomic_read(&pool->counter) > 0) {
		if (max_count == 0) {
			snd_printk("snd_seq_pool_done timeout: %d cells remain\n", atomic_read(&pool->counter));
			break;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
		max_count--;
	}
	
	/* release all resources */
	spin_lock_irqsave(&pool->lock, flags);
	ptr = pool->ptr;
	pool->ptr = NULL;
	pool->free = NULL;
	pool->total_elements = 0;
	spin_unlock_irqrestore(&pool->lock, flags);

	while (ptr) {
		_ptr = ptr->next;
		kfree(ptr);
		ptr = _ptr;
	}

	spin_lock_irqsave(&pool->lock, flags);
	pool->closing = 0;
	spin_unlock_irqrestore(&pool->lock, flags);

	return 0;
}


/* init new memory pool */
pool_t *snd_seq_pool_new(int poolsize)
{
	pool_t *pool;

	/* create pool block */
	pool = snd_kcalloc(sizeof(pool_t), GFP_KERNEL);
	if (pool == NULL) {
		snd_printd("seq: malloc failed for pool\n");
		return NULL;
	}
	spin_lock_init(&pool->lock);
	pool->ptr = NULL;
	pool->free = NULL;
	pool->total_elements = 0;
	atomic_set(&pool->counter, 0);
	pool->closing = 0;
	init_waitqueue_head(&pool->output_sleep);
	
	pool->size = poolsize;

	/* init statistics */
	pool->max_used = 0;
	return pool;
}

/* remove memory pool */
int snd_seq_pool_delete(pool_t **ppool)
{
	pool_t *pool = *ppool;

	*ppool = NULL;
	if (pool == NULL)
		return 0;
	snd_seq_pool_done(pool);
	kfree(pool);
	return 0;
}

/* initialize sequencer memory */
int __init snd_sequencer_memory_init(void)
{
	return 0;
}

/* release sequencer memory */
void __exit snd_sequencer_memory_done(void)
{
}


/* exported to seq_clientmgr.c */
void snd_seq_info_pool(snd_info_buffer_t * buffer, pool_t *pool, char *space)
{
	if (pool == NULL)
		return;
	snd_iprintf(buffer, "%sPool size          : %d\n", space, pool->total_elements);
	snd_iprintf(buffer, "%sCells in use       : %d\n", space, atomic_read(&pool->counter));
	snd_iprintf(buffer, "%sPeak cells in use  : %d\n", space, pool->max_used);
	snd_iprintf(buffer, "%sAlloc success      : %d\n", space, pool->event_alloc_success);
	snd_iprintf(buffer, "%sAlloc failures     : %d\n", space, pool->event_alloc_failures);
}

/* exported to seq_info.c */
void snd_seq_info_memory_read(snd_info_entry_t *entry, 
			      snd_info_buffer_t * buffer)
{
}
