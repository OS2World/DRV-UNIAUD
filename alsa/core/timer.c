/*
 *  Timers abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <sound/timer.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/minors.h>
#include <sound/initval.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

int snd_timer_limit = 1;
MODULE_DESCRIPTION("ALSA timer interface");
MODULE_CLASSES("{sound}");
MODULE_PARM(snd_timer_limit, "i");
MODULE_PARM_DESC(snd_timer_limit, "Maximum global timers in system. (1 by default)");

typedef struct {
	snd_timer_instance_t *timeri;
	unsigned long ticks;
	unsigned long overrun;
	int qhead;
	int qtail;
	int qused;
	int queue_size;
	snd_timer_read_t *queue;
	spinlock_t qlock;
	wait_queue_head_t qchange_sleep;
} snd_timer_user_t;

snd_timer_t *snd_timer_devices = NULL;
snd_timer_instance_t *snd_timer_slave_devices = NULL;

static atomic_t snd_timer_slave_in_use = ATOMIC_INIT(0);
static spinlock_t snd_timer_slave = SPIN_LOCK_UNLOCKED;
static DECLARE_MUTEX(register_mutex);

static int snd_timer_free(snd_timer_t *timer);
static int snd_timer_dev_free(snd_device_t *device);
static int snd_timer_dev_register(snd_device_t *device);
static int snd_timer_dev_unregister(snd_device_t *device);

static void snd_timer_add_slaves(snd_timer_instance_t * timeri);
static void snd_timer_reschedule(snd_timer_t * timer, unsigned long ticks_left);

/*
 *
 */

static snd_timer_instance_t *__snd_timer_open(char *owner, snd_timer_t *timer)
{
	snd_timer_instance_t *timeri;
	unsigned long flags;
	
	if (timer == NULL)
		return NULL;
	timeri = snd_kcalloc(sizeof(*timeri), GFP_KERNEL);
	if (timeri == NULL)
		return NULL;
	if (timer->card && !try_inc_mod_count(timer->card->module)) {
		kfree(timeri);
		return NULL;
	}
	timeri->timer = timer;
	timeri->owner = snd_kmalloc_strdup(owner, GFP_KERNEL);
	spin_lock_irqsave(&timer->lock, flags);
	if (timer->first != NULL) {
		timeri->next = timer->first;
	} else {
		if (timer->hw.open)
			timer->hw.open(timer);
	}
	timer->first = timeri;
	spin_unlock_irqrestore(&timer->lock, flags);
	return timeri;
}

static snd_timer_t *snd_timer_find(snd_timer_id_t *tid)
{
	snd_timer_t *timer;

	for (timer = snd_timer_devices; timer != NULL; timer = timer->next) {
		if (timer->tmr_class != tid->dev_class)
			continue;
		if ((timer->tmr_class == SNDRV_TIMER_CLASS_CARD ||
		     timer->tmr_class == SNDRV_TIMER_CLASS_PCM) &&
		    (timer->card == NULL ||
		     timer->card->number != tid->card))
			continue;
		if (timer->tmr_device != tid->device)
			continue;
		if (timer->tmr_subdevice != tid->subdevice)
			continue;
		break;
	}
	return timer;
}

#ifdef CONFIG_KMOD

static void snd_timer_request(snd_timer_id_t *tid)
{
	char str[32];
	
	switch (tid->dev_class) {
	case SNDRV_TIMER_CLASS_GLOBAL:
		sprintf(str, "snd-timer-%i", tid->card);
		break;
	case SNDRV_TIMER_CLASS_CARD:
	case SNDRV_TIMER_CLASS_PCM:
		sprintf(str, "snd-card-%i", tid->card);
		break;
	default:
		return;
	}
	request_module(str);
}

#endif

static snd_timer_instance_t *snd_timer_open_slave(char *owner, snd_timer_id_t *tid)
{
	snd_timer_instance_t *timeri;
	unsigned long flags;
	
	if (tid->dev_sclass <= SNDRV_TIMER_SCLASS_NONE ||
	    tid->dev_sclass > SNDRV_TIMER_SCLASS_OSS_SEQUENCER) {
	    	snd_printd("invalid slave class %i\n", tid->dev_sclass);
		return NULL;
	}
	timeri = snd_kcalloc(sizeof(snd_timer_instance_t), GFP_KERNEL);
	if (timeri == NULL)
		return NULL;
	timeri->owner = snd_kmalloc_strdup(owner, GFP_KERNEL);
	timeri->slave_class = tid->dev_sclass;
	timeri->slave_id = tid->device;
	spin_lock_irqsave(&snd_timer_slave, flags);
	timeri->flags |= SNDRV_TIMER_IFLG_SLAVE;
	timeri->next = snd_timer_slave_devices;
	snd_timer_slave_devices = timeri;
	spin_unlock_irqrestore(&snd_timer_slave, flags);
	return timeri;
}

snd_timer_instance_t *snd_timer_open(char *owner, snd_timer_id_t *tid)
{
	snd_timer_t *timer;
	snd_timer_instance_t *timeri = NULL;
	
	if (tid->dev_class == SNDRV_TIMER_CLASS_SLAVE)
		return snd_timer_open_slave(owner, tid);
	down(&register_mutex);
	timer = snd_timer_find(tid);
#ifdef CONFIG_KMOD
	if (timer == NULL) {
		up(&register_mutex);
		snd_timer_request(tid);
		down(&register_mutex);
		timer = snd_timer_find(tid);
	}
#endif
	if (timer)
		timeri = __snd_timer_open(owner, timer);
	up(&register_mutex);
	return timeri;
}

