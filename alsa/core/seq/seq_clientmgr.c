/*
 *  ALSA sequencer Client Manager
 *  Copyright (c) 1998-2001 by Frank van de Pol <fvdpol@home.nl>
 *                             Jaroslav Kysela <perex@suse.cz>
 *                             Takashi Iwai <tiwai@suse.de>
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
#include <sound/minors.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include <sound/seq_kernel.h>
#include "seq_clientmgr.h"
#include "seq_memory.h"
#include "seq_queue.h"
#include "seq_timer.h"
#include "seq_info.h"
#include "seq_system.h"
#include <sound/seq_device.h>

/* Client Manager

 * this module handles the connections of userland and kernel clients
 * 
 */

#define SNDRV_SEQ_LFLG_INPUT	0x0001
#define SNDRV_SEQ_LFLG_OUTPUT	0x0002
#define SNDRV_SEQ_LFLG_OPEN	(SNDRV_SEQ_LFLG_INPUT|SNDRV_SEQ_LFLG_OUTPUT)

static spinlock_t clients_lock = SPIN_LOCK_UNLOCKED;
static DECLARE_MUTEX(register_mutex);

/*
 * client table
 */
static char clienttablock[SNDRV_SEQ_MAX_CLIENTS];
static client_t *clienttab[SNDRV_SEQ_MAX_CLIENTS];
static usage_t client_usage = {0, 0};

/*
 * prototypes
 */
static int bounce_error_event(client_t *client, snd_seq_event_t *event, int err, int atomic, int hop);
static int snd_seq_deliver_single_event(client_t *client, snd_seq_event_t *event, int filter, int atomic, int hop);


/*
 */
static inline unsigned short snd_seq_file_flags(struct file *file)
{
        switch (file->f_mode & (FMODE_READ | FMODE_WRITE)) {
        case FMODE_WRITE:
                return SNDRV_SEQ_LFLG_OUTPUT;
        case FMODE_READ:
                return SNDRV_SEQ_LFLG_INPUT;
        default:
                return SNDRV_SEQ_LFLG_OPEN;
        }
}

static inline int snd_seq_write_pool_allocated(client_t *client)
{
	return snd_seq_total_cells(client->pool) > 0;
}

/* return pointer to client structure for specified id */
static client_t *clientptr(int clientid)
{
	if (clientid < 0 || clientid >= SNDRV_SEQ_MAX_CLIENTS) {
		snd_printd("Seq: oops. Trying to get pointer to client %d\n", clientid);
		return NULL;
	}
	return clienttab[clientid];
}

extern int snd_seq_client_load[];

client_t *snd_seq_client_use_ptr(int clientid)
{
	unsigned long flags;
	client_t *client;

	if (clientid < 0 || clientid >= SNDRV_SEQ_MAX_CLIENTS) {
		snd_printd("Seq: oops. Trying to get pointer to client %d\n", clientid);
		return NULL;
	}
	spin_lock_irqsave(&clients_lock, flags);
	client = clientptr(clientid);
	if (client)
		goto __lock;
	if (clienttablock[clientid]) {
		spin_unlock_irqrestore(&clients_lock, flags);
		return NULL;
	}
	spin_unlock_irqrestore(&clients_lock, flags);
#ifdef CONFIG_KMOD
	if (!in_interrupt()) {
		if (clientid < 64) {
			int idx;
			char name[32];
			
			for (idx = 0; idx < 64; idx++) {
				if (snd_seq_client_load[idx] < 0)
					break;
				if (snd_seq_client_load[idx] == clientid) {
					sprintf(name, "snd-seq-client-%i", clientid);
					request_module(name);
					break;
				}
			}
		} else if (clientid >= 64 && clientid < 128) {
			int card = (clientid - 64) / 8;
			if (card < snd_ecards_limit) {
#ifndef MODULE
				if (current->fs->root) {
#endif
					snd_request_card(card);
					snd_seq_device_load_drivers();
#ifndef MODULE
				}
#endif
			}
		}
		spin_lock_irqsave(&clients_lock, flags);
		client = clientptr(clientid);
		if (client)
			goto __lock;
		spin_unlock_irqrestore(&clients_lock, flags);
	}
#endif
	return NULL;

      __lock:
	snd_use_lock_use(&client->use_lock);
	spin_unlock_irqrestore(&clients_lock, flags);
	return client;
}

static void usage_alloc(usage_t * res, int num)
{
	res->cur += num;
	if (res->cur > res->peak)
		res->peak = res->cur;
}

static void usage_free(usage_t * res, int num)
{
	res->cur -= num;
}

/* initialise data structures */
int __init client_init_data(void)
{
	/* zap out the client table */
	memset(&clienttablock, 0, sizeof(clienttablock));
	memset(&clienttab, 0, sizeof(clienttab));
	return 0;
}


static client_t *seq_create_client1(int client_index, int poolsize)
{
	unsigned long flags;
	int c;
	client_t *client;

	/* init client data */
	client = snd_kcalloc(sizeof(client_t), GFP_KERNEL);
	if (client == NULL)
		return NULL;
	client->pool = snd_seq_pool_new(poolsize);
	if (client->pool == NULL) {
		kfree(client);
		return NULL;
	}
	client->type = NO_CLIENT;
	snd_use_lock_init(&client->use_lock);
	rwlock_init(&client->ports_lock);

	/* find free slot in the client table */
	spin_lock_irqsave(&clients_lock, flags);
	if (client_index < 0) {
		for (c = 128; c < SNDRV_SEQ_MAX_CLIENTS; c++) {
			if (clienttab[c] || clienttablock[c])
				continue;
			clienttab[client->number = c] = client;
			spin_unlock_irqrestore(&clients_lock, flags);
			return client;
		}
	} else {
		if (clienttab[client_index] == NULL && !clienttablock[client_index]) {
			clienttab[client->number = client_index] = client;
			spin_unlock_irqrestore(&clients_lock, flags);
			return client;
		}
	}
	spin_unlock_irqrestore(&clients_lock, flags);
	snd_seq_pool_done(client->pool);
	snd_seq_pool_delete(&client->pool);
	kfree(client);
	return NULL;	/* no free slot found or busy, return failure code */
}


static int seq_free_client1(client_t *client)
{
	unsigned long flags;

	snd_assert(client != NULL, return -EINVAL);
	snd_seq_delete_ports(client);
	snd_seq_queue_client_leave(client->number);
	spin_lock_irqsave(&clients_lock, flags);
	clienttablock[client->number] = 1;
	clienttab[client->number] = NULL;
	spin_unlock_irqrestore(&clients_lock, flags);
	snd_use_lock_sync(&client->use_lock);
	snd_seq_queue_client_termination(client->number);
	if (client->pool) {
		snd_seq_pool_done(client->pool);
		snd_seq_pool_delete(&client->pool);
	}
	spin_lock_irqsave(&clients_lock, flags);
	clienttablock[client->number] = 0;
	spin_unlock_irqrestore(&clients_lock, flags);
	return 0;
}


static void seq_free_client(client_t * client)
{
	if (client == NULL)
		return;

	down(&register_mutex);
	switch (client->type) {
	case NO_CLIENT:
		snd_printk("Seq: Trying to free unused client %d\n", client->number);
		break;
	case USER_CLIENT:
	case KERNEL_CLIENT:
		seq_free_client1(client);
		usage_free(&client_usage, 1);
		break;

	default:
		snd_printk("Seq: Trying to free client %d with undefined type = %d\n", client->number, client->type);
	}
	up(&register_mutex);

	snd_seq_system_client_ev_client_exit(client->number);
}



/* -------------------------------------------------------- */

/* create a user client */
static int snd_seq_open(struct inode *inode, struct file *file)
{
	int c, mode;			/* client id */
	client_t *client;
	user_client_t *user;

	if (down_interruptible(&register_mutex))
		return -ERESTARTSYS;
	client = seq_create_client1(-1, SNDRV_SEQ_DEFAULT_EVENTS);
	if (client == NULL) {
		up(&register_mutex);
		return -ENOMEM;	/* failure code */
	}

	mode = snd_seq_file_flags(file);
	if (mode & SNDRV_SEQ_LFLG_INPUT)
		client->accept_input = 1;
	if (mode & SNDRV_SEQ_LFLG_OUTPUT)
		client->accept_output = 1;

	user = &client->data.user;
	user->fifo = NULL;
	user->fifo_pool_size = 0;

	if (mode & SNDRV_SEQ_LFLG_INPUT) {
		user->fifo_pool_size = SNDRV_SEQ_DEFAULT_CLIENT_EVENTS;
		user->fifo = snd_seq_fifo_new(user->fifo_pool_size);
		if (user->fifo == NULL) {
			seq_free_client1(client);
			kfree(client);
			up(&register_mutex);
			return -ENOMEM;
		}
	}

	usage_alloc(&client_usage, 1);
	client->type = USER_CLIENT;
	up(&register_mutex);

	c = client->number;
#ifdef TARGET_OS2
	file->private_data = client;
#else
	(user_client_t *) file->private_data = client;
#endif
	/* fill client data */
	user->file = file;
	sprintf(client->name, "Client-%d", c);

	/* make others aware this new client */
	snd_seq_system_client_ev_client_start(c);

#ifndef LINUX_2_3
	MOD_INC_USE_COUNT;
#endif

	return 0;
}

/* delete a user client */
static int snd_seq_release(struct inode *inode, struct file *file)
{
	client_t *client = (client_t *) file->private_data;

	if (client) {
		seq_free_client(client);
		if (client->data.user.fifo)
			snd_seq_fifo_delete(&client->data.user.fifo);
		kfree(client);
	}

#ifndef LINUX_2_3
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}


/* handle client read() */
/* possible error values:
 *	-ENXIO	invalid client or file open mode
 *	-ENOSPC	FIFO overflow (the flag is cleared after this error report)
 *	-EINVAL	no enough user-space buffer to write the whole event
 *	-EFAULT	seg. fault during copy to user space
 */
