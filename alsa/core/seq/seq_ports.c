/*
 *   ALSA sequencer Ports
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@home.nl>
 *                         Jaroslav Kysela <perex@suse.cz>
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
#include "seq_system.h"
#include "seq_ports.h"
#include "seq_clientmgr.h"

/*

   registration of client ports

 */


/* 

NOTE: the current implementation of the port structure as a linked list is
not optimal for clients that have many ports. For sending messages to all
subscribers of a port we first need to find the address of the port
structure, which means we have to traverse the list. A direct access table
(array) would be better, but big preallocated arrays waste memory.

Possible actions:

1) leave it this way, a client does normaly does not have more than a few
ports

2) replace the linked list of ports by a array of pointers which is
dynamicly kmalloced. When a port is added or deleted we can simply allocate
a new array, copy the corresponding pointers, and delete the old one. We
then only need a pointer to this array, and an integer that tells us how
much elements are in array.

*/

/*
 NOTE 2: there are now only two subscribers group lists, import and
 export, which correspond to input from other ports and output to other
 ports, respectively.  The "tracking" groups in the former version do
 no longer exist.  The subscription is always managed as a simple
 connection either from another to this port or vice versa.
						-- iwai
 */


/* return pointer to port structure - ports are locked if found */
client_port_t *snd_seq_port_use_ptr(client_t *client, int num)
{
	client_port_t *p;

	if (client == NULL)
		return NULL;
	snd_seq_ports_lock(client);
	for (p = client->ports; p; p = p->next) {
		if (p->port == num)
			return p;
	}
	snd_seq_ports_unlock(client);
	return NULL;		/* not found */
}


/* search for next port - ports are locked if found */
client_port_t *snd_seq_port_query_nearest(client_t *client, snd_seq_port_info_t *pinfo)
{
	int num;
	client_port_t *p, *found;

	num = pinfo->addr.port;
	found = NULL;
	snd_seq_ports_lock(client);
	for (p = client->ports; p; p = p->next) {
		if (p->port < num)
			continue;
		if (p->port == num || found == NULL || p->port < found->port) {
			if (p->port == num)
				return p;
			found = p;
		}
	}
	if (found)
		return found;
	snd_seq_ports_unlock(client);
	return NULL;		/* not found */
}


/* create a port, port number is returned (-1 on failure) */
client_port_t *snd_seq_create_port(client_t *client, int port)
{
	unsigned long flags;
	client_port_t *new_port, *p, *prev;
	int num = -1;
	
	/* sanity check */
	if (client == NULL) {
		snd_printd("oops: snd_seq_create_port() got NULL client\n");
		return NULL;
	}

	if (client->num_ports >= SNDRV_SEQ_MAX_PORTS - 1) {
		snd_printk("too many ports for client %d\n", client->number);
		return NULL;
	}

	/* create a new port */
	new_port = snd_kcalloc(sizeof(client_port_t), GFP_KERNEL);
	if (new_port) {
		new_port->port = -1;
		new_port->next = NULL;
	} else {
		snd_printd("malloc failed for registering client port\n");
		return NULL;	/* failure, out of memory */
	}

	/* init port data */
	new_port->owner = THIS_MODULE;
	sprintf(new_port->name, "port-%d", num);
	rwlock_init(&new_port->import.use_lock);
	rwlock_init(&new_port->export.use_lock);
	init_MUTEX(&new_port->subscribe_mutex);
	init_MUTEX(&new_port->use_mutex);

	/* wait if we can do some operation - spinlock is enabled!! */
	write_lock_irqsave(&client->ports_lock, flags);

	/* add the port to the list of registed ports for this client */
	p = client->ports;
	prev = NULL;
	num = port >= 0 ? port : 0;
	for (; p; prev = p, p = p->next) {
		if (p->port > num)
			break;
		if (port < 0) /* auto-probe mode */
			num = p->port + 1;
	}

	/* insert the new port */
	new_port->next = p;
	if (prev)
		prev->next = new_port;
	else
		client->ports = new_port;
	client->num_ports++;
	new_port->port = num;	/* store the port number in the port */
	sprintf(new_port->name, "port-%d", num);
	write_unlock_irqrestore(&client->ports_lock, flags);

	return new_port;
}

/* */
enum group_type_t {
	GROUP_TYPE_IMPORT,
	GROUP_TYPE_EXPORT
};