int snd_timer_close(snd_timer_instance_t * timeri)
{
	snd_timer_t *timer = NULL;
	snd_timer_instance_t *timeri1;
	unsigned long flags;

	snd_assert(timeri != NULL, return -ENXIO);
	snd_timer_stop(timeri);
	if (!(timeri->flags & SNDRV_TIMER_IFLG_SLAVE)) {
		if ((timer = timeri->timer) == NULL)
			return -EINVAL;
		spin_lock_irqsave(&timer->lock, flags);
		while (atomic_read(&timer->in_use)) {
			spin_unlock_irqrestore(&timer->lock, flags);
			udelay(10);
			spin_lock_irqsave(&timer->lock, flags);
		}
		if ((timeri1 = timer->first) == timeri) {
			timer->first = timeri->next;
		} else {
			while (timeri1) {
				if (timeri1->next == timeri)
					break;
				timeri1 = timeri1->next;
			}
			if (timeri1 == NULL) {
				spin_unlock_irqrestore(&timer->lock, flags);
				return -ENXIO;
			}
			timeri1->next = timeri->next;
		}
		if (timer->first == NULL) {
			if (timer->hw.close)
				timer->hw.close(timer);
		}
		spin_unlock_irqrestore(&timer->lock, flags);
	} else {
		spin_lock_irqsave(&snd_timer_slave, flags);
		while (atomic_read(&snd_timer_slave_in_use)) {
			spin_unlock_irqrestore(&snd_timer_slave, flags);
			udelay(10);
			spin_lock_irqsave(&snd_timer_slave, flags);
		}
		if ((timeri1 = snd_timer_slave_devices) == timeri) {
			snd_timer_slave_devices = timeri->next;
		} else {
			while (timeri1) {
				if (timeri1->next == timeri)
					break;
				timeri1 = timeri1->next;
			}
			if (timeri1 == NULL) {
				spin_unlock_irqrestore(&snd_timer_slave, flags);
				return -ENXIO;
			}
			timeri1->next = timeri->next;
		}
		snd_timer_add_slaves(timeri->master);
		spin_unlock_irqrestore(&snd_timer_slave, flags);
	}
	if (timeri->private_free)
		timeri->private_free(timeri);
	kfree(timeri->owner);
	kfree(timeri);
	if (timer && timer->card)
		dec_mod_count(timer->card->module);
	return 0;
}

unsigned long snd_timer_resolution(snd_timer_instance_t * timeri)
{
	snd_timer_t * timer;

	if (timeri == NULL)
		return 0;
	if ((timer = timeri->timer) != NULL) {
		if (timer->hw.c_resolution)
			return timer->hw.c_resolution(timer);
		return timer->hw.resolution;
	}
	return 0;
}

static void snd_timer_add_slaves(snd_timer_instance_t * timeri)
{
	snd_timer_instance_t *ti;
	
	if (timeri == NULL)
		return;
	timeri->slave = NULL;
	for (ti = snd_timer_slave_devices; ti; ti = ti->next) {
		if (ti->slave_class != timeri->slave_class ||
		    ti->slave_id != timeri->slave_id ||
		    !(ti->flags & SNDRV_TIMER_IFLG_RUNNING))
			continue;
		ti->master = timeri;
		ti->slave = timeri->slave;
		timeri->slave = ti;
	}
}

int snd_timer_start(snd_timer_instance_t * timeri, unsigned int ticks)
{
	snd_timer_t *timer;
	int result = -EINVAL;
	unsigned long flags;

	if (timeri == NULL || ticks < 1)
		return result;
	if (!(timeri->flags & SNDRV_TIMER_IFLG_SLAVE)) {
		timer = timeri->timer;
		if (timer != NULL) {
			spin_lock_irqsave(&timer->lock, flags);
			timeri->ticks = timeri->cticks = ticks;
			if (timer->running) {
				timer->flags |= SNDRV_TIMER_FLG_RESCHED;
				timeri->flags |= SNDRV_TIMER_IFLG_START;
				result = 1;	/* delayed start */
			} else {
				timer->sticks = ticks;
				timer->hw.start(timer);
				timer->running++;
				timeri->flags |= SNDRV_TIMER_IFLG_RUNNING;
				result = 0;
			}
			spin_lock(&snd_timer_slave);
			snd_timer_add_slaves(timeri);
			spin_unlock(&snd_timer_slave);
			spin_unlock_irqrestore(&timer->lock, flags);
		}
	} else {
		spin_lock_irqsave(&snd_timer_slave, flags);
		timeri->flags |= SNDRV_TIMER_IFLG_RUNNING;
		snd_timer_add_slaves(timeri);
		spin_unlock_irqrestore(&snd_timer_slave, flags);
		result = 1;			/* delayed start */
	}
	return result;
}

int snd_timer_stop(snd_timer_instance_t * timeri)
{
	snd_timer_t *timer;
	unsigned long flags;

	snd_assert(timeri != NULL, return -ENXIO);
	if (!(timeri->flags & SNDRV_TIMER_IFLG_SLAVE)) {
		timer = timeri->timer;
		if (timer != NULL) {
			spin_lock_irqsave(&timer->lock, flags);
			if (timeri->flags & SNDRV_TIMER_IFLG_RUNNING) {
				timeri->flags &= ~SNDRV_TIMER_IFLG_RUNNING;
				if (!(--timer->running)) {
					timer->hw.stop(timer);
					if (timer->flags & SNDRV_TIMER_FLG_RESCHED) {
						timer->flags &= ~SNDRV_TIMER_FLG_RESCHED;
						snd_timer_reschedule(timer, 0);
						if (timer->flags & SNDRV_TIMER_FLG_CHANGE) {
							timer->flags &= ~SNDRV_TIMER_FLG_CHANGE;
							timer->hw.start(timer);
						}
					}
				}
			}
			spin_unlock_irqrestore(&timer->lock, flags);
			return 0;
		}
	} else {
		spin_lock_irqsave(&snd_timer_slave, flags);
		timeri->flags &= ~SNDRV_TIMER_IFLG_RUNNING;
		snd_timer_add_slaves(timeri);
		spin_unlock_irqrestore(&snd_timer_slave, flags);
		return 0;
	}
	return -EINVAL;
}