static ssize_t snd_seq_read(struct file *file, char *buf, size_t count, loff_t *offset)
{
	client_t *client = (client_t *) file->private_data;
	fifo_t *fifo;
	int err;
	long result = 0;
	snd_seq_event_cell_t *cell;

	if (!(snd_seq_file_flags(file) & SNDRV_SEQ_LFLG_INPUT))
		return -ENXIO;

	if (verify_area(VERIFY_WRITE, buf, count))
		return -EFAULT;

	/* check client structures are in place */
	snd_assert(client != NULL, return -ENXIO);

	if (!client->accept_input || (fifo = client->data.user.fifo) == NULL)
		return -ENXIO;

	if (atomic_read(&fifo->overflow) > 0) {
		/* buffer overflow is detected */
		snd_seq_fifo_clear(fifo);
		/* return error code */
		return -ENOSPC;
	}

	cell = NULL;
	err = 0;
	snd_seq_fifo_lock(fifo);

	/* while data available in queue */
	while (count >= sizeof(snd_seq_event_t)) {
		int nonblock;

		nonblock = (file->f_flags & O_NONBLOCK) || result > 0;
		if ((err = snd_seq_fifo_cell_out(fifo, &cell, nonblock)) < 0) {
			break;
		}
		if (snd_seq_ev_is_variable(&cell->event)) {
			snd_seq_event_t tmpev;
			tmpev = cell->event;
			tmpev.data.ext.len &= ~SNDRV_SEQ_EXT_MASK;
			if (copy_to_user(buf, &tmpev, sizeof(snd_seq_event_t))) {
				err = -EFAULT;
				break;
			}
			count -= sizeof(snd_seq_event_t);
			buf += sizeof(snd_seq_event_t);
			err = snd_seq_expand_var_event(&cell->event, count, buf, 0, sizeof(snd_seq_event_t));
			if (err < 0)
				break;
			result += err;
			count -= err;
			buf += err;
		} else {
			copy_to_user(buf, &cell->event, sizeof(snd_seq_event_t));
			count -= sizeof(snd_seq_event_t);
			buf += sizeof(snd_seq_event_t);
		}
		snd_seq_cell_free(cell);
		cell = NULL; /* to be sure */
		result += sizeof(snd_seq_event_t);
	}

	if (err < 0) {
		if (cell)
			snd_seq_fifo_cell_putback(fifo, cell);
		if (err == -EAGAIN && result > 0)
			err = 0;
	}
	snd_seq_fifo_unlock(fifo);

	return (err < 0) ? err : result;
}


/*
 * check access permission to the port
 */
static int check_port_perm(client_port_t *port, unsigned int flags)
{
	if ((port->capability & flags) != flags)
		return 0;
	return flags;
}

/*
 * check if the destination client is available, and return the pointer
 * if filter is non-zero, client filter bitmap is tested.
 */
static client_t *get_event_dest_client(snd_seq_event_t *event, int filter)
{
	client_t *dest;

	dest = snd_seq_client_use_ptr(event->dest.client);
	if (dest == NULL)
		return NULL;
	if (! dest->accept_input)
		goto __not_avail;
	if ((dest->filter & SNDRV_SEQ_FILTER_USE_EVENT) &&
	    ! test_bit(event->type, &dest->event_filter))
		goto __not_avail;
	if (filter && !(dest->filter & filter))
		goto __not_avail;

	return dest; /* ok - accessible */
__not_avail:
	snd_seq_client_unlock(dest);
	return NULL;
}


/*
 * Return the error event.
 *
 * If the receiver client is a user client, the original event is
 * encapsulated in SNDRV_SEQ_EVENT_BOUNCE as variable length event.  If
 * the original event is also variable length, the external data is
 * copied after the event record. 
 * If the receiver client is a kernel client, the original event is
 * quoted in SNDRV_SEQ_EVENT_KERNEL_ERROR, since this requires no extra
 * kmalloc.
 */
static int bounce_error_event(client_t *client, snd_seq_event_t *event,
			      int err, int atomic, int hop)
{
	snd_seq_event_t bounce_ev;
	int result;

	if (client == NULL ||
	    ! (client->filter & SNDRV_SEQ_FILTER_BOUNCE) ||
	    ! client->accept_input)
		return 0; /* ignored */

	/* set up quoted error */
	memset(&bounce_ev, 0, sizeof(bounce_ev));
	bounce_ev.type = SNDRV_SEQ_EVENT_KERNEL_ERROR;
	bounce_ev.flags = SNDRV_SEQ_EVENT_LENGTH_FIXED;
	bounce_ev.queue = SNDRV_SEQ_QUEUE_DIRECT;
	bounce_ev.source.client = SNDRV_SEQ_CLIENT_SYSTEM;
	bounce_ev.source.port = SNDRV_SEQ_PORT_SYSTEM_ANNOUNCE;
	bounce_ev.dest.client = client->number;
	bounce_ev.dest.port = event->source.port;
	bounce_ev.data.quote.origin = event->dest;
	bounce_ev.data.quote.event = event;
	bounce_ev.data.quote.value = -err; /* use positive value */
	result = snd_seq_deliver_single_event(NULL, &bounce_ev, 0, atomic, hop + 1);
	if (result < 0) {
		client->event_lost++;
		return result;
	}

	return result;
}


/*
 * deliver an event to the specified destination.
 * if filter is non-zero, client filter bitmap is tested.
 *
 *  RETURN VALUE: 0 : if succeeded
 *		 <0 : error
 */
static int snd_seq_deliver_single_event(client_t *client,
					snd_seq_event_t *event,
					int filter, int atomic, int hop)
{
	client_t *dest = NULL;
	client_port_t *dest_port = NULL;
	int result = -ENOENT;
	int direct, quoted = 0;

	dest = get_event_dest_client(event, filter);
	if (dest == NULL)
		goto __skip;
	dest_port = snd_seq_port_use_ptr(dest, event->dest.port);
	if (dest_port == NULL)
		goto __skip;

	/* check permission */
	if (! check_port_perm(dest_port, SNDRV_SEQ_PORT_CAP_WRITE)) {
		result = -EPERM;
		goto __skip;
	}
		
	direct = snd_seq_ev_is_direct(event);

	/* expand the quoted event */
	if (event->type == SNDRV_SEQ_EVENT_KERNEL_QUOTE) {
		quoted = 1;
		event = event->data.quote.event;
		if (event == NULL) {
			snd_printd("seq: quoted event is NULL\n");
			result = 0; /* do not send bounce error */
			goto __skip;
		}
	}

	switch (dest->type) {
	case USER_CLIENT:
		if (dest->data.user.fifo)
			result = snd_seq_fifo_event_in(dest->data.user.fifo, event);
		break;

	case KERNEL_CLIENT:
		if (dest_port->event_input == NULL)
			break;
		result = dest_port->event_input(event, direct, dest_port->private_data, atomic, hop);
		break;
	default:
		break;
	}

  __skip:
	if (dest_port)
		snd_seq_ports_unlock(dest);
	if (dest)
		snd_seq_client_unlock(dest);

	if (result < 0) {
		if (quoted) {
			/* return directly to the original source */
			dest = snd_seq_client_use_ptr(event->source.client);
			result = bounce_error_event(dest, event, result, atomic, hop);
			snd_seq_client_unlock(dest);
		} else {
			result = bounce_error_event(client, event, result, atomic, hop);
		}
	}
	return result;
}


/*
 * send the event to all subscribers:
 */
static int deliver_to_subscribers(client_t *client,
				  snd_seq_event_t *event,
				  int atomic, int hop)
{
	subscribers_t *subs;
	int err = 0, num_ev = 0;
	snd_seq_event_t event_saved;
	client_port_t *src_port;

	src_port = snd_seq_port_use_ptr(client, event->source.port);
	if (src_port == NULL)
		return -EINVAL; /* invalid source port */
	if (src_port->export.list == NULL) {
		snd_seq_ports_unlock(client);
		return 0; /* no subscription - skip */
	}

	/* save original event record */
	event_saved = *event;

	snd_seq_subscribers_lock(&src_port->export);
	for (subs = src_port->export.list; subs; subs = subs->next) {
		event->dest = subs->addr;
		if (subs->info.flags & SNDRV_SEQ_PORT_SUBS_TIMESTAMP) {
			/* convert time according to flag with subscription */
			queue_t *q;
			q = queueptr(subs->info.queue);
			if (q) {
				event->queue = subs->info.queue;
				event->flags &= ~SNDRV_SEQ_TIME_STAMP_MASK;
				if (subs->info.flags & SNDRV_SEQ_PORT_SUBS_TIME_REAL) {
					event->time.time = snd_seq_timer_get_cur_time(q->timer);
					event->flags |= SNDRV_SEQ_TIME_STAMP_REAL;
				} else {
					event->time.tick = snd_seq_timer_get_cur_tick(q->timer);
					event->flags |= SNDRV_SEQ_TIME_STAMP_TICK;
				}
				queuefree(q);
			}
		}
		err = snd_seq_deliver_single_event(client, event,
						   0, atomic, hop);
		if (err < 0)
			break;
		num_ev++;
		if (subs->info.flags & SNDRV_SEQ_PORT_SUBS_TIMESTAMP)
			/* restore original event record */
			*event = event_saved;
	}
	snd_seq_subscribers_unlock(&src_port->export);
	*event = event_saved; /* restore */
	snd_seq_ports_unlock(client);
	return (err < 0) ? err : num_ev;
}

/*
 * broadcast to all ports:
 */
static int port_broadcast_event(client_t *client,
				snd_seq_event_t *event,
				int atomic, int hop)
{
	int num_ev = 0, err = 0;
	client_t *dest_client;
	client_port_t *p;

	dest_client = get_event_dest_client(event, SNDRV_SEQ_FILTER_BROADCAST);
	if (dest_client == NULL)
		return 0; /* no matching destination */

	snd_seq_ports_lock(dest_client);
	for (p = dest_client->ports; p; p = p->next) {
		event->dest.port = p->port;
		/* pass NULL as source client to avoid error bounce */
		err = snd_seq_deliver_single_event(NULL, event,
						   SNDRV_SEQ_FILTER_BROADCAST,
						   atomic, hop);
		if (err < 0)
			break;
		num_ev++;
	}
	snd_seq_ports_unlock(dest_client);
	snd_seq_client_unlock(dest_client);
	event->dest.port = SNDRV_SEQ_ADDRESS_BROADCAST; /* restore */
	return (err < 0) ? err : num_ev;
}

/*
 * send the event to all clients:
 * if destination port is also ADDRESS_BROADCAST, deliver to all ports.
 */
static int broadcast_event(client_t *client,
			   snd_seq_event_t *event, int atomic, int hop)
{
	int err = 0, num_ev = 0;
	int dest;
	snd_seq_addr_t addr;

	addr = event->dest; /* save */

	for (dest = 0; dest < SNDRV_SEQ_MAX_CLIENTS; dest++) {
		/* don't send to itself */
		if (dest == client->number)
			continue;
		event->dest.client = dest;
		event->dest.port = addr.port;
		if (addr.port == SNDRV_SEQ_ADDRESS_BROADCAST)
			err = port_broadcast_event(client, event, atomic, hop);
		else
			/* pass NULL as source client to avoid error bounce */
			err = snd_seq_deliver_single_event(NULL, event,
							   SNDRV_SEQ_FILTER_BROADCAST,
							   atomic, hop);
		if (err < 0)
			break;
		num_ev += err;
	}
	event->dest = addr; /* restore */
	return (err < 0) ? err : num_ev;
}


