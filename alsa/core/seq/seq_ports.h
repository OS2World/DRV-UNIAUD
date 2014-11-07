/*
 *   ALSA sequencer Ports 
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
#ifndef __SND_SEQ_PORTS_H
#define __SND_SEQ_PORTS_H

#include <sound/seq_kernel.h>
#include "seq_lock.h"

/* list of 'exported' ports */

/* Client ports that are not exported are still accessible, but are
 anonymous ports. 
 
 If a port supports SUBSCRIPTION, that port can send events to all
 subscribersto a special address, with address
 (queue==SNDRV_SEQ_ADDRESS_SUBSCRIBERS). The message is then send to all
 recipients that are registered in the subscription list. A typical
 application for these SUBSCRIPTION events is handling of incoming MIDI
 data. The port doesn't 'know' what other clients are interested in this
 message. If for instance a MIDI recording application would like to receive
 the events from that port, it will first have to subscribe with that port.
 
*/

typedef struct subscribers_t {
	struct subscribers_t *next;	/* ptr to next subscription entry */
	snd_seq_addr_t addr;		/* subscription data */
	snd_seq_port_subscribe_t info;	/* additional info */
} subscribers_t;

typedef struct subscribers_group_t {
	subscribers_t *list;		/* list of subscribed ports */
	unsigned int count;		/* count of subscribers */
	unsigned int exclusive: 1;	/* exclusive mode */
	rwlock_t use_lock;
} subscribers_group_t;

typedef struct client_port_t {
	struct client_port_t *next;	/* ptr to next port definition */

	struct module *owner;		/* owner of this port */

	int  port;			/* port number */
	char name[64];			/* port name */	

	/* subscribers */
	subscribers_group_t export;	/* read (export) group */
	subscribers_group_t import;	/* write (import) group */

	snd_seq_kernel_port_subscribe_t *subscribe;
	snd_seq_kernel_port_unsubscribe_t *unsubscribe;
	unsigned int subscribe_count;
	struct semaphore subscribe_mutex;
	snd_seq_kernel_port_use_t *use;
	snd_seq_kernel_port_unuse_t *unuse;
	unsigned int use_count;
	struct semaphore use_mutex;
	snd_seq_kernel_port_input_t *event_input;
	snd_seq_kernel_port_private_free_t *private_free;
	void *private_data;
	unsigned int callback_all;
	
	/* capability, inport, output, sync */
	unsigned int capability;	/* port capability bits */
	unsigned int type;		/* port type bits */

	/* supported channels */
	int midi_channels;
	int synth_voices;
		
} client_port_t;

/* lock ports - to prevent create/delete operations */
#define snd_seq_ports_lock(client) read_lock(&(client)->ports_lock)
#define snd_seq_ports_unlock(client) read_unlock(&(client)->ports_lock)

/* return pointer to port structure and lock ports */
extern client_port_t *snd_seq_port_use_ptr(client_t *client, int num);

/* search for next port - ports are locked if found */
extern client_port_t *snd_seq_port_query_nearest(client_t *client, snd_seq_port_info_t *pinfo);

/* create a port, port number is returned (-1 on failure) */
extern client_port_t *snd_seq_create_port(client_t *client, int port_index);

/* delete a port */
extern int snd_seq_delete_port(client_t *client, int port);

/* delete all ports */
extern int snd_seq_delete_ports(client_t *client);

/* set port info fields */
extern int snd_seq_set_port_info(client_port_t *port, snd_seq_port_info_t *info);

/* get port info fields */
extern int snd_seq_get_port_info(client_port_t *port, snd_seq_port_info_t *info);

/* lock ports - to prevent add/remove operations */
#define snd_seq_subscribers_lock(port) read_lock(&(port)->use_lock)
#define snd_seq_subscribers_unlock(port) read_unlock(&(port)->use_lock)

/* add subscriber to subscription list */
extern int snd_seq_port_add_subscriber(subscribers_group_t *group, snd_seq_addr_t *dest, snd_seq_port_subscribe_t *subs, int exclusive);

/* remove subscriber from subscription list */ 
extern int snd_seq_port_remove_subscriber(subscribers_group_t *group, snd_seq_addr_t *dest, snd_seq_port_subscribe_t *subs);

/* subscribe port */
int snd_seq_port_subscribe(client_port_t *port, snd_seq_port_subscribe_t *info);

/* unsubscribe port */
int snd_seq_port_unsubscribe(client_port_t *port, snd_seq_port_subscribe_t *info);

/* use port */
int snd_seq_port_use(client_port_t *port, snd_seq_port_subscribe_t *info);

/* unuse port */
int snd_seq_port_unuse(client_port_t *port, snd_seq_port_subscribe_t *info);

/* check if the address was already subscribed */
int snd_seq_port_is_subscribed(subscribers_group_t *group, snd_seq_addr_t *addr);

/* get matched subscriber */
subscribers_t *snd_seq_port_get_subscription(subscribers_group_t *group, snd_seq_addr_t *addr);

#endif