int snd_timer_continue(snd_timer_instance_t * timeri)
{
	snd_timer_t * timer;
	int result = -EINVAL;
	unsigned long flags;

	if (timeri == NULL)
		return result;
	if (timeri->flags & SNDRV_TIMER_IFLG_SLAVE)
		return snd_timer_start(timeri, 1);
	if ((timer = timeri->timer) != NULL) {
		spin_lock_irqsave(&timer->lock, flags);
		if (!(timeri->flags & SNDRV_TIMER_IFLG_RUNNING)) {
			if (!timeri->cticks)
				timeri->cticks = 1;
			if (!timer->running) {
				timer->hw.start(timer);
				timer->running++;
				timeri->flags |= SNDRV_TIMER_IFLG_RUNNING;
				result = 0;
			} else {
				timer->flags |= SNDRV_TIMER_FLG_RESCHED;
				timeri->flags |= SNDRV_TIMER_IFLG_START;
				result = 1;	/* delayed start */
			}
			spin_lock(&snd_timer_slave);
			snd_timer_add_slaves(timeri);
			spin_unlock(&snd_timer_slave);
		}
		spin_unlock_irqrestore(&timer->lock, flags);
	}
	return result;
}

static void snd_timer_reschedule(snd_timer_t * timer, unsigned long ticks_left)
{
	snd_timer_instance_t *ti;
	unsigned long ticks = ~0UL;

	ti = timer->first;
	if (ti == NULL) {
		timer->flags &= ~SNDRV_TIMER_FLG_RESCHED;
		return;
	}
	while (ti) {
		if (ti->flags & SNDRV_TIMER_IFLG_START) {
			ti->flags &= ~SNDRV_TIMER_IFLG_START;
			ti->flags |= SNDRV_TIMER_IFLG_RUNNING;
			timer->running++;
		}
		if (ti->flags & SNDRV_TIMER_IFLG_RUNNING) {
			ticks = ti->cticks;
			ti = ti->next;
			break;
		}
		ti = ti->next;
	}
	while (ti) {
		if (ti->flags & SNDRV_TIMER_IFLG_START) {
			ti->flags &= ~SNDRV_TIMER_IFLG_START;
			ti->flags |= SNDRV_TIMER_IFLG_RUNNING;
			timer->running++;
		}
		if (ti->flags & SNDRV_TIMER_IFLG_RUNNING) {
			if (ticks > ti->cticks)
				ticks = ti->cticks;
		}
		ti = ti->next;
	}
	if (ticks == ~0UL) {
		timer->flags &= ~SNDRV_TIMER_FLG_RESCHED;
		return;
	}		
	if (ticks > timer->hw.ticks)
		ticks = timer->hw.ticks;
	if (ticks_left != ticks)
		timer->flags |= SNDRV_TIMER_FLG_CHANGE;
	timer->sticks = ticks;
}

static inline int snd_timer_insert(snd_timer_instance_t *ts,
				   snd_timer_instance_t **tc,
				   snd_timer_instance_t *ti)
{
	int result = 1;

	/* remove timer from old queue (tick lost) */
	if (ti->iprev != NULL) {
		ti->iprev->inext = ti->inext;
		if (ti->inext)
			ti->inext->iprev = ti->iprev;
		ti->lost++;
		result = 0;	/* in_use is already increased */
	}
	/* add timer to next queue */
	if (ts->inext == NULL) {
		ts->inext = ti;
		ti->iprev = ts;
	} else {
		(*tc)->inext = ti;
		ti->iprev = *tc;
	}
	*tc = ti;
	ti->inext = NULL;
	return result;
}

void snd_timer_interrupt(snd_timer_t * timer, unsigned long ticks_left)
{
	snd_timer_instance_t *ti, *tc, *tm;
	snd_timer_instance_t ts;
	unsigned long resolution = 0, ticks;

	if (timer == NULL)
		return;
	ts.iprev = NULL;
	ts.inext = NULL;
	spin_lock(&timer->lock);
	ti = timer->first;
	while (ti) {
		if (ti->flags & SNDRV_TIMER_IFLG_RUNNING) {
			if (ti->cticks < ticks_left) {
				ti->cticks = 0;
			} else {
				ti->cticks -= ticks_left;
			}
			if (!ti->cticks) {
				if (ti->flags & SNDRV_TIMER_IFLG_AUTO) {
					ti->cticks = ti->ticks;
				} else {
					ti->flags &= ~SNDRV_TIMER_IFLG_RUNNING;
					timer->running--;
				}
				if (snd_timer_insert(&ts, &tc, ti))
					atomic_inc(&timer->in_use);
				spin_lock(&snd_timer_slave);
				tm = ti->slave;
				while (tm) {
					tm->ticks = ti->ticks;
					if (snd_timer_insert(&ts, &tc, tm))
						atomic_inc(&snd_timer_slave_in_use);
					tm = tm->slave;
				}				
				spin_unlock(&snd_timer_slave);
			}
		}
		ti = ti->next;
	}
	if (timer->flags & SNDRV_TIMER_FLG_RESCHED)
		snd_timer_reschedule(timer, ticks_left);
	if (timer->running) {
		if (timer->hw.flags & SNDRV_TIMER_HW_STOP) {
			timer->hw.stop(timer);
			timer->flags |= SNDRV_TIMER_FLG_CHANGE;
		}
		if (!(timer->hw.flags & SNDRV_TIMER_HW_AUTO) ||
		    (timer->flags & SNDRV_TIMER_FLG_CHANGE)) {
			timer->flags &= ~SNDRV_TIMER_FLG_CHANGE;
			timer->hw.start(timer);
		}
	} else {
		timer->hw.stop(timer);
	}

	if (timer->hw.c_resolution)
		resolution = timer->hw.c_resolution(timer);
	else
		resolution = timer->hw.resolution;

	while (ts.inext != NULL) {
		ti = ts.inext;
		ts.inext = ti->inext;
		if (ts.inext)
			ts.inext->iprev = &ts;
		ti->iprev = ti->inext = NULL;
		ticks = ti->ticks;
		spin_unlock(&timer->lock);
		if (ti->callback)
			ti->callback(ti, resolution, ticks, ti->callback_data);
		spin_lock(&timer->lock);
		if (!(ti->flags & SNDRV_TIMER_IFLG_SLAVE)) {
			atomic_dec(&timer->in_use);
		} else {
			atomic_dec(&snd_timer_slave_in_use);
		}
	}
	spin_unlock(&timer->lock);
}