/* multicast - not supported yet */
static int multicast_event(client_t *client, snd_seq_event_t *event,
			   int atomic, int hop)
{
	snd_printd("seq: multicast not supported yet.\n");
	return 0; /* ignored */
}


/* deliver an event to the destination port(s).
 * if the event is to subscribers or broadcast, the event is dispatched
 * to multiple targets.
 *
 * RETURN VALUE: n > 0  : the number of delivered events.
 *               n == 0 : the event was not passed to any client.
 *               n < 0  : error - event was not processed.
 */
int snd_seq_deliver_event(client_t *client, snd_seq_event_t *event,
			  int atomic, int hop)
{
	int result;

	hop++;
	if (hop >= SNDRV_SEQ_MAX_HOPS) {
		snd_printd("too long delivery path (%d:%d->%d:%d)\n",
			   event->source.client, event->source.port,
			   event->dest.client, event->dest.port);
		return -EMLINK;
	}

	if (event->queue == SNDRV_SEQ_ADDRESS_SUBSCRIBERS ||
	    event->dest.client == SNDRV_SEQ_ADDRESS_SUBSCRIBERS)
		result = deliver_to_subscribers(client, event, atomic, hop);

	else if (event->queue == SNDRV_SEQ_ADDRESS_BROADCAST ||
		 event->dest.client == SNDRV_SEQ_ADDRESS_BROADCAST)
		result = broadcast_event(client, event, atomic, hop);
	
	else if (event->dest.client >= SNDRV_SEQ_MAX_CLIENTS)
		result = multicast_event(client, event, atomic, hop);

	else if (event->dest.port == SNDRV_SEQ_ADDRESS_BROADCAST)
		result = port_broadcast_event(client, event, atomic, hop);

	else
		result = snd_seq_deliver_single_event(client, event, 0, atomic, hop);

	return result;
}

/*
 * dispatch an event cell:
 * This function is called only from queue check routines in timer
 * interrupts or after enqueued.
 * The event cell shall be released or re-queued in this function.
 *
 * RETURN VALUE: n > 0  : the number of delivered events.
 *		 n == 0 : the event was not passed to any client.
 *		 n < 0  : error - event was not processed.
 */
int snd_seq_dispatch_event(snd_seq_event_cell_t *cell, int atomic, int hop)
{
	client_t *client;
	int result;

	snd_assert(cell != NULL, return -EINVAL);

	client = snd_seq_client_use_ptr(cell->event.source.client);
	if (client == NULL) {
		snd_seq_cell_free(cell); /* release this cell */
		return -EINVAL;
	}

	if (cell->event.type == SNDRV_SEQ_EVENT_NOTE) {
		/* NOTE event:
		 * the event cell is re-used as a NOTE-OFF event and
		 * enqueued again.
		 */
		snd_seq_event_t tmpev, *ev;

		/* reserve this event to enqueue note-off later */
		tmpev = cell->event;
		tmpev.type = SNDRV_SEQ_EVENT_NOTEON;
		result = snd_seq_deliver_event(client, &tmpev, atomic, hop);

		/*
		 * This was originally a note event.  We now re-use the
		 * cell for the note-off event.
		 */

		ev = &cell->event;
		ev->type = SNDRV_SEQ_EVENT_NOTEOFF;
		ev->flags |= SNDRV_SEQ_PRIORITY_HIGH;

		/* add the duration time */
		switch (ev->flags & SNDRV_SEQ_TIME_STAMP_MASK) {
		case SNDRV_SEQ_TIME_STAMP_TICK:
			ev->time.tick += ev->data.note.duration;
			break;
		case SNDRV_SEQ_TIME_STAMP_REAL:
			/* unit for duration is ms */
			ev->time.time.tv_nsec += 1000000 * (ev->data.note.duration % 1000);
			ev->time.time.tv_sec += ev->data.note.duration / 1000 +
						ev->time.time.tv_nsec / 1000000000;
			ev->time.time.tv_nsec %= 1000000000;
			break;
		}
		ev->data.note.velocity = ev->data.note.off_velocity;

		/* Now queue this cell as the note off event */
		if (snd_seq_enqueue_event(cell, atomic, hop) < 0)
			snd_seq_cell_free(cell); /* release this cell */

	} else {
		/* Normal events:
		 * event cell is freed after processing the event
		 */

		result = snd_seq_deliver_event(client, &cell->event, atomic, hop);
		snd_seq_cell_free(cell);
	}

	snd_seq_client_unlock(client);
	return result;
}


/* Allocate a cell from client pool and enqueue it to queue:
 * if pool is empty and blocking is TRUE, sleep until a new cell is
 * available.
 */
static int snd_seq_client_enqueue_event(client_t *client,
					snd_seq_event_t *event,
					struct file *file, int blocking,
					int atomic, int hop)
{
	snd_seq_event_cell_t *cell;
	int err;

	/* special queue values - force direct passing */
	if (event->queue == SNDRV_SEQ_ADDRESS_SUBSCRIBERS) {
		event->dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
		event->queue = SNDRV_SEQ_QUEUE_DIRECT;
	} else if (event->queue == SNDRV_SEQ_ADDRESS_BROADCAST) {
		event->dest.client = SNDRV_SEQ_ADDRESS_BROADCAST;
		event->queue = SNDRV_SEQ_QUEUE_DIRECT;
	}

	if (event->dest.client == SNDRV_SEQ_ADDRESS_SUBSCRIBERS) {
		/* check presence of source port */
		client_port_t *src_port = snd_seq_port_use_ptr(client, event->source.port);
		if (src_port == NULL)
			return -EINVAL;
		snd_seq_ports_unlock(client);
	}

	/* direct event processing without enqueued */
	if (snd_seq_ev_is_direct(event)) {
		if (event->type == SNDRV_SEQ_EVENT_NOTE)
			return -EINVAL; /* this event must be enqueued! */
		return snd_seq_deliver_event(client, event, atomic, hop);
	}

	/* Not direct, normal queuing */
	if (snd_seq_queue_is_used(event->queue, client->number) <= 0)
		return -EINVAL;  /* invalid queue */
	if (! snd_seq_write_pool_allocated(client))
		return -ENXIO; /* queue is not allocated */

	/* allocate an event cell */
	err = snd_seq_event_dup(client->pool, event, &cell, !blocking && !atomic, file);
	if (err < 0)
		return err;

	/* we got a cell. enqueue it. */
	if ((err = snd_seq_enqueue_event(cell, atomic, hop)) < 0) {
		snd_seq_cell_free(cell);
		return err;
	}

	return 0;
}


/*
 * check validity of event type and data length.
 * return non-zero if invalid.
 */
static int check_event_type_and_length(snd_seq_event_t *ev)
{
	switch (snd_seq_ev_length_type(ev)) {
	case SNDRV_SEQ_EVENT_LENGTH_FIXED:
		if (snd_seq_ev_is_variable_type(ev) ||
		    snd_seq_ev_is_varipc_type(ev))
			return -EINVAL;
		break;
	case SNDRV_SEQ_EVENT_LENGTH_VARIABLE:
		if (! snd_seq_ev_is_variable_type(ev) ||
		    (ev->data.ext.len & ~SNDRV_SEQ_EXT_MASK) >= SNDRV_SEQ_MAX_EVENT_LEN)
			return -EINVAL;
		break;
	case SNDRV_SEQ_EVENT_LENGTH_VARUSR:
		if (! snd_seq_ev_is_instr_type(ev) ||
		    ! snd_seq_ev_is_direct(ev))
			return -EINVAL;
		break;
	case SNDRV_SEQ_EVENT_LENGTH_VARIPC:
		if (! snd_seq_ev_is_varipc_type(ev))
			return -EINVAL;
		break;
	}
	return 0;
}


/* handle write() */
/* possible error values:
 *	-ENXIO	invalid client or file open mode
 *	-ENOMEM	malloc failed
 *	-EFAULT	seg. fault during copy from user space
 *	-EINVAL	invalid event
 *	-EAGAIN	no space in output pool
 *	-EINTR	interrupts while sleep
 *	-EMLINK	too many hops
 *	others	depends on return value from driver callback
 */
static ssize_t snd_seq_write(struct file *file, const char *buf, size_t count, loff_t *offset)
{
	client_t *client = (client_t *) file->private_data;
	int written = 0, len;
	int err = -EINVAL;
	snd_seq_event_t event;

	if (!(snd_seq_file_flags(file) & SNDRV_SEQ_LFLG_OUTPUT))
		return -ENXIO;

	/* check client structures are in place */
	snd_assert(client != NULL, return -ENXIO);
		
	if (!client->accept_output || client->pool == NULL)
		return -ENXIO;

	/* allocate the pool now if the pool is not allocated yet */ 
	if (client->pool->size > 0 && !snd_seq_write_pool_allocated(client)) {
		if (snd_seq_pool_init(client->pool) < 0)
			return -ENOMEM;
	}

	/* only process whole events */
	while (count >= sizeof(snd_seq_event_t)) {
		/* Read in the event header from the user */
		len = sizeof(event);
		if (copy_from_user(&event, buf, len)) {
			err = -EFAULT;
			break;
		}
		event.source.client = client->number;	/* fill in client number */
		/* Check for extension data length */
		if (check_event_type_and_length(&event)) {
			err = -EINVAL;
			break;
		}

		/* check for special events */
		if (event.type == SNDRV_SEQ_EVENT_NONE)
			goto __skip_event;
		else if (snd_seq_ev_is_reserved(&event)) {
			err = -EINVAL;
			break;
		}

		if (snd_seq_ev_is_variable(&event)) {
			int extlen = event.data.ext.len & ~SNDRV_SEQ_EXT_MASK;
			if (extlen + len > count) {
				/* back out, will get an error this time or next */
				err = -EINVAL;
				break;
			}
			/* set user space pointer */
			event.data.ext.len = extlen | SNDRV_SEQ_EXT_USRPTR;
			event.data.ext.ptr = (char*)buf + sizeof(snd_seq_event_t);
			len += extlen; /* increment data length */
		}

		/* ok, enqueue it */
		err = snd_seq_client_enqueue_event(client, &event, file,
						   !(file->f_flags & O_NONBLOCK),
						   0, 0);
		if (err < 0)
			break;

	__skip_event:
		/* Update pointers and counts */
		count -= len;
		buf += len;
		written += len;
	}

	return written ? written : err;
}


