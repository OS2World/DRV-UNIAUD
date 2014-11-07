/* $Id: timer.c,v 1.5 2002/04/21 14:00:09 sandervl Exp $ */
/*
 * OS/2 implementation of Linux timer kernel functions
 *
 * (C) 2000-2002 InnoTek Systemberatung GmbH
 * (C) 2000-2001 Sander van Leeuwen (sandervl@xs4all.nl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include "linux.h"
#include <linux/init.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>
#include <asm/io.h>
#include <linux/time.h>

#define LINUX
#include <ossidc.h>
#include <irqos2.h>

void do_gettimeofday(struct timeval *tv)
{
    tv->tv_sec  = 0; //os2gettimesec();
    tv->tv_usec = os2gettimemsec() * 1000;
}

void add_timer(struct timer_list * timer)
{

}

int  del_timer(struct timer_list * timer)
{
  return 0;
}

/*
 * mod_timer is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 * mod_timer(a,b) is equivalent to del_timer(a); a->expires = b; add_timer(a)
 */
void mod_timer(struct timer_list *timer, unsigned long expires)
{

}