/*

 */

int snd_timer_new(snd_card_t *card, char *id, snd_timer_id_t *tid, snd_timer_t ** rtimer)
{
	snd_timer_t *timer;
	int err;
#ifdef TARGET_OS2
	static snd_device_ops_t ops = {
		snd_timer_dev_free,
		snd_timer_dev_register,
		snd_timer_dev_unregister
	};
#else
	static snd_device_ops_t ops = {
		dev_free:	snd_timer_dev_free,
		dev_register:	snd_timer_dev_register,
		dev_unregister:	snd_timer_dev_unregister
	};
#endif

	snd_assert(tid != NULL, return -EINVAL);
	snd_assert(rtimer != NULL, return -EINVAL);
	*rtimer = NULL;
	timer = snd_magic_kcalloc(snd_timer_t, 0, GFP_KERNEL);
	if (timer == NULL)
		return -ENOMEM;
	timer->tmr_class = tid->dev_class;
	timer->card = card;
	timer->tmr_device = tid->device;
	timer->tmr_subdevice = tid->subdevice;
	if (id)
		strncpy(timer->id, id, sizeof(timer->id) - 1);
	spin_lock_init(&timer->lock);
	if (card != NULL) {
		if ((err = snd_device_new(card, SNDRV_DEV_TIMER, timer, &ops)) < 0) {
			snd_timer_free(timer);
			return err;
		}
	}
	*rtimer = timer;
	return 0;
}

static int snd_timer_free(snd_timer_t *timer)
{
	snd_assert(timer != NULL, return -ENXIO);
	if (timer->private_free)
		timer->private_free(timer);
	snd_magic_kfree(timer);
	return 0;
}

int snd_timer_dev_free(snd_device_t *device)
{
	snd_timer_t *timer = snd_magic_cast(snd_timer_t, device->device_data, return -ENXIO);
	return snd_timer_free(timer);
}

int snd_timer_dev_register(snd_device_t *dev)
{
	snd_timer_t *timer = snd_magic_cast(snd_timer_t, dev->device_data, return -ENXIO);
	snd_timer_t *timer1, *timer2;

	snd_assert(timer != NULL && timer->hw.start != NULL && timer->hw.stop != NULL, return -ENXIO);
	if (!(timer->hw.flags & SNDRV_TIMER_HW_SLAVE) &&
	    !timer->hw.resolution && timer->hw.c_resolution == NULL)
	    	return -EINVAL;

	down(&register_mutex);
	if ((timer1 = snd_timer_devices) == NULL) {
		snd_timer_devices = timer;
	} else {
		for (timer2 = NULL; timer1 != NULL; timer2 = timer1, timer1 = timer1->next) {
			if (timer1->tmr_class > timer->tmr_class)
				break;
			if (timer1->tmr_class < timer->tmr_class)
				continue;
			if (timer1->card && timer->card) {
				if (timer1->card->number > timer->card->number)
					break;
				if (timer1->card->number < timer->card->number)
					continue;
			}
			if (timer1->tmr_device > timer->tmr_device)
				break;
			if (timer1->tmr_device < timer->tmr_device)
				continue;
			if (timer1->tmr_subdevice > timer->tmr_subdevice)
				break;
			if (timer1->tmr_subdevice < timer->tmr_subdevice)
				continue;
			up(&register_mutex);
			return -EBUSY;
		}
		if (timer2 != NULL) {
			timer->next = timer2->next;
			timer2->next = timer;
		} else {
			timer->next = snd_timer_devices;
			snd_timer_devices = timer;
		}
	}
	up(&register_mutex);
	return 0;
}

int snd_timer_unregister(snd_timer_t *timer)
{
	snd_timer_t *timer1;

	snd_assert(timer != NULL, return -ENXIO);
	if (timer->first) {
		snd_printd("timer 0x%lx is busy?\n", (long)timer);
		return -EBUSY;
	}
	
	down(&register_mutex);
	if ((timer1 = snd_timer_devices) == timer) {
		snd_timer_devices = timer->next;
	} else {
		while (timer1) {
			if (timer1->next == timer)
				break;
			timer1 = timer1->next;
		}
		if (timer1 == NULL) {
			up(&register_mutex);
			return -ENXIO;
		} else {
			timer1->next = timer->next;			
		}
	}
	up(&register_mutex);
	return snd_timer_free(timer);
}