/*
 * handle polling
 */
static unsigned int snd_seq_poll(struct file *file, poll_table * wait)
{
	client_t *client = (client_t *) file->private_data;
	unsigned int mask = 0;

	/* check client structures are in place */
	snd_assert(client != NULL, return -ENXIO);

	if ((snd_seq_file_flags(file) & SNDRV_SEQ_LFLG_INPUT) &&
	    client->data.user.fifo) {

		/* check if data is available in the outqueue */
		if (snd_seq_fifo_poll_wait(client->data.user.fifo, file, wait))
			mask |= POLLIN | POLLRDNORM;
	}

	if (snd_seq_file_flags(file) & SNDRV_SEQ_LFLG_OUTPUT) {

		/* check if data is available in the pool */
		if (!snd_seq_write_pool_allocated(client) ||
		    snd_seq_pool_poll_wait(client->pool, file, wait))
			mask |= POLLOUT | POLLWRNORM;
	}

	return mask;
}


/*-----------------------------------------------------*/


/* SYSTEM_INFO ioctl() */
static int snd_seq_ioctl_system_info(client_t *client, snd_seq_system_info_t * _info)
{
	snd_seq_system_info_t info;

	memset(&info, 0, sizeof(info));
	/* fill the info fields */
	info.queues = SNDRV_SEQ_MAX_QUEUES;
	info.clients = SNDRV_SEQ_MAX_CLIENTS;
	info.ports = 256;	/* fixed limit */
	info.channels = 256;	/* fixed limit */
	info.cur_clients = client_usage.cur;
	info.cur_queues = snd_seq_queue_get_cur_queues();

	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}


/* CLIENT_INFO ioctl() */
static void get_client_info(client_t *cptr, snd_seq_client_info_t *info)
{
	info->client = cptr->number;

	/* fill the info fields */
	info->type = cptr->type;
	strcpy(info->name, cptr->name);
	info->filter = cptr->filter;
	info->event_lost = cptr->event_lost;
	*info->event_filter = *cptr->event_filter;
	info->num_ports = cptr->num_ports;
	memset(info->reserved, 0, sizeof(info->reserved));
}

static int snd_seq_ioctl_get_client_info(client_t * client, snd_seq_client_info_t * _client_info)
{
	client_t *cptr;
	snd_seq_client_info_t client_info;

	if (copy_from_user(&client_info, _client_info, sizeof(client_info)))
		return -EFAULT;

	/* requested client number */
	cptr = snd_seq_client_use_ptr(client_info.client);
	if (cptr == NULL)
		return -ENOENT;		/* don't change !!! */

	get_client_info(cptr, &client_info);
	snd_seq_client_unlock(cptr);

	if (copy_to_user(_client_info, &client_info, sizeof(client_info)))
		return -EFAULT;
	return 0;
}


/* CLIENT_INFO ioctl() */
static int snd_seq_ioctl_set_client_info(client_t * client, snd_seq_client_info_t * _client_info)
{
	snd_seq_client_info_t client_info;

	if (copy_from_user(&client_info, _client_info, sizeof(client_info)))
		return -EFAULT;

	/* it is not allowed to set the info fields for an another client */
	if (client->number != client_info.client)
		return -EPERM;
	/* also client type must be set now */
	if (client->type != client_info.type)
		return -EINVAL;

	/* fill the info fields */
	if (client_info.name[0]) {
		strncpy(client->name, client_info.name, sizeof(client->name)-1);
		client->name[sizeof(client->name)-1] = '\0';
	}
	client->filter = client_info.filter;
	client->event_lost = client_info.event_lost;
	*client->event_filter = *client_info.event_filter;

	return 0;
}


/* 
 * CREATE PORT ioctl() 
 */
static int snd_seq_ioctl_create_port(client_t * client, snd_seq_port_info_t * _info)
{
	client_port_t *port;
	snd_seq_port_info_t info;
	snd_seq_port_callback_t *callback;

	if (in_interrupt())
		return -EBUSY;

	if (copy_from_user(&info, _info, sizeof(snd_seq_port_info_t)))
		return -EFAULT;

	/* it is not allowed to create the port for an another client */
	if (info.addr.client != client->number)
		return -EPERM;

	port = snd_seq_create_port(client, (info.flags & SNDRV_SEQ_PORT_FLG_GIVEN_PORT) ? info.addr.port : -1);
	if (port == NULL)
		return -ENOMEM;

	if (client->type == USER_CLIENT && info.kernel) {
		snd_seq_delete_port(client, port->port);
		return -EINVAL;
	}
	if (client->type == KERNEL_CLIENT) {
		if ((callback = info.kernel) != NULL) {
			if (callback->owner)
				port->owner = callback->owner;
			port->private_data = callback->private_data;
			port->subscribe = callback->subscribe;
			port->unsubscribe = callback->unsubscribe;
			port->use = callback->use;
			port->unuse = callback->unuse;
			port->event_input = callback->event_input;
			port->private_free = callback->private_free;
			port->callback_all = callback->callback_all;
		}
	}

	info.addr.port = port->port;

	snd_seq_set_port_info(port, &info);
	snd_seq_system_client_ev_port_start(client->number, port->port);

	if (copy_to_user(_info, &info, sizeof(snd_seq_port_info_t)))
		return -EFAULT;

	return 0;
}

/* 
 * DELETE PORT ioctl() 
 */
static int snd_seq_ioctl_delete_port(client_t * client, snd_seq_port_info_t * _info)
{
	snd_seq_port_info_t info;
	int err;

	if (in_interrupt())
		return -EBUSY;

	snd_assert(client != NULL && _info != NULL, return -EINVAL);

	/* set passed parameters */
	if (copy_from_user(&info, _info, sizeof(snd_seq_port_info_t)))
		return -EFAULT;
	
	/* it is not allowed to remove the port for an another client */
	if (info.addr.client != client->number)
		return -EPERM;

	err = snd_seq_delete_port(client, info.addr.port);
	if (err >= 0)
		snd_seq_system_client_ev_port_exit(client->number, info.addr.port);
	return err;
}


/* 
 * GET_PORT_INFO ioctl() (on any client) 
 */
static int snd_seq_ioctl_get_port_info(client_t *client, snd_seq_port_info_t * info)
{
	client_t *cptr;
	client_port_t *port;
	snd_seq_port_info_t _info;

	snd_assert(client != NULL, return -EINVAL);

	if (copy_from_user(&_info, info, sizeof(snd_seq_port_info_t)))
		return -EFAULT;
	cptr = snd_seq_client_use_ptr(_info.addr.client);
	if (cptr == NULL)
		return -ENXIO;

	port = snd_seq_port_use_ptr(cptr, _info.addr.port);
	if (port == NULL) {
		snd_seq_client_unlock(cptr);
		return -ENOENT;			/* don't change */
	}

	/* get port info */
	snd_seq_get_port_info(port, &_info);
	snd_seq_ports_unlock(cptr);
	snd_seq_client_unlock(cptr);

	if (copy_to_user(info, &_info, sizeof(snd_seq_port_info_t)))
		return -EFAULT;
	return 0;
}


/* 
 * SET_PORT_INFO ioctl() (only ports on this/own client) 
 */
static int snd_seq_ioctl_set_port_info(client_t * client, snd_seq_port_info_t * info)
{
	client_port_t *port;
	snd_seq_port_info_t _info;

	snd_assert(client != NULL && info != NULL, return -EINVAL);

	if (copy_from_user(&_info, info, sizeof(snd_seq_port_info_t)))
		return -EFAULT;

	if (_info.addr.client != client->number) /* only set our own ports ! */
		return -EPERM;
	port = snd_seq_port_use_ptr(client, _info.addr.port);
	if (port) {
		snd_seq_set_port_info(port, &_info);
		snd_seq_ports_unlock(client);
	}
	return 0;
}


/*
 * port subscription (connection)
 */
#define PERM_RD		(SNDRV_SEQ_PORT_CAP_READ|SNDRV_SEQ_PORT_CAP_SUBS_READ)
#define PERM_WR		(SNDRV_SEQ_PORT_CAP_WRITE|SNDRV_SEQ_PORT_CAP_SUBS_WRITE)

#define CHECK_SENDER	1
#define CHECK_DEST	2

static int connect_ports(client_port_t *sport, client_port_t *dport,
			 snd_seq_port_subscribe_t *info, int check)
{
	int result;

	/* check permissions */
	if (check & CHECK_SENDER) {
		if (! check_port_perm(sport, PERM_RD))
			return -EPERM;
		if ((info->flags & SNDRV_SEQ_PORT_SUBS_EXCLUSIVE) && sport->subscribe_count > 0)
			return -EBUSY;
		if (sport->export.exclusive)
			return -EBUSY;
	}
	if (check & CHECK_DEST) {
		if (! check_port_perm(dport, PERM_WR))
			return -EPERM;
		if ((info->flags & SNDRV_SEQ_PORT_SUBS_EXCLUSIVE) && dport->use_count > 0)
			return -EBUSY;
		if (dport->import.exclusive)
			return -EBUSY;
	}
	if (snd_seq_port_is_subscribed(&sport->export, &info->dest) ||
	    snd_seq_port_is_subscribed(&dport->import, &info->sender))
		return -EBUSY;

	/* subscription */
	if ((result = snd_seq_port_subscribe(sport, info)) < 0)
		return result;
	if ((result = snd_seq_port_use(dport, info)) < 0) {
		snd_seq_port_unsubscribe(sport, info);
		return result;
	}

	/* add subscriber list */
	result = snd_seq_port_add_subscriber(&sport->export, 
					     &info->dest, info,
					     ((info->flags & SNDRV_SEQ_PORT_SUBS_EXCLUSIVE) && CHECK_SENDER));
	if (result < 0) {
		snd_seq_port_unsubscribe(sport, info);
		snd_seq_port_unuse(dport, info);
		return result;
	}
	result = snd_seq_port_add_subscriber(&dport->import,
					     &info->sender, info,
					     ((info->flags & SNDRV_SEQ_PORT_SUBS_EXCLUSIVE) && CHECK_DEST));
	if (result < 0) {
		snd_seq_port_remove_subscriber(&sport->export, &info->dest, info);
		snd_seq_port_unsubscribe(sport, info);
		snd_seq_port_unuse(dport, info);
		return result;
	}
	return 0;
}

/*
 * send an subscription notify event to user client:
 * client must be user client.
 */