/* clear subscription list */
static void clear_subscribers(client_t *lclient, client_port_t *lport, subscribers_group_t *group, int grptype)
{
	snd_seq_port_subscribe_t subs;
	snd_seq_addr_t raddr, laddr;
	unsigned long flags;
	subscribers_t *s;
	client_t *rclient;
	client_port_t *rport;

	/* address of the local removed port (queue is overwritten later) */
	laddr.client = lclient->number;
	laddr.port = lport->port;

	/* remove each subscriber */
	for (;;) {
		/* retrieve a subscriber */
		write_lock_irqsave(&group->use_lock, flags);
		s = group->list;
		if (s) {
			group->list = s->next;
			group->count--;
		}
		write_unlock_irqrestore(&group->use_lock, flags);
		if (s == NULL)
			break;

		/* address of the remote removed port (queue is overwritten later) */
		raddr.client = s->addr.client;
		raddr.port = s->addr.port;
		
		/* find the remote client/port pair */
		if ((rclient = snd_seq_client_use_ptr(raddr.client)) == NULL) {
			snd_printd("Oops, raddr.client %i was not found\n", raddr.client);
			goto __skip;
		}
		if ((rport = snd_seq_port_use_ptr(rclient, raddr.port)) == NULL) {
			snd_printd("Oops, raddr.port %i was not found\n", raddr.port);
			goto __skip;
		}

		/* we must remove both connected sides here */
		subs = s->info;
		switch (grptype) {
		case GROUP_TYPE_IMPORT:
			snd_seq_port_remove_subscriber(&lport->import, &raddr, &subs);
			snd_seq_port_remove_subscriber(&rport->export, &laddr, &subs);
			snd_seq_port_unuse(lport, &subs);
			if (raddr.client != laddr.client && lclient->type == USER_CLIENT)
				snd_seq_client_notify_subscription(laddr.client, laddr.port, &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
			snd_seq_port_unsubscribe(rport, &subs);
			if (raddr.client != laddr.client && rclient->type == USER_CLIENT)
				snd_seq_client_notify_subscription(raddr.client, raddr.port, &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
			snd_seq_client_notify_subscription(SNDRV_SEQ_ADDRESS_SUBSCRIBERS, 0, &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
			break;
		case GROUP_TYPE_EXPORT:
			snd_seq_port_remove_subscriber(&lport->export, &raddr, &subs);
			snd_seq_port_remove_subscriber(&rport->import, &laddr, &subs);
			snd_seq_port_unsubscribe(lport, &subs);
			if (raddr.client != laddr.client && lclient->type == USER_CLIENT)
				snd_seq_client_notify_subscription(laddr.client, laddr.port, &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
			snd_seq_port_unuse(rport, &subs);
			if (raddr.client != laddr.client && rclient->type == USER_CLIENT)
				snd_seq_client_notify_subscription(raddr.client, raddr.port, &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
			snd_seq_client_notify_subscription(SNDRV_SEQ_ADDRESS_SUBSCRIBERS, 0, &subs, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
			break;
		}

	__skip:
		snd_seq_ports_unlock(rclient);
		snd_seq_client_unlock(rclient);
		kfree(s); /* delete instance */
	}
}
	

/* delete port data */
static int snd_seq_port_delete(client_t *client, client_port_t *port)
{
	/* sanity check */
	if (client == NULL || port == NULL) {
		snd_printd("oops: port_delete() got NULL\n");
		return -EINVAL;
	}

	/* clear subscribers info */
	clear_subscribers(client, port, &port->import, GROUP_TYPE_IMPORT);
	clear_subscribers(client, port, &port->export, GROUP_TYPE_EXPORT);

	if (port->private_free)
		port->private_free(port->private_data);

	snd_assert(port->subscribe_count == 0,);
	snd_assert(port->use_count == 0,);

	kfree(port);
	return 0;
}


/* delete a port from port structure */
int snd_seq_delete_port(client_t *client, int port)
{
	unsigned long flags;
	client_port_t *p, *prev;

	write_lock_irqsave(&client->ports_lock, flags);
	if ((p = client->ports) != NULL) {
		prev = NULL;
		for (; p; prev = p, p = p->next) {
			if (p->port == port) {
				if (prev)
					prev->next = p->next;
				else
					client->ports = p->next;
				client->num_ports--;
				write_unlock_irqrestore(&client->ports_lock, flags);
				return snd_seq_port_delete(client, p);
			}
		}
	}
	write_unlock_irqrestore(&client->ports_lock, flags);
	return -ENOENT; 	/* error, port not found */
}

/* delete whole port list */
int snd_seq_delete_ports(client_t *client)
{
	unsigned long flags;
	client_port_t *p, *next;
	
	snd_assert(client != NULL, return -EINVAL);
	write_lock_irqsave(&client->ports_lock, flags);
	p = client->ports;
	client->ports = NULL;
	client->num_ports = 0;
	write_unlock_irqrestore(&client->ports_lock, flags);
	while (p != NULL) {
		next = p->next;
		snd_seq_port_delete(client, p);
		snd_seq_system_client_ev_port_exit(client->number, p->port);
		p = next;
	}
	return 0;
}

/* set port info fields */
int snd_seq_set_port_info(client_port_t * port, snd_seq_port_info_t * info)
{
	if ((port == NULL) || (info == NULL))
		return -1;

	/* set port name */
	if (info->name[0]) {
		strncpy(port->name, info->name, sizeof(port->name)-1);
		port->name[sizeof(port->name)-1] = '\0';
	}
	
	/* set capabilities */
	port->capability = info->capability;
	
	/* get port type */
	port->type = info->type;

	/* information about supported channels/voices */
	port->midi_channels = info->midi_channels;
	port->synth_voices = info->synth_voices;

	return 0;
}

/* get port info fields */
int snd_seq_get_port_info(client_port_t * port, snd_seq_port_info_t * info)
{
	if ((port == NULL) || (info == NULL))
		return -1;

	/* get port name */
	strncpy(info->name, port->name, sizeof(info->name));
	
	/* get capabilities */
	info->capability = port->capability;

	/* get port type */
	info->type = port->type;

	/* information about supported channels/voices */
	info->midi_channels = port->midi_channels;
	info->synth_voices = port->synth_voices;

	/* get subscriber counts */
	info->read_use = port->subscribe_count;
	info->write_use = port->use_count;
	
	return 0;
}


/* add subscriber to subscription list */
int snd_seq_port_add_subscriber(subscribers_group_t *group, snd_seq_addr_t *addr, snd_seq_port_subscribe_t *subs, int exclusive)
{
	unsigned long flags;
	subscribers_t *n, *s;

	snd_assert(group != NULL && addr != NULL, return -EINVAL);
	n = snd_kcalloc(sizeof(subscribers_t), GFP_KERNEL);
	if (n == NULL)
		return -ENOMEM;
	n->addr = *addr;
	n->info = *subs;
	write_lock_irqsave(&group->use_lock, flags);
	s = group->list;
	if (s == NULL) {
		/* first subscriber */
		group->list = n;
		group->exclusive = exclusive ? 1 : 0;
	} else {
		if (exclusive) {
			write_unlock_irqrestore(&group->use_lock, flags);
			kfree(n);
			return -EBUSY;
		}
		/* add to the end of the list */
		while (s->next) {
			s = s->next;
		}
		s->next = n;
	}
	group->count++;
	write_unlock_irqrestore(&group->use_lock, flags);
	return 0;
}


static inline int addr_compare(snd_seq_addr_t *r, snd_seq_addr_t *s)
{
	return (r->client == s->client) &&
	       (r->port == s->port);
}


/* 
 * remove subscriber from subscription list 
 * both address field and queue must match with the existing subscription
 * list.
 */ 
int snd_seq_port_remove_subscriber(subscribers_group_t *group, snd_seq_addr_t *addr, snd_seq_port_subscribe_t *subs)
{
	unsigned long flags;
	subscribers_t *s, *p;

	write_lock_irqsave(&group->use_lock, flags);
	p = NULL;
	for (s = group->list; s; p = s, s = s->next) {
		if (addr_compare(&s->addr, addr) && s->info.queue == subs->queue) {
			if (p)
				p->next = s->next;
			else
				group->list = s->next;
			if (group->exclusive)
				group->exclusive = 0;
			group->count--;
			write_unlock_irqrestore(&group->use_lock, flags);
			kfree(s);
			return 0;

		}
	}
	write_unlock_irqrestore(&group->use_lock, flags);
	return -ENOENT;
}


/* get matched subscriber - must unlock after use it! */
subscribers_t *snd_seq_port_get_subscription(subscribers_group_t *group,
					     snd_seq_addr_t *addr)
{
	subscribers_t *s;

	snd_seq_subscribers_lock(group);
	for (s = group->list; s; s = s->next)
		if (addr_compare(&s->addr, addr)) {
			return s;
		}
	snd_seq_subscribers_unlock(group);
	return NULL;
}


/* check if the address was already subscribed */
int snd_seq_port_is_subscribed(subscribers_group_t *group, snd_seq_addr_t *addr)
{
	if (snd_seq_port_get_subscription(group, addr)) {
		snd_seq_subscribers_unlock(group);
		return 1;
	}
	return 0;
}

/*
 * call callback functions (if any):
 * the callbacks are invoked only when the first (for connection) or
 * the last subscription (for disconnection) is done.  Second or later
 * subscription results in increment of counter, but no callback is
 * invoked.
 * This feature is useful if these callbacks are associated with
 * initialization or termination of devices (see seq_midi.c).
 *
 * New: If callback_all option is set, the callback function is invoked
 * at each connnection/disconnection.
 */

/* subscribe port */
int snd_seq_port_subscribe(client_port_t *port, snd_seq_port_subscribe_t *info)
{
	int result = 0;

	snd_assert(port->owner, return -EFAULT);
	down(&port->subscribe_mutex);
	port->subscribe_count++;
	if (!try_inc_mod_count(port->owner)) {
		port->subscribe_count--;
		result = -EFAULT;
		goto __error;
	}
	if (port->subscribe && (port->callback_all || port->subscribe_count == 1)) {
		result = port->subscribe(port->private_data, info);
		if (result < 0) {
			port->subscribe_count--;
			dec_mod_count(port->owner);
			goto __error;
		}
	}
      __error:
	up(&port->subscribe_mutex);
	return result;
}

/* unsubscribe port */
int snd_seq_port_unsubscribe(client_port_t *port, snd_seq_port_subscribe_t *info)
{
	int result = 0;

	snd_assert(port->owner, return -EFAULT);
	down(&port->subscribe_mutex);
	if (port->subscribe_count > 0) {
		port->subscribe_count--;
		if (port->unsubscribe && (port->callback_all || port->subscribe_count == 0))
			result = port->unsubscribe(port->private_data, info);
		dec_mod_count(port->owner);
	}
	up(&port->subscribe_mutex);
	return result;
}

/* use port */
int snd_seq_port_use(client_port_t *port, snd_seq_port_subscribe_t *info)
{
	int result = 0;

	snd_assert(port->owner, return -EFAULT);
	down(&port->use_mutex);
	port->use_count++;
	if (info->sender.client != SNDRV_SEQ_CLIENT_SYSTEM &&
	    !try_inc_mod_count(port->owner)) {
		port->use_count--;
		result = -EFAULT;
		goto __error;
	}
	if (port->use && (port->callback_all || port->use_count == 1)) {
		result = port->use(port->private_data, info);
		if (result < 0) {
			port->use_count--;
			if (info->sender.client != SNDRV_SEQ_CLIENT_SYSTEM)
				dec_mod_count(port->owner);
		}
	}
      __error:
	up(&port->use_mutex);
	return result;
}

/* unuse port */
int snd_seq_port_unuse(client_port_t *port, snd_seq_port_subscribe_t *info)
{
	int result = 0;

	snd_assert(port->owner, return -EFAULT);
	down(&port->use_mutex);
	if (port->use_count > 0) {
		port->use_count--;
		if (port->unuse && (port->callback_all || port->use_count == 0))
			result = port->unuse(port->private_data, info);
		if (info->sender.client != SNDRV_SEQ_CLIENT_SYSTEM)
			dec_mod_count(port->owner);
	}
	up(&port->use_mutex);
	return result;
}

/*
 * Initialise a port callback structure.
 */
void snd_port_init_callback(snd_seq_port_callback_t *p)
{
	if (p == NULL)
		return;
	p->use = NULL;
	p->unuse = NULL;
	p->subscribe = NULL;
	p->unsubscribe = NULL;
	p->event_input = NULL;
	p->private_data = NULL;
	p->private_free = NULL;
	p->callback_all = 0;
}

/*
 * Allocate and initialise a port callback
 */
snd_seq_port_callback_t * snd_port_alloc_callback(void)
{
	snd_seq_port_callback_t *p;

	p = kmalloc(sizeof(snd_seq_port_callback_t), GFP_KERNEL);
	snd_port_init_callback(p);

	return p;
}

/*
 * Attach a device driver that wants to receive events from the
 * sequencer.  Returns the new port number on success.
 * A driver that wants to receive the events converted to midi, will
 * use snd_seq_midisynth_register_port().
 */
int snd_seq_event_port_attach(int client,
			      snd_seq_port_callback_t *pcbp,
			      int cap,
			      int type,
			      char *portname)
{
	snd_seq_port_info_t portinfo;
	int  ret;

	/* Set up the port */
	memset(&portinfo, 0, sizeof(portinfo));
	portinfo.addr.client = client;
	if (portname)
		strncpy(portinfo.name, portname, sizeof(portinfo.name));
	else
		sprintf(portinfo.name, "Unamed port");

	portinfo.capability = cap;
	portinfo.type = type;
	portinfo.kernel = pcbp;

	/* Create it */
	ret = snd_seq_kernel_client_ctl(client,
					SNDRV_SEQ_IOCTL_CREATE_PORT,
					&portinfo);

	if (ret >= 0)
		ret = portinfo.addr.port;

	return ret;
}


/*
 * Detach the driver from a port.
 */
int snd_seq_event_port_detach(int client, int port)
{
	snd_seq_port_info_t portinfo;
	int  err;

	memset(&portinfo, 0, sizeof(portinfo));
	portinfo.addr.client = client;
	portinfo.addr.port   = port;
	err = snd_seq_kernel_client_ctl(client,
					SNDRV_SEQ_IOCTL_DELETE_PORT,
					&portinfo);

	return err;
}