static int snd_timer_dev_unregister(snd_device_t *device)
{
	snd_timer_t *timer = snd_magic_cast(snd_timer_t, device->device_data, return -ENXIO);
	return snd_timer_unregister(timer);
}

/*
 * exported functions for global timers
 */
int snd_timer_global_new(char *id, int device, snd_timer_t **rtimer)
{
	snd_timer_id_t tid;
	
	tid.dev_class = SNDRV_TIMER_CLASS_GLOBAL;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = -1;
	tid.device = device;
	tid.subdevice = 0;
	return snd_timer_new(NULL, id, &tid, rtimer);
}

int snd_timer_global_free(snd_timer_t *timer)
{
	return snd_timer_free(timer);
}

int snd_timer_global_register(snd_timer_t *timer)
{
	snd_device_t dev;

	memset(&dev, 0, sizeof(dev));
	dev.device_data = timer;
	return snd_timer_dev_register(&dev);
}

int snd_timer_global_unregister(snd_timer_t *timer)
{
	return snd_timer_unregister(timer);
}

/* 
 *  System timer
 */

unsigned int snd_timer_system_resolution(void)
{
	return 1000000000L / HZ;
}

static void snd_timer_s_function(unsigned long data)
{
	snd_timer_t *timer = (snd_timer_t *)data;
	snd_timer_interrupt(timer, timer->sticks);
}

static int snd_timer_s_start(snd_timer_t * timer)
{
	struct timer_list *tlist;

	tlist = (struct timer_list *) timer->private_data;
	tlist->expires = jiffies + timer->sticks;
	add_timer(tlist);
	return 0;
}

static int snd_timer_s_stop(snd_timer_t * timer)
{
	struct timer_list *tlist;

	tlist = (struct timer_list *) timer->private_data;
	del_timer(tlist);
	timer->sticks = tlist->expires - jiffies;
	return 0;
}

#ifdef TARGET_OS2
static struct _snd_timer_hardware snd_timer_system =
{
	SNDRV_TIMER_HW_FIRST,
	1000000000L / HZ,
	10000000L,
        0,0,0,
	snd_timer_s_start,
	snd_timer_s_stop
};
#else
static struct _snd_timer_hardware snd_timer_system =
{
	flags:		SNDRV_TIMER_HW_FIRST,
	resolution:	1000000000L / HZ,
	ticks:		10000000L,
	start:		snd_timer_s_start,
	stop:		snd_timer_s_stop
};
#endif

static void snd_timer_free_system(snd_timer_t *timer)
{
	if (timer->private_data)
		kfree(timer->private_data);
}

static int snd_timer_register_system(void)
{
	snd_timer_t *timer;
	struct timer_list *tlist;
	int err;

	if ((err = snd_timer_global_new("system", SNDRV_TIMER_GLOBAL_SYSTEM, &timer)) < 0)
		return err;
	strcpy(timer->name, "system timer");
	timer->hw = snd_timer_system;
	tlist = (struct timer_list *) snd_kcalloc(sizeof(struct timer_list), GFP_KERNEL);
	if (tlist == NULL) {
		snd_timer_free(timer);
		return -ENOMEM;
	}
	tlist->function = snd_timer_s_function;
	tlist->data = (unsigned long) timer;
	timer->private_data = tlist;
	timer->private_free = snd_timer_free_system;
	return snd_timer_global_register(timer);
}

/*
 *  Info interface
 */

static void snd_timer_proc_read(snd_info_entry_t *entry,
				snd_info_buffer_t * buffer)
{
	unsigned long flags;
	snd_timer_t *timer;
	snd_timer_instance_t *ti;

	down(&register_mutex);
	timer = snd_timer_devices;
	while (timer) {
		switch (timer->tmr_class) {
		case SNDRV_TIMER_CLASS_GLOBAL:
			snd_iprintf(buffer, "G%i: ", timer->tmr_device);
			break;
		case SNDRV_TIMER_CLASS_CARD:
			snd_iprintf(buffer, "C%i-%i: ", timer->card->number, timer->tmr_device);
			break;
		case SNDRV_TIMER_CLASS_PCM:
			snd_iprintf(buffer, "P%i-%i-%i: ", timer->card->number, timer->tmr_device, timer->tmr_subdevice);
			break;
		default:
			snd_iprintf(buffer, "?%i-%i-%i-%i: ", timer->tmr_class, timer->card ? timer->card->number : -1, timer->tmr_device, timer->tmr_subdevice);
		}
		snd_iprintf(buffer, "%s :", timer->name);
		if (timer->hw.resolution)
			snd_iprintf(buffer, " %lu.%luus (%lu ticks)", timer->hw.resolution / 1000, timer->hw.resolution % 1000, timer->hw.ticks);
		if (timer->hw.flags & SNDRV_TIMER_HW_SLAVE)
			snd_iprintf(buffer, " SLAVE");
		snd_iprintf(buffer, "\n");
		spin_lock_irqsave(&timer->lock, flags);
		if (timer->first) {
			ti = timer->first;
			while (ti) {
				snd_iprintf(buffer, "  Client %s : %s : lost interrupts %li\n",
						ti->owner ? ti->owner : "unknown",
						ti->flags & (SNDRV_TIMER_IFLG_START|SNDRV_TIMER_IFLG_RUNNING) ? "running" : "stopped",
						ti->lost);
				ti = ti->next;
			}
		}
		spin_unlock_irqrestore(&timer->lock, flags);
		timer = timer->next;
	}
	up(&register_mutex);
}

/*
 *  USER SPACE interface
 */