int snd_seq_client_notify_subscription(int client, int port,
				       snd_seq_port_subscribe_t *info, int evtype)
{
	snd_seq_event_t event;

	memset(&event, 0, sizeof(event));
	event.type = evtype;
	event.data.connect.dest = info->dest;
	event.data.connect.sender = info->sender;

	return snd_seq_system_notify(client, port, &event);  /* non-atomic */
}


/* 
 * add to port's subscription list IOCTL interface 
 */
static int snd_seq_ioctl_subscribe_port(client_t * client, snd_seq_port_subscribe_t * _subs)
{
	int result = -EINVAL;
	client_t *receiver = NULL, *sender = NULL;
	client_port_t *sport = NULL, *dport = NULL;
	snd_seq_port_subscribe_t subs;
	int check;

	if (in_interrupt())
		return -EBUSY;

	snd_assert(client != NULL, return -EINVAL);

	if (copy_from_user(&subs, _subs, sizeof(snd_seq_port_subscribe_t)))
		return -EFAULT;

	if ((receiver = snd_seq_client_use_ptr(subs.dest.client)) == NULL)
		goto __end;
	if ((sender = snd_seq_client_use_ptr(subs.sender.client)) == NULL)
		goto __end;
	if ((sport = snd_seq_port_use_ptr(sender, subs.sender.port)) == NULL)
		goto __end;
	if ((dport = snd_seq_port_use_ptr(receiver, subs.dest.port)) == NULL)
		goto __end;

	check = CHECK_DEST|CHECK_SENDER;
	/* if sender or receiver is the subscribing client itself,
	 * no permission check is necessary
	 */
	if (client->number == subs.dest.client)
		check &= ~CHECK_DEST;
	if (client->number == subs.sender.client)
		check &= ~CHECK_SENDER;

	if (check == (CHECK_DEST|CHECK_SENDER)) {
		/* connection by third client - check export permission */
		result = -EPERM;
		if (check_port_perm(sport, SNDRV_SEQ_PORT_CAP_NO_EXPORT))
			goto __end;
		if (check_port_perm(dport, SNDRV_SEQ_PORT_CAP_NO_EXPORT))
			goto __end;
	}

	/* connect them */
	if ((result = connect_ports(sport, dport, &subs, check)) >= 0) {
		/* succeeded - notify if necessary */
		if (client->number != subs.sender.client && sender->type == USER_CLIENT)
			snd_seq_client_notify_subscription(subs.sender.client, subs.sender.port,
							   &subs, SNDRV_SEQ_EVENT_PORT_SUBSCRIBED);
		if (client->number != subs.dest.client && receiver->type == USER_CLIENT)
			snd_seq_client_notify_subscription(subs.dest.client, subs.dest.port,
							   &subs, SNDRV_SEQ_EVENT_PORT_SUBSCRIBED);
		/* broadcast announce */
		snd_seq_client_notify_subscription(SNDRV_SEQ_ADDRESS_SUBSCRIBERS, 0, &subs, SNDRV_SEQ_EVENT_PORT_SUBSCRIBED);
	}

      __end:
      	if (sport)
		snd_seq_ports_unlock(sender);
	if (dport)
		snd_seq_ports_unlock(receiver);
	if (sender)
		snd_seq_client_unlock(sender);
	if (receiver)
		snd_seq_client_unlock(receiver);
	return result;
}


/* 
 * remove from port's subscription list 
 */
