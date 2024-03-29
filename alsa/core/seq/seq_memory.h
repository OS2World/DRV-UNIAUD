/*
 *  ALSA sequencer Memory Manager
 *  Copyright (c) 1998 by Frank van de Pol <fvdpol@home.nl>
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
#ifndef __SND_SEQ_MEMORYMGR_H
#define __SND_SEQ_MEMORYMGR_H

#include <sound/seq_kernel.h>

typedef struct pool pool_t;

/* container for sequencer event (internal use) */
typedef struct snd_seq_event_cell_t {
	snd_seq_event_t event;
	pool_t *pool;				/* used pool */
	struct snd_seq_event_cell_t *next;	/* next cell */
} snd_seq_event_cell_t;

/* chunk of sequencer events (internal use) */
typedef struct snd_seq_event_chunk_t {
	int cells;				/* cells in chunk */
	struct snd_seq_event_chunk_t *next;	/* next chunk */
} snd_seq_event_chunk_t;

/* design note: the pool is a contigious block of memory, if we dynamicly
   want to add additional cells to the pool be better store this in another
   pool as we need to know the base address of the pool when releasing
   memory. */

struct pool {
	snd_seq_event_chunk_t *ptr;	/* pointer to first event chunk */
	snd_seq_event_cell_t *free;	/* pointer to the head of the free list */

	int total_elements;	/* pool size actually allocated */
	atomic_t counter;	/* cells free */

	int size;		/* pool size to be allocated */
	int room;		/* watermark for sleep/wakeup */

	int closing;

	/* statistics */
	int max_used;
	int event_alloc_nopool;
	int event_alloc_failures;
	int event_alloc_success;

	/* Write locking */
	wait_queue_head_t output_sleep;

	/* Pool lock */
	spinlock_t lock;
};

extern void snd_seq_cell_free(snd_seq_event_cell_t* cell);
int snd_seq_cell_alloc(pool_t *pool, snd_seq_event_cell_t **cellp, int nonblock, struct file *file);

int snd_seq_event_dup(pool_t *pool, snd_seq_event_t *event, snd_seq_event_cell_t **cellp, int nonblock, struct file *file);

/* return number of unused (free) cells */
extern int snd_seq_unused_cells(pool_t *pool);

/* return total number of allocated cells */
extern int snd_seq_total_cells(pool_t *pool);

/* init pool - allocate events */
extern int snd_seq_pool_init(pool_t *pool);

/* done pool - free events */
extern int snd_seq_pool_done(pool_t *pool);

/* create pool */
extern pool_t *snd_seq_pool_new(int poolsize);

/* remove pool */
extern int snd_seq_pool_delete(pool_t **pool);

/* init memory */
extern int snd_sequencer_memory_init(void);
            
/* release event memory */
extern void snd_sequencer_memory_done(void);

/* polling */
extern int snd_seq_pool_poll_wait(pool_t *pool, struct file *file, poll_table *wait);


#endif