static void snd_timer_user_interrupt(snd_timer_instance_t *timeri,
				     unsigned long resolution,
				     unsigned long ticks,
				     void *data)
{
	unsigned long flags;
	snd_timer_user_t *tu = snd_magic_cast(snd_timer_user_t, data, return);
	snd_timer_read_t *r;
	
	if (tu->qused >= tu->queue_size) {
		tu->overrun++;
	} else {
		spin_lock_irqsave(&tu->qlock, flags);
		r = &tu->queue[tu->qtail++];
		tu->qtail %= tu->queue_size;
		r->resolution = resolution;
		r->ticks = ticks;
		tu->qused++;
		spin_unlock_irqrestore(&tu->qlock, flags);
		wake_up(&tu->qchange_sleep);
	}
}

static int snd_timer_user_open(struct inode *inode, struct file *file)
{
	snd_timer_user_t *tu;
	
	tu = snd_magic_kcalloc(snd_timer_user_t, 0, GFP_KERNEL);
	if (tu == NULL)
		return -ENOMEM;
	spin_lock_init(&tu->qlock);
	init_waitqueue_head(&tu->qchange_sleep);
	tu->ticks = 1;
	tu->queue_size = 128;
	tu->queue = (snd_timer_read_t *)kmalloc(tu->queue_size * sizeof(snd_timer_read_t), GFP_KERNEL);
	if (tu->queue == NULL) {
		snd_magic_kfree(tu);
		return -ENOMEM;
	}
	file->private_data = tu;
#ifndef LINUX_2_3
	MOD_INC_USE_COUNT;
#endif
	return 0;
}