static int snd_seq_ioctl_unsubscribe_port(client_t * client, snd_seq_port_subscribe_t * _subs)
{
	int result = -ENXIO;
	client_t *receiver = NULL, *sender = NULL;
	client_port_t *sport = NULL, *dport = NULL;
	snd_seq_port_subscribe_t subs;

	if (in_interrupt())
		return -EBUSY;

	snd_assert(client != NULL, return -EINVAL);

	if (copy_from_user(&subs, _subs, sizeof(snd_seq_port_subscribe_t)))
		return -EFAULT;

	if ((receiver = snd_seq_client_use_ptr(subs.dest.client)) == NULL)
		goto __end;
	if ((sender = snd_seq_client_use_ptr(subs.sender.client)) == NULL)
		goto __end;
	if ((sport = snd_seq_port_use_ptr(sender, subs.sender.port)) == NULL)
		goto __end;
	if ((dport = snd_seq_port_use_ptr(receiver, subs.dest.port)) == NULL)
		goto __end;

	/* check permissions */
	result = -EPERM;
	if (client->number != subs.sender.client) {
		if (! check_port_perm(sport, PERM_RD))
			goto __end;
	}
	if (client->number != subs.dest.client) {
		if (! check_port_perm(dport, PERM_WR))
			goto __end;
	}
	if (client->number != subs.sender.client &&
	    client->number != subs.dest.client) {
		/* connection by third client - check export permission */
		if (check_port_perm(sport, SNDRV_SEQ_PORT_CAP_NO_EXPORT))
			goto __end;
		if (check_port_perm(dport, SNDRV_SEQ_PORT_CAP_NO_EXPORT))
			goto __end;
	}

	result = -ENOENT;
	if (snd_seq_port_is_subscribed(&sport->export, &subs.dest)) {
		snd_seq_port_remove_subscriber(&sport->export, &subs.dest, &subs);
		snd_seq_port_unsubscribe(sport, &subs);
		if (client->number != subs.sender.client &&
		    sender->type == USER_CLIENT)
			snd_seq_client_notify_subscription(subs.sender.client, subs.sender.port,
							   &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
		result = 0;
	}

	if (snd_seq_port_is_subscribed(&dport->import, &subs.sender)) {
		snd_seq_port_remove_subscriber(&dport->import, &subs.sender, &subs);
		snd_seq_port_unuse(dport, &subs);
		if (client->number != subs.sender.client &&
		    receiver->type == USER_CLIENT)
			snd_seq_client_notify_subscription(subs.dest.client, subs.dest.port,
							   &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
		result = 0;
	}
		
	if (! result) /* broadcast announce */
		snd_seq_client_notify_subscription(SNDRV_SEQ_ADDRESS_SUBSCRIBERS, 0,
						   &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
      __end:
      	if (sport)
		snd_seq_ports_unlock(sender);
	if (dport)
		snd_seq_ports_unlock(receiver);
	if (sender)
		snd_seq_client_unlock(sender);
	if (receiver)
		snd_seq_client_unlock(receiver);
	return result;
}


/* CREATE_QUEUE ioctl() */
static int snd_seq_ioctl_create_queue(client_t *client, snd_seq_queue_info_t * _info)
{
	snd_seq_queue_info_t info;
	int result;
	queue_t *q;

	if (copy_from_user(&info, _info, sizeof(snd_seq_queue_info_t)))
		return -EFAULT;

	result = snd_seq_queue_alloc(client->number, info.locked, info.flags);
	if (result < 0)
		return result;

	q = queueptr(result);
	if (q == NULL)
		return -EINVAL;

	info.queue = q->queue;
	info.locked = q->locked;
	info.owner = q->owner;

	/* set queue name */
	if (! info.name[0])
		sprintf(info.name, "Queue-%d", q->queue);
	strncpy(q->name, info.name, sizeof(q->name)-1);
	q->name[sizeof(q->name)-1] = 0;
	queuefree(q);

	if (copy_to_user(_info, &info, sizeof(snd_seq_queue_info_t)))
		return -EFAULT;

	return 0;
}

/* DELETE_QUEUE ioctl() */
static int snd_seq_ioctl_delete_queue(client_t *client, snd_seq_queue_info_t * _info)
{
	snd_seq_queue_info_t info;

	if (copy_from_user(&info, _info, sizeof(snd_seq_queue_info_t)))
		return -EFAULT;

	return snd_seq_queue_delete(client->number, info.queue);
}

/* GET_QUEUE_INFO ioctl() */
static int snd_seq_ioctl_get_queue_info(client_t *client, snd_seq_queue_info_t * _info)
{
	snd_seq_queue_info_t info;
	queue_t *q;

	if (copy_from_user(&info, _info, sizeof(snd_seq_queue_info_t)))
		return -EFAULT;

	q = queueptr(info.queue);
	if (q == NULL)
		return -EINVAL;

	memset(&info, 0, sizeof(info));
	info.queue = q->queue;
	info.owner = q->owner;
	info.locked = q->locked;
	strncpy(info.name, q->name, sizeof(info.name) - 1);
	info.name[sizeof(info.name)-1] = 0;
	queuefree(q);

	if (copy_to_user(_info, &info, sizeof(snd_seq_queue_info_t)))
		return -EFAULT;

	return 0;
}

/* change queue owner and name */
static int set_queue_info(client_t *client, snd_seq_queue_info_t * _info, int change_name)
{
	snd_seq_queue_info_t info;

	if (copy_from_user(&info, _info, sizeof(snd_seq_queue_info_t)))
		return -EFAULT;

	if (info.owner != client->number)
		return -EINVAL;

	/* change owner/locked permission */
	if (snd_seq_queue_check_access(info.queue, client->number)) {
		if (snd_seq_queue_set_owner(info.queue, client->number, info.locked) < 0)
			return -EPERM;
		if (info.locked)
			snd_seq_queue_use(info.queue, client->number, 1);
	} else {
		return -EPERM;
	}	

	if (change_name) {
		queue_t *q = queueptr(info.queue);
		if (! q)
			return -EINVAL;
		if (q->owner != client->number) {
			queuefree(q);
			return -EPERM;
		}
		strncpy(q->name, info.name, sizeof(q->name) - 1);
		q->name[sizeof(q->name)-1] = 0;
		queuefree(q);
	}

	return 0;
}

/* SET_QUEUE_INFO ioctl() */
static int snd_seq_ioctl_set_queue_info(client_t *client, snd_seq_queue_info_t * _info)
{
	return set_queue_info(client, _info, 1);
}

/* GET_NAMED_QUEUE ioctl() */
static int snd_seq_ioctl_get_named_queue(client_t *client, snd_seq_queue_info_t * _info)
{
	snd_seq_queue_info_t info;
	queue_t *q;

	if (copy_from_user(&info, _info, sizeof(snd_seq_queue_info_t)))
		return -EFAULT;

	q = snd_seq_queue_find_name(info.name);
	if (q == NULL)
		return -EINVAL;
	info.queue = q->queue;
	info.owner = q->owner;
	info.locked = q->locked;
	queuefree(q);

	if (copy_to_user(_info, &info, sizeof(snd_seq_queue_info_t)))
		return -EFAULT;

	return 0;
}

/* GET_QUEUE_STATUS ioctl() */
static int snd_seq_ioctl_get_queue_status(client_t * client, snd_seq_queue_status_t * _status)
{
	snd_seq_queue_status_t status;
	queue_t *queue;
	seq_timer_t *tmr;

	if (copy_from_user(&status, _status, sizeof(snd_seq_queue_status_t)))
		return -EFAULT;

	queue = queueptr(status.queue);
	if (queue == NULL)
		return -EINVAL;
	memset(&status, 0, sizeof(status));
	status.queue = queue->queue;
	
	tmr = queue->timer;
	status.events = queue->tickq->cells + queue->timeq->cells;

	status.time = snd_seq_timer_get_cur_time(tmr);
	status.tick = snd_seq_timer_get_cur_tick(tmr);

	status.running = tmr->running;

	status.flags = queue->flags;
	queuefree(queue);

	if (copy_to_user(_status, &status, sizeof(snd_seq_queue_status_t)))
		return -EFAULT;
	return 0;
}


/* GET_QUEUE_TEMPO ioctl() */
static int snd_seq_ioctl_get_queue_tempo(client_t * client, snd_seq_queue_tempo_t * _tempo)
{
	snd_seq_queue_tempo_t tempo;
	queue_t *queue;
	seq_timer_t *tmr;

	if (copy_from_user(&tempo, _tempo, sizeof(snd_seq_queue_tempo_t)))
		return -EFAULT;

	queue = queueptr(tempo.queue);
	if (queue == NULL)
		return -EINVAL;
	memset(&tempo, 0, sizeof(tempo));
	tempo.queue = queue->queue;
	
	tmr = queue->timer;

	tempo.tempo = tmr->tempo;
	tempo.ppq = tmr->ppq;
	queuefree(queue);

	if (copy_to_user(_tempo, &tempo, sizeof(snd_seq_queue_tempo_t)))
		return -EFAULT;
	return 0;
}


/* SET_QUEUE_TEMPO ioctl() */
static int snd_seq_ioctl_set_queue_tempo(client_t * client, snd_seq_queue_tempo_t * _tempo)
{
	int result;
	snd_seq_queue_tempo_t tempo;

	if (copy_from_user(&tempo, _tempo, sizeof(snd_seq_queue_tempo_t)))
		return -EFAULT;

	if (snd_seq_queue_check_access(tempo.queue, client->number)) {
		result = snd_seq_queue_timer_set_tempo(tempo.queue, client->number, tempo.tempo, tempo.ppq);
		if (result < 0)
			return result;
	} else {
		return -EPERM;
	}	

	return 0;
}


/* GET_QUEUE_TIMER ioctl() */
static int snd_seq_ioctl_get_queue_timer(client_t * client, snd_seq_queue_timer_t * _timer)
{
	snd_seq_queue_timer_t timer;
	queue_t *queue;
	seq_timer_t *tmr;

	if (in_interrupt())
		return -EBUSY;

	if (copy_from_user(&timer, _timer, sizeof(snd_seq_queue_timer_t)))
		return -EFAULT;

	queue = queueptr(timer.queue);
	if (queue == NULL)
		return -EINVAL;
	tmr = queue->timer;

	memset(&timer, 0, sizeof(timer));
	timer.queue = queue->queue;

	if (down_interruptible(&queue->use_mutex)) {
		queuefree(queue);
		return -ERESTARTSYS;
	}
	timer.type = tmr->type;
	if (tmr->type == SNDRV_SEQ_TIMER_ALSA) {
		timer.u.alsa.id = tmr->alsa_id;
		timer.u.alsa.resolution = tmr->resolution;
	}
	up(&queue->use_mutex);
	queuefree(queue);
	
	if (copy_to_user(_timer, &timer, sizeof(snd_seq_queue_timer_t)))
		return -EFAULT;
	return 0;
}


/* SET_QUEUE_TIMER ioctl() */
static int snd_seq_ioctl_set_queue_timer(client_t * client, snd_seq_queue_timer_t * _timer)
{
	int result = 0;
	snd_seq_queue_timer_t timer;

	if (in_interrupt())
		return -EBUSY;

	if (copy_from_user(&timer, _timer, sizeof(snd_seq_queue_timer_t)))
		return -EFAULT;

	if (timer.type < 0 || timer.type >= SNDRV_SEQ_TIMER_MIDI_TICK)
		return -EINVAL;

	if (snd_seq_queue_check_access(timer.queue, client->number)) {
		queue_t *q;
		seq_timer_t *tmr;

		q = queueptr(timer.queue);
		if (q == NULL)
			return -ENXIO;
		tmr = q->timer;
		if (down_interruptible(&q->use_mutex)) {
			queuefree(q);
			return -ERESTARTSYS;
		}
		snd_seq_queue_timer_close(timer.queue);
		tmr->type = timer.type;
		if (tmr->type == SNDRV_SEQ_TIMER_ALSA) {
			tmr->alsa_id = timer.u.alsa.id;
			tmr->resolution = timer.u.alsa.resolution;
		}
		result = snd_seq_queue_timer_open(timer.queue);
		up(&q->use_mutex);
		queuefree(q);
	} else {
		return -EPERM;
	}	

	return result;
}


/* GET_QUEUE_CLIENT ioctl() */
static int snd_seq_ioctl_get_queue_client(client_t * client, snd_seq_queue_client_t * _info)
{
	snd_seq_queue_client_t info;
	int used;

	if (copy_from_user(&info, _info, sizeof(snd_seq_queue_client_t)))
		return -EFAULT;

	used = snd_seq_queue_is_used(info.queue, client->number);
	if (used < 0)
		return -EINVAL;
	info.used = used;
	info.client = client->number;

	if (copy_to_user(_info, &info, sizeof(snd_seq_queue_client_t)))
		return -EFAULT;
	return 0;
}


/* SET_QUEUE_CLIENT ioctl() */
static int snd_seq_ioctl_set_queue_client(client_t * client, snd_seq_queue_client_t * _info)
{
	int err;
	snd_seq_queue_client_t info;

	if (copy_from_user(&info, _info, sizeof(snd_seq_queue_client_t)))
		return -EFAULT;

	if (info.used >= 0) {
		err = snd_seq_queue_use(info.queue, client->number, info.used);
		if (err < 0)
			return err;
	}

	return snd_seq_ioctl_get_queue_client(client, _info);
}


/* GET_CLIENT_POOL ioctl() */
static int snd_seq_ioctl_get_client_pool(client_t * client, snd_seq_client_pool_t * _info)
{
	snd_seq_client_pool_t info;
	client_t *cptr;

	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;

	cptr = snd_seq_client_use_ptr(info.client);
	if (cptr == NULL)
		return -ENOENT;
	memset(&info, 0, sizeof(info));
	info.output_pool = cptr->pool->size;
	info.output_room = cptr->pool->room;
	info.output_free = info.output_pool;
	if (cptr->pool)
		info.output_free = snd_seq_unused_cells(cptr->pool);
	if (cptr->type == USER_CLIENT) {
		info.input_pool = cptr->data.user.fifo_pool_size;
		info.input_free = info.input_pool;
		if (cptr->data.user.fifo)
			info.input_free = snd_seq_unused_cells(cptr->data.user.fifo->pool);
	} else {
		info.input_pool = 0;
		info.input_free = 0;
	}
	snd_seq_client_unlock(cptr);
	
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

/* SET_CLIENT_POOL ioctl() */
static int snd_seq_ioctl_set_client_pool(client_t * client, snd_seq_client_pool_t * _info)
{
	snd_seq_client_pool_t info;
	int rc;

	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;

	if (client->number != info.client)
		return -EINVAL; /* can't change other clients */

	if (info.output_pool >= 1 && info.output_pool <= SNDRV_SEQ_MAX_EVENTS &&
	    (! snd_seq_write_pool_allocated(client) ||
	     info.output_pool != client->pool->size)) {
		if (snd_seq_write_pool_allocated(client)) {
			/* remove all existing cells */
			snd_seq_queue_client_leave_cells(client->number);
			snd_seq_pool_done(client->pool);
		}
		client->pool->size = info.output_pool;
		rc = snd_seq_pool_init(client->pool);
		if (rc < 0)
			return rc;
		client->pool->room = (info.output_pool + 1) / 2;
	}
	if (client->type == USER_CLIENT && client->data.user.fifo != NULL &&
	    info.input_pool >= 1 &&
	    info.input_pool <= SNDRV_SEQ_MAX_CLIENT_EVENTS &&
	    info.input_pool != client->data.user.fifo_pool_size) {
		/* change pool size */
		rc = snd_seq_fifo_resize(client->data.user.fifo, info.input_pool);
		if (rc < 0)
			return rc;
		client->data.user.fifo_pool_size = info.input_pool;
	}
	if (info.output_room >= 1 &&
	    info.output_room <= client->pool->size) {
		client->pool->room  = info.output_room;
	}

	return snd_seq_ioctl_get_client_pool(client, _info);
}


/* REMOVE_EVENTS ioctl() */
static int snd_seq_ioctl_remove_events(client_t * client,
				       snd_seq_remove_events_t *_info)
{
	snd_seq_remove_events_t info;

	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;

	/*
	 * Input mostly not implemented XXX.
	 */
	if (info.remove_mode & SNDRV_SEQ_REMOVE_INPUT) {
		/*
		 * No restrictions so for a user client we can clear
		 * the whole fifo
		 */
		if (client->type == USER_CLIENT)
			snd_seq_fifo_clear(client->data.user.fifo);
	}

	if (info.remove_mode & SNDRV_SEQ_REMOVE_OUTPUT)
		snd_seq_queue_remove_cells(client->number, &info);

	return 0;
}


/*
 * get subscription info
 */
static int snd_seq_ioctl_get_subscription(client_t *client, snd_seq_port_subscribe_t *_subs)
{
	int result;
	client_t *sender = NULL;
	client_port_t *sport = NULL;
	snd_seq_port_subscribe_t subs;
	subscribers_t *p;

	if (copy_from_user(&subs, _subs, sizeof(subs)))
		return -EFAULT;

	result = -EINVAL;
	if ((sender = snd_seq_client_use_ptr(subs.sender.client)) == NULL)
		goto __end;
	if ((sport = snd_seq_port_use_ptr(sender, subs.sender.port)) == NULL)
		goto __end;
	p = snd_seq_port_get_subscription(&sport->export, &subs.dest);
	if (p) {
		result = 0;
		subs = p->info;
		snd_seq_subscribers_unlock(&sport->export);
	} else
		result = -ENOENT;

      __end:
      	if (sport)
		snd_seq_ports_unlock(sender);
	if (sender)
		snd_seq_client_unlock(sender);
	if (result >= 0) {
		if (copy_to_user(_subs, &subs, sizeof(subs)))
			return -EFAULT;
	}
	return result;
}


/*
 * get subscription info - check only its presence
 */
static int snd_seq_ioctl_query_subs(client_t *client, snd_seq_query_subs_t *_subs)
{
	int result = -ENXIO;
	client_t *cptr = NULL;
	client_port_t *port = NULL;
	snd_seq_query_subs_t subs;
	subscribers_t *p;
	subscribers_group_t *group;
	int i;

	if (copy_from_user(&subs, _subs, sizeof(subs)))
		return -EFAULT;

	if ((cptr = snd_seq_client_use_ptr(subs.root.client)) == NULL)
		goto __end;
	if ((port = snd_seq_port_use_ptr(cptr, subs.root.port)) == NULL)
		goto __end;

	switch (subs.type) {
	case SNDRV_SEQ_QUERY_SUBS_READ:
		group = &port->export;
		break;
	case SNDRV_SEQ_QUERY_SUBS_WRITE:
		group = &port->import;
		break;
	default:
		goto __end;
	}

	snd_seq_subscribers_lock(group);
	/* search for the subscriber */
	subs.num_subs = group->count;
	p = group->list;
	for (i = 0; i < subs.index; i++) {
		if (p == NULL)
			break;
		p = p->next;
	}
	if (p) {
		/* found! */
		subs.addr = p->addr;
		subs.flags = p->info.flags;
		subs.queue = p->info.queue;
		result = 0;
	} else
		result = -ENOENT;
	snd_seq_subscribers_unlock(group);

      __end:
   	if (port)
		snd_seq_ports_unlock(cptr);
	if (cptr)
		snd_seq_client_unlock(cptr);
	if (result >= 0) {
		if (copy_to_user(_subs, &subs, sizeof(subs)))
			return -EFAULT;
	}
	return result;
}


/*
 * query next client
 */
static int snd_seq_ioctl_query_next_client(client_t *client, snd_seq_client_info_t *info)
{
	client_t *cptr = NULL;
	snd_seq_client_info_t _info;

	if (copy_from_user(&_info, info, sizeof(_info)))
		return -EFAULT;

	/* search for next client */
	_info.client++;
	if (_info.client < 0)
		_info.client = 0;
	for (; _info.client < SNDRV_SEQ_MAX_CLIENTS; _info.client++) {
		cptr = snd_seq_client_use_ptr(_info.client);
		if (cptr)
			break; /* found */
	}
	if (cptr == NULL)
		return -ENOENT;

	get_client_info(cptr, &_info);
	snd_seq_client_unlock(cptr);

	if (copy_to_user(info, &_info, sizeof(_info)))
		return -EFAULT;
	return 0;
}

/* 
 * query next port
 */
static int snd_seq_ioctl_query_next_port(client_t *client, snd_seq_port_info_t *info)
{
	client_t *cptr;
	client_port_t *port = NULL;
	snd_seq_port_info_t _info;

	if (copy_from_user(&_info, info, sizeof(_info)))
		return -EFAULT;
	cptr = snd_seq_client_use_ptr(_info.addr.client);
	if (cptr == NULL)
		return -ENXIO;

	/* search for next port */
	_info.addr.port++;
	port = snd_seq_port_query_nearest(cptr, &_info);
	if (port == NULL) {
		snd_seq_client_unlock(cptr);
		return -ENOENT;
	}

	/* get port info */
	_info.addr.port = port->port;
	snd_seq_get_port_info(port, &_info);
	snd_seq_ports_unlock(cptr);
	snd_seq_client_unlock(cptr);

	if (copy_to_user(info, &_info, sizeof(_info)))
		return -EFAULT;
	return 0;
}

/* -------------------------------------------------------- */

static int snd_seq_do_ioctl(client_t *client, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {

		case SNDRV_SEQ_IOCTL_PVERSION:
			/* return sequencer version number */
			return put_user(SNDRV_SEQ_VERSION, (int *)arg) ? -EFAULT : 0;

		case SNDRV_SEQ_IOCTL_CLIENT_ID:
			/* return the id of this client */
			return put_user(client->number, (int *)arg) ? -EFAULT : 0;

		case SNDRV_SEQ_IOCTL_SYSTEM_INFO:
			/* return system information */
			return snd_seq_ioctl_system_info(client, (snd_seq_system_info_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_CLIENT_INFO:
			/* return info on specified client */
			return snd_seq_ioctl_get_client_info(client, (snd_seq_client_info_t *) arg);

		case SNDRV_SEQ_IOCTL_SET_CLIENT_INFO:
			/* set info on specified client */
			return snd_seq_ioctl_set_client_info(client, (snd_seq_client_info_t *) arg);

		case SNDRV_SEQ_IOCTL_CREATE_PORT:
			/* create a port for this client */
			return snd_seq_ioctl_create_port(client, (snd_seq_port_info_t *) arg);

		case SNDRV_SEQ_IOCTL_DELETE_PORT:
			/* remove a port from this client */
			return snd_seq_ioctl_delete_port(client, (snd_seq_port_info_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_PORT_INFO:
			/* get info for specified port */
			return snd_seq_ioctl_get_port_info(client, (snd_seq_port_info_t *) arg);

		case SNDRV_SEQ_IOCTL_SET_PORT_INFO:
			/* set info for specified port in this client */
			return snd_seq_ioctl_set_port_info(client, (snd_seq_port_info_t *) arg);

		case SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT:
			/* add to port's subscription list */
			return snd_seq_ioctl_subscribe_port(client, (snd_seq_port_subscribe_t *) arg);

		case SNDRV_SEQ_IOCTL_UNSUBSCRIBE_PORT:
			/* remove from port's subscription list */
			return snd_seq_ioctl_unsubscribe_port(client, (snd_seq_port_subscribe_t *) arg);

		case SNDRV_SEQ_IOCTL_CREATE_QUEUE:
			/* create a queue */
			return snd_seq_ioctl_create_queue(client, (snd_seq_queue_info_t *) arg);

		case SNDRV_SEQ_IOCTL_DELETE_QUEUE:
			/* delete a queue */
			return snd_seq_ioctl_delete_queue(client, (snd_seq_queue_info_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_QUEUE_INFO:
			/* get queue information */
			return snd_seq_ioctl_get_queue_info(client, (snd_seq_queue_info_t *) arg);

		case SNDRV_SEQ_IOCTL_SET_QUEUE_INFO:
			/* set queue information */
			return snd_seq_ioctl_set_queue_info(client, (snd_seq_queue_info_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_NAMED_QUEUE:
			/* query the queue information from the name */
			return snd_seq_ioctl_get_named_queue(client, (snd_seq_queue_info_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_QUEUE_STATUS:
			/* get the status for the specified queue */
			return snd_seq_ioctl_get_queue_status(client, (snd_seq_queue_status_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_QUEUE_TEMPO:
			/* get the tempo for the specified queue */
			return snd_seq_ioctl_get_queue_tempo(client, (snd_seq_queue_tempo_t *) arg);

		case SNDRV_SEQ_IOCTL_SET_QUEUE_TEMPO:
			/* set a tempo for the specified queue */
			return snd_seq_ioctl_set_queue_tempo(client, (snd_seq_queue_tempo_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_QUEUE_TIMER:
			/* get the timer for the specified queue */
			return snd_seq_ioctl_get_queue_timer(client, (snd_seq_queue_timer_t *) arg);

		case SNDRV_SEQ_IOCTL_SET_QUEUE_TIMER:
			/* set an timer for the specified queue */
			return snd_seq_ioctl_set_queue_timer(client, (snd_seq_queue_timer_t *) arg);
		case SNDRV_SEQ_IOCTL_GET_QUEUE_CLIENT:
			/* get client specific info for specified queue */
			return snd_seq_ioctl_get_queue_client(client, (snd_seq_queue_client_t *) arg);

		case SNDRV_SEQ_IOCTL_SET_QUEUE_CLIENT:
			/* set client specfic info for specified queue */
			return snd_seq_ioctl_set_queue_client(client, (snd_seq_queue_client_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_CLIENT_POOL:
			/* get client pool size */
			return snd_seq_ioctl_get_client_pool(client, (snd_seq_client_pool_t *) arg);

		case SNDRV_SEQ_IOCTL_SET_CLIENT_POOL:
			/* set client pool size */
			return snd_seq_ioctl_set_client_pool(client, (snd_seq_client_pool_t *) arg);

		case SNDRV_SEQ_IOCTL_GET_SUBSCRIPTION:
			/* get subscription info */
			return snd_seq_ioctl_get_subscription(client, (snd_seq_port_subscribe_t *) arg);

		case SNDRV_SEQ_IOCTL_QUERY_NEXT_CLIENT:
			/* query next client */
			return snd_seq_ioctl_query_next_client(client, (snd_seq_client_info_t *)arg);

		case SNDRV_SEQ_IOCTL_QUERY_NEXT_PORT:
			/* query next port */
			return snd_seq_ioctl_query_next_port(client, (snd_seq_port_info_t *)arg);

		case SNDRV_SEQ_IOCTL_REMOVE_EVENTS:
			return snd_seq_ioctl_remove_events(client, (snd_seq_remove_events_t *) arg);

		case SNDRV_SEQ_IOCTL_QUERY_SUBS:
			return snd_seq_ioctl_query_subs(client, (snd_seq_query_subs_t *) arg);

		default:
			snd_printd("seq unknown ioctl() 0x%x (type='%c', number=0x%2x)\n",
				   cmd, _IOC_TYPE(cmd), _IOC_NR(cmd));
	}
	return -ENOTTY;
}


static int snd_seq_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	client_t *client = (client_t *) file->private_data;

	snd_assert(client != NULL, return -ENXIO);
		
	return snd_seq_do_ioctl(client, cmd, arg);
}


/* -------------------------------------------------------- */


/* exported to kernel modules */
int snd_seq_create_kernel_client(snd_card_t *card, int client_index, snd_seq_client_callback_t * callback)
{
	client_t *client;

	if (in_interrupt())
		return -EBUSY;

	if (callback == NULL)
		return -EINVAL;
	if (card && client_index > 7)
		return -EINVAL;
	if (card == NULL && client_index > 63)
		return -EINVAL;
	if (card)
		client_index += 64 + (card->number << 3);

	if (down_interruptible(&register_mutex))
		return -ERESTARTSYS;
	/* empty write queue as default */
	client = seq_create_client1(client_index, 0);
	if (client == NULL) {
		up(&register_mutex);
		return -EBUSY;	/* failure code */
	}
	usage_alloc(&client_usage, 1);

	client->accept_input = callback->allow_output;
	client->accept_output = callback->allow_input;
		
	/* fill client data */
	client->data.kernel.card = card;
	client->data.kernel.private_data = callback->private_data;
	sprintf(client->name, "Client-%d", client->number);

	client->type = KERNEL_CLIENT;
	up(&register_mutex);

	/* make others aware this new client */
	snd_seq_system_client_ev_client_start(client->number);
	
	/* return client number to caller */
	return client->number;
}

/* exported to kernel modules */
int snd_seq_delete_kernel_client(int client)
{
	client_t *ptr;

	if (in_interrupt())
		return -EBUSY;

	ptr = clientptr(client);
	if (ptr == NULL)
		return -EINVAL;

	seq_free_client(ptr);
	kfree(ptr);
	return 0;
}


/* skeleton to enqueue event, called from snd_seq_kernel_client_enqueue
 * and snd_seq_kernel_client_enqueue_blocking
 */
static int kernel_client_enqueue(int client, snd_seq_event_t *ev,
				 struct file *file, int blocking,
				 int atomic, int hop)
{
	client_t *cptr;
	int result;

	snd_assert(ev != NULL, return -EINVAL);

	if (ev->type == SNDRV_SEQ_EVENT_NONE)
		return 0; /* ignore this */
	if (ev->type == SNDRV_SEQ_EVENT_KERNEL_ERROR ||
	    ev->type == SNDRV_SEQ_EVENT_KERNEL_QUOTE)
		return -EINVAL; /* quoted events can't be enqueued */

	/* fill in client number */
	ev->source.client = client;

	if (check_event_type_and_length(ev))
		return -EINVAL;

	cptr = snd_seq_client_use_ptr(client);
	if (cptr == NULL)
		return -EINVAL;
	
	if (! cptr->accept_output)
		result = -EPERM;
	else /* send it */
		result = snd_seq_client_enqueue_event(cptr, ev, file, blocking, atomic, hop);

	snd_seq_client_unlock(cptr);
	return result;
}

/*
 * exported, called by kernel clients to enqueue events (w/o blocking)
 *
 * RETURN VALUE: zero if succeed, negative if error
 */
int snd_seq_kernel_client_enqueue(int client, snd_seq_event_t * ev,
				  int atomic, int hop)
{
	return kernel_client_enqueue(client, ev, NULL, 0, atomic, hop);
}

/*
 * exported, called by kernel clients to enqueue events (with blocking)
 *
 * RETURN VALUE: zero if succeed, negative if error
 */
int snd_seq_kernel_client_enqueue_blocking(int client, snd_seq_event_t * ev,
					   struct file *file,
					   int atomic, int hop)
{
	return kernel_client_enqueue(client, ev, file, 1, atomic, hop);
}


/* 
 * exported, called by kernel clients to dispatch events directly to other
 * clients, bypassing the queues.  Event time-stamp will be updated.
 *
 * RETURN VALUE: negative = delivery failed,
 *		 zero, or positive: the number of delivered events
 */
int snd_seq_kernel_client_dispatch(int client, snd_seq_event_t * ev,
				   int atomic, int hop)
{
	client_t *cptr;
	int result;

	snd_assert(ev != NULL, return -EINVAL);

	/* fill in client number */
	ev->queue = SNDRV_SEQ_QUEUE_DIRECT;
	ev->source.client = client;

	if (check_event_type_and_length(ev))
		return -EINVAL;

	cptr = snd_seq_client_use_ptr(client);
	if (cptr == NULL)
		return -EINVAL;

	if (!cptr->accept_output)
		result = -EPERM;
	else
		result = snd_seq_deliver_event(cptr, ev, atomic, hop);

	snd_seq_client_unlock(cptr);
	return result;
}


/*
 * exported, called by kernel clients to perform same functions as with
 * userland ioctl() 
 */
int snd_seq_kernel_client_ctl(int clientid, unsigned int cmd, void *arg)
{
	client_t *client;
	mm_segment_t fs;
	int result;

	client = clientptr(clientid);
	if (client == NULL)
		return -ENXIO;
	fs = snd_enter_user();
	result = snd_seq_do_ioctl(client, cmd, (unsigned long)arg);
	snd_leave_user(fs);
	return result;
}


/* exported (for OSS emulator) */
int snd_seq_kernel_client_write_poll(int clientid, struct file *file, poll_table *wait)
{
	client_t *client;

	client = clientptr(clientid);
	if (client == NULL)
		return -ENXIO;

	if (! snd_seq_write_pool_allocated(client))
		return 1;
	if (snd_seq_pool_poll_wait(client->pool, file, wait))
		return 1;
	return 0;
}

/*---------------------------------------------------------------------------*/

/*
 *  /proc interface
 */
static void snd_seq_info_dump_subscribers(snd_info_buffer_t * buffer, subscribers_group_t * group)
{
	subscribers_t *s;

	snd_seq_subscribers_lock(group);
	for (s = group->list; s; s = s->next) {
		snd_iprintf(buffer, "%d:%d",
				s->addr.client,
				s->addr.port);
		if (s->info.flags & SNDRV_SEQ_PORT_SUBS_TIMESTAMP)
			snd_iprintf(buffer, "[%c:%d]", ((s->info.flags & SNDRV_SEQ_PORT_SUBS_TIME_REAL) ? 'r' : 't'), s->info.queue);
		if (group->exclusive)
			snd_iprintf(buffer, "[ex]");
		if (s->next)
			snd_iprintf(buffer, ", ");
	}
	snd_seq_subscribers_unlock(group);
}

#define FLAG_PERM_RD(perm) ((perm) & SNDRV_SEQ_PORT_CAP_READ ? ((perm) & SNDRV_SEQ_PORT_CAP_SUBS_READ ? 'R' : 'r') : '-')
#define FLAG_PERM_WR(perm) ((perm) & SNDRV_SEQ_PORT_CAP_WRITE ? ((perm) & SNDRV_SEQ_PORT_CAP_SUBS_WRITE ? 'W' : 'w') : '-')
#define FLAG_PERM_EX(perm) ((perm) & SNDRV_SEQ_PORT_CAP_NO_EXPORT ? '-' : 'e')

#define FLAG_PERM_DUPLEX(perm) ((perm) & SNDRV_SEQ_PORT_CAP_DUPLEX ? 'X' : '-')

static void snd_seq_info_dump_ports(snd_info_buffer_t * buffer, client_port_t * ports)
{
	client_port_t *p = ports;

	while (p) {
		snd_iprintf(buffer, "  Port %3d : \"%s\" (%c%c%c%c)\n",
			    p->port, p->name,
			    FLAG_PERM_RD(p->capability),
			    FLAG_PERM_WR(p->capability),
			    FLAG_PERM_EX(p->capability),
			    FLAG_PERM_DUPLEX(p->capability));
		if (p->export.list) {
			snd_iprintf(buffer, "    Connecting To: ");
			snd_seq_info_dump_subscribers(buffer, &p->export);
			snd_iprintf(buffer, "\n");
		}
		if (p->import.list) {
			snd_iprintf(buffer, "    Connected From: ");
			snd_seq_info_dump_subscribers(buffer, &p->import);
			snd_iprintf(buffer, "\n");
		}

		p = p->next;
	}
}


/* exported to seq_info.c */
void snd_seq_info_clients_read(snd_info_entry_t *entry, 
			       snd_info_buffer_t * buffer)
{
	extern void snd_seq_info_pool(snd_info_buffer_t * buffer, pool_t * pool, char *space);
	int c;
	client_t *client;

	snd_iprintf(buffer, "Client info\n");
	snd_iprintf(buffer, "  cur  clients : %d\n", client_usage.cur);
	snd_iprintf(buffer, "  peak clients : %d\n", client_usage.peak);
	snd_iprintf(buffer, "  max  clients : %d\n", SNDRV_SEQ_MAX_CLIENTS);
	snd_iprintf(buffer, "\n");

	/* list the client table */
	for (c = 0; c < SNDRV_SEQ_MAX_CLIENTS; c++) {
		client = snd_seq_client_use_ptr(c);
		if (client == NULL)
			continue;
		snd_seq_ports_lock(client);
		if (client->type == NO_CLIENT)
			continue;

		snd_iprintf(buffer, "Client %3d : \"%s\" [%s]\n",
			    c, client->name,
			    client->type == USER_CLIENT ? "User" : "Kernel");
		snd_seq_info_dump_ports(buffer, client->ports);
		if (snd_seq_write_pool_allocated(client)) {
			snd_iprintf(buffer, "  Output pool :\n");
			snd_seq_info_pool(buffer, client->pool, "    ");
		}
		if (client->type == USER_CLIENT && client->data.user.fifo &&
		    client->data.user.fifo->pool) {
			snd_iprintf(buffer, "  Input pool :\n");
			snd_seq_info_pool(buffer, client->data.user.fifo->pool, "    ");
		}
		snd_seq_ports_unlock(client);
		snd_seq_client_unlock(client);
	}
}


/*---------------------------------------------------------------------------*/


/*
 *  REGISTRATION PART
 */
#ifdef TARGET_OS2
static struct file_operations snd_seq_f_ops =
{
#ifdef LINUX_2_3
	owner:		THIS_MODULE,
#endif
        0,
	snd_seq_read,
	snd_seq_write,
        0,
	snd_seq_poll,
	snd_seq_ioctl,
        0,
	snd_seq_open,
        0,
	snd_seq_release,
        0,0,0,0,0
};

static snd_minor_t snd_seq_reg =
{
        {0,0},
        0,0,
	"sequencer",0,
	&snd_seq_f_ops,
};
#else
static struct file_operations snd_seq_f_ops =
{
#ifdef LINUX_2_3
	owner:		THIS_MODULE,
#endif
	read:		snd_seq_read,
	write:		snd_seq_write,
	open:		snd_seq_open,
	release:	snd_seq_release,
	poll:		snd_seq_poll,
	ioctl:		snd_seq_ioctl,
};

static snd_minor_t snd_seq_reg =
{
	comment:	"sequencer",
	f_ops:		&snd_seq_f_ops,
};
#endif

/* 
 * register sequencer device 
 */
int __init snd_sequencer_device_init(void)
{
	int err;

	if (down_interruptible(&register_mutex))
		return -ERESTARTSYS;

	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_SEQUENCER, NULL, 0, &snd_seq_reg, "seq")) < 0) {
		up(&register_mutex);
		return err;
	}
	
	up(&register_mutex);

	return 0;
}



/* 
 * unregister sequencer device 
 */
void __exit snd_sequencer_device_done(void)
{
	snd_unregister_device(SNDRV_DEVICE_TYPE_SEQUENCER, NULL, 0);
}