static int snd_timer_user_release(struct inode *inode, struct file *file)
{
	snd_timer_user_t *tu;

	if (file->private_data) {
		tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
		file->private_data = NULL;
		if (tu->timeri)
			snd_timer_close(tu->timeri);
		if (tu->queue)
			kfree(tu->queue);
		snd_magic_kfree(tu);
	}
#ifndef LINUX_2_3
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

static void snd_timer_user_zero_id(snd_timer_id_t *id)
{
	id->dev_class = SNDRV_TIMER_CLASS_NONE;
	id->dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	id->card = -1;
	id->device = -1;
	id->subdevice = -1;
}

static void snd_timer_user_copy_id(snd_timer_id_t *id, snd_timer_t *timer)
{
	id->dev_class = timer->tmr_class;
	id->dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	id->card = timer->card ? timer->card->number : -1;
	id->device = timer->tmr_device;
	id->subdevice = timer->tmr_subdevice;
}

static int snd_timer_user_next_device(snd_timer_id_t *_tid)
{
	snd_timer_id_t id;
	snd_timer_t *timer;
	
	if (copy_from_user(&id, _tid, sizeof(id)))
		return -EFAULT;
	down(&register_mutex);
	if (id.dev_class < 0) {		/* first item */
		timer = snd_timer_devices;
		if (timer != NULL)
			snd_timer_user_copy_id(&id, timer);
		else
			snd_timer_user_zero_id(&id);
	} else {
		switch (id.dev_class) {
		case SNDRV_TIMER_CLASS_GLOBAL:
			id.device = id.device < 0 ? 0 : id.device + 1;
			for (timer = snd_timer_devices; timer != NULL; timer = timer->next) {
				if (timer->tmr_class > SNDRV_TIMER_CLASS_GLOBAL) {
					snd_timer_user_copy_id(&id, timer);
					break;
				}
				if (timer->tmr_device >= id.device) {
					snd_timer_user_copy_id(&id, timer);
					break;
				}
			}
			if (timer == NULL)
				snd_timer_user_zero_id(&id);
			break;
		case SNDRV_TIMER_CLASS_CARD:
		case SNDRV_TIMER_CLASS_PCM:
			if (id.card < 0) {
				id.card = 0;
			} else {
				if (id.card < 0) {
					id.card = 0;
				} else {
					if (id.device < 0) {
						id.device = 0;
					} else {
						id.subdevice = id.subdevice < 0 ? 0 : id.subdevice + 1;
					}
				}
			}
			for (timer = snd_timer_devices; timer != NULL; timer = timer->next) {
				if (timer->tmr_class > id.dev_class) {
					snd_timer_user_copy_id(&id, timer);
					break;
				}
				if (timer->tmr_class < id.dev_class)
					continue;
				if (timer->card->number > id.card) {
					snd_timer_user_copy_id(&id, timer);
					break;
				}
				if (timer->card->number < id.card)
					continue;
				if (timer->tmr_device > id.device) {
					snd_timer_user_copy_id(&id, timer);
					break;
				}
				if (timer->tmr_device < id.device)
					continue;
				if (timer->tmr_subdevice > id.subdevice) {
					snd_timer_user_copy_id(&id, timer);
					break;
				}
				if (timer->tmr_subdevice < id.subdevice)
					continue;
				snd_timer_user_copy_id(&id, timer);
				break;
			}
			if (timer == NULL)
				snd_timer_user_zero_id(&id);
			break;
		default:
			snd_timer_user_zero_id(&id);
		}
	}
	up(&register_mutex);
	if (copy_to_user(_tid, &id, sizeof(*_tid)))
		return -EFAULT;
	return 0;
} 

static int snd_timer_user_tselect(struct file *file, snd_timer_select_t *_tselect)
{
	snd_timer_user_t *tu;
	snd_timer_select_t tselect;
	char str[32];
	
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	if (tu->timeri)
		snd_timer_close(tu->timeri);
	if (copy_from_user(&tselect, _tselect, sizeof(tselect)))
		return -EFAULT;
	sprintf(str, "application %i", current->pid);
	if ((tu->timeri = snd_timer_open(str, &tselect.id)) == NULL)
		return -ENODEV;
	if (tselect.id.dev_class != SNDRV_TIMER_CLASS_SLAVE) {
		tu->timeri->slave_class = SNDRV_TIMER_SCLASS_APPLICATION;
		tu->timeri->slave_id = current->pid;
	}
	tu->timeri->callback = snd_timer_user_interrupt;
	tu->timeri->callback_data = (void *)tu;
	return 0;
}

static int snd_timer_user_info(struct file *file, snd_timer_info_t *_info)
{
	snd_timer_user_t *tu;
	snd_timer_info_t info;
	snd_timer_t *t;
	
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	snd_assert(tu->timeri != NULL, return -ENXIO);
	t = tu->timeri->timer;
	snd_assert(t != NULL, return -ENXIO);
	memset(&info, 0, sizeof(info));
	info.card = t->card->number;
	if (t->hw.flags & SNDRV_TIMER_HW_SLAVE)
		info.flags |= SNDRV_TIMER_FLG_SLAVE;
	strncpy(info.id, t->id, sizeof(info.id)-1);
	strncpy(info.name, t->name, sizeof(info.name)-1);
	info.ticks = t->hw.ticks;
	info.resolution = t->hw.resolution;
	if (copy_to_user(_info, &info, sizeof(*_info)))
		return -EFAULT;
	return 0;
}

static int snd_timer_user_params(struct file *file, snd_timer_params_t *_params)
{
	unsigned long flags;
	snd_timer_user_t *tu;
	snd_timer_params_t params;
	snd_timer_t *t;
	snd_timer_read_t *tr;
	int err;
	
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	snd_assert(tu->timeri != NULL, return -ENXIO);
	t = tu->timeri->timer;
	snd_assert(t != NULL, return -ENXIO);
	if (copy_from_user(&params, _params, sizeof(params)))
		return -EFAULT;
	if (!(t->hw.flags & SNDRV_TIMER_HW_SLAVE) && params.ticks < 1) {
		err = -EINVAL;
		goto _end;
	}
	if (params.queue_size > 0 && (params.queue_size < 32 || params.queue_size > 1024)) {
		err = -EINVAL;
		goto _end;
	}
	snd_timer_stop(tu->timeri);
	spin_lock_irqsave(&t->lock, flags);
	if (params.flags & SNDRV_TIMER_PSFLG_AUTO) {
		tu->timeri->flags |= SNDRV_TIMER_IFLG_AUTO;
	} else {
		tu->timeri->flags &= ~SNDRV_TIMER_IFLG_AUTO;
	}
	spin_unlock_irqrestore(&t->lock, flags);
	if (params.queue_size > 0 && tu->queue_size != params.queue_size) {
		tr = (snd_timer_read_t *)kmalloc(params.queue_size * sizeof(snd_timer_read_t), GFP_KERNEL);
		if (tr) {
			kfree(tu->queue);
			tu->queue_size = params.queue_size;
			tu->queue = tr;
		}
	}
	if (t->hw.flags & SNDRV_TIMER_HW_SLAVE) {
		tu->ticks = 1;
	} else {
		tu->ticks = params.ticks;
	}
	err = 0;
 _end:
	if (copy_to_user(_params, &params, sizeof(params)))
		return -EFAULT;
	return err;
}

static int snd_timer_user_status(struct file *file, snd_timer_status_t *_status)
{
	unsigned long flags;
	snd_timer_user_t *tu;
	snd_timer_status_t status;
	
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	snd_assert(tu->timeri != NULL, return -ENXIO);
	memset(&status, 0, sizeof(status));
	status.resolution = snd_timer_resolution(tu->timeri);
	status.lost = tu->timeri->lost;
	status.overrun = tu->overrun;
	spin_lock_irqsave(&tu->qlock, flags);
	status.queue = tu->qused;
	spin_unlock_irqrestore(&tu->qlock, flags);
	if (copy_to_user(_status, &status, sizeof(status)))
		return -EFAULT;
	return 0;
}

static int snd_timer_user_start(struct file *file)
{
	int err;
	snd_timer_user_t *tu;
		
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	snd_assert(tu->timeri != NULL, return -ENXIO);
	snd_timer_stop(tu->timeri);
	tu->timeri->lost = 0;
	return (err = snd_timer_start(tu->timeri, tu->ticks)) < 0 ? err : 0;
}

static int snd_timer_user_stop(struct file *file)
{
	int err;
	snd_timer_user_t *tu;
		
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	snd_assert(tu->timeri != NULL, return -ENXIO);
	return (err = snd_timer_stop(tu->timeri)) < 0 ? err : 0;
}

static int snd_timer_user_continue(struct file *file)
{
	int err;
	snd_timer_user_t *tu;
		
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	snd_assert(tu->timeri != NULL, return -ENXIO);
	tu->timeri->lost = 0;
	return (err = snd_timer_continue(tu->timeri)) < 0 ? err : 0;
}

static int snd_timer_user_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	snd_timer_user_t *tu;
	
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	switch (cmd) {
	case SNDRV_TIMER_IOCTL_PVERSION:
		return put_user(SNDRV_TIMER_VERSION, (int *)arg) ? -EFAULT : 0;
	case SNDRV_TIMER_IOCTL_NEXT_DEVICE:
		return snd_timer_user_next_device((snd_timer_id_t *)arg);
	case SNDRV_TIMER_IOCTL_SELECT:
		return snd_timer_user_tselect(file, (snd_timer_select_t *)arg);
	case SNDRV_TIMER_IOCTL_INFO:
		return snd_timer_user_info(file, (snd_timer_info_t *)arg);
	case SNDRV_TIMER_IOCTL_PARAMS:
		return snd_timer_user_params(file, (snd_timer_params_t *)arg);
	case SNDRV_TIMER_IOCTL_STATUS:
		return snd_timer_user_status(file, (snd_timer_status_t *)arg);
	case SNDRV_TIMER_IOCTL_START:
		return snd_timer_user_start(file);
	case SNDRV_TIMER_IOCTL_STOP:
		return snd_timer_user_stop(file);
	case SNDRV_TIMER_IOCTL_CONTINUE:
		return snd_timer_user_continue(file);
	}
	return -ENOTTY;
}

static ssize_t snd_timer_user_read(struct file *file, char *buffer, size_t count, loff_t *offset)
{
	snd_timer_user_t *tu;
	long result = 0;
	int  err = 0;
	
	tu = snd_magic_cast(snd_timer_user_t, file->private_data, return -ENXIO);
	while (count - result >= sizeof(snd_timer_read_t)) {
		spin_lock_irq(&tu->qlock);
		while (!tu->qused) {
			wait_queue_t wait;

			if (file->f_flags & O_NONBLOCK) {
				spin_unlock_irq(&tu->qlock);
				err = -EAGAIN;
				break;
			}

			set_current_state(TASK_INTERRUPTIBLE);
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&tu->qchange_sleep, &wait);

			spin_unlock(&tu->qlock);
			schedule();
			spin_lock_irq(&tu->qlock);

			remove_wait_queue(&tu->qchange_sleep, &wait);

			if (signal_pending(current)) {
				err = -ERESTARTSYS;
				break;
			}
		}
		spin_unlock_irq(&tu->qlock);
		if (err < 0)
			break;

		if (copy_to_user(buffer, &tu->queue[tu->qhead++], sizeof(snd_timer_read_t))) {
			err = -EFAULT;
			break;
		}

		tu->qhead %= tu->queue_size;
		spin_lock_irq(&tu->qlock);
		tu->qused--;
		spin_unlock_irq(&tu->qlock);
		result += sizeof(snd_timer_read_t);
		buffer += sizeof(snd_timer_read_t);
	}
	return err? err: result;
}

static unsigned int snd_timer_user_poll(struct file *file, poll_table * wait)
{
        unsigned int mask;
        snd_timer_user_t *tu;

        tu = snd_magic_cast(snd_timer_user_t, file->private_data, return 0);

        poll_wait(file, &tu->qchange_sleep, wait);
	
	mask = 0;
	if (tu->qused)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

#ifdef TARGET_OS2
static struct file_operations snd_timer_f_ops =
{
#ifdef LINUX_2_3
	THIS_MODULE,
#endif
        0,
	snd_timer_user_read,
        0,0,
	snd_timer_user_poll,
	snd_timer_user_ioctl,
        0,
	snd_timer_user_open,
        0,
	snd_timer_user_release,
        0,0,0,0,0
};

static snd_minor_t snd_timer_reg =
{
        {0,0},
        0,0,
	"timer",
        0,
	&snd_timer_f_ops
};
#else
static struct file_operations snd_timer_f_ops =
{
#ifdef LINUX_2_3
	owner:		THIS_MODULE,
#endif
	read:		snd_timer_user_read,
	open:		snd_timer_user_open,
	release:	snd_timer_user_release,
	poll:		snd_timer_user_poll,
	ioctl:		snd_timer_user_ioctl,
};

static snd_minor_t snd_timer_reg =
{
	comment:	"timer",
	f_ops:		&snd_timer_f_ops,
};
#endif

/*
 *  ENTRY functions
 */

static snd_info_entry_t *snd_timer_proc_entry = NULL;

static int __init alsa_timer_init(void)
{
	int err;
	snd_info_entry_t *entry;

#ifdef CONFIG_SND_OSSEMUL
	snd_oss_info_register(SNDRV_OSS_INFO_DEV_TIMERS, SNDRV_CARDS - 1, "system timer");
#endif
	if ((entry = snd_info_create_module_entry(THIS_MODULE, "timers", NULL)) != NULL) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->c.text.read_size = SNDRV_TIMER_DEVICES * 128;
		entry->c.text.read = snd_timer_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_timer_proc_entry = entry;
	if ((err = snd_timer_register_system()) < 0)
		snd_printk("unable to register system timer (%i)\n", err);
	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_TIMER,
					NULL, 0, &snd_timer_reg, "timer"))<0)
		snd_printk("unable to register timer device (%i)\n", err);
	return 0;
}

static void __exit alsa_timer_exit(void)
{
	snd_unregister_device(SNDRV_DEVICE_TYPE_TIMER, NULL, 0);
	/* unregister the system timer */
	if (snd_timer_devices)
		snd_timer_unregister(snd_timer_devices);
	if (snd_timer_proc_entry) {
		snd_info_unregister(snd_timer_proc_entry);
		snd_timer_proc_entry = NULL;
	}
#ifdef CONFIG_SND_OSSEMUL
	snd_oss_info_unregister(SNDRV_OSS_INFO_DEV_TIMERS, SNDRV_CARDS - 1);
#endif
}

module_init(alsa_timer_init)
module_exit(alsa_timer_exit)

EXPORT_SYMBOL(snd_timer_open);
EXPORT_SYMBOL(snd_timer_close);
EXPORT_SYMBOL(snd_timer_resolution);
EXPORT_SYMBOL(snd_timer_start);
EXPORT_SYMBOL(snd_timer_stop);
EXPORT_SYMBOL(snd_timer_continue);
EXPORT_SYMBOL(snd_timer_new);
EXPORT_SYMBOL(snd_timer_global_new);
EXPORT_SYMBOL(snd_timer_global_free);
EXPORT_SYMBOL(snd_timer_global_register);
EXPORT_SYMBOL(snd_timer_global_unregister);
EXPORT_SYMBOL(snd_timer_interrupt);
EXPORT_SYMBOL(snd_timer_system_resolution);
