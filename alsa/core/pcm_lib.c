/*
 *  Digital Audio (PCM) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Abramo Bagnara <abramo@alsa-project.org>
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
#include <sound/control.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/timer.h>

void snd_pcm_playback_silence(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t frames, ofs;
	snd_pcm_sframes_t noise_dist;
	if (runtime->silenced_start != runtime->control->appl_ptr) {
		snd_pcm_sframes_t n = runtime->control->appl_ptr - runtime->silenced_start;
		if (n < 0)
			n += runtime->boundary;
		if ((snd_pcm_uframes_t)n < runtime->silenced_size)
			runtime->silenced_size -= n;
		else
			runtime->silenced_size = 0;
		runtime->silenced_start = runtime->control->appl_ptr;
	}
	if (runtime->silenced_size == runtime->buffer_size)
		return;
	snd_assert(runtime->silenced_size <= runtime->buffer_size, return);
	noise_dist = snd_pcm_playback_hw_avail(runtime) + runtime->silenced_size;
	if (noise_dist > (snd_pcm_sframes_t) runtime->silence_threshold)
		return;
	frames = runtime->silence_threshold - noise_dist;
	if (frames < runtime->silence_size)
		frames = runtime->silence_size;
	if (runtime->silenced_size + frames > runtime->buffer_size)
		frames = runtime->buffer_size - runtime->silenced_size;
	ofs = runtime->silenced_start % runtime->buffer_size + runtime->silenced_size;
	if (ofs >= runtime->buffer_size)
		ofs -= runtime->buffer_size;
#ifdef TARGET_OS2
        if (ofs + frames > runtime->buffer_size) {
                dprintf(("silence: overshooting buffer boundary %x + %x > %x", ofs, frames, runtime->buffer_size));
                frames = runtime->buffer_size - ofs;
        }
#endif
	if (runtime->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED ||
	    runtime->access == SNDRV_PCM_ACCESS_MMAP_INTERLEAVED) {
		if (substream->ops->silence) {
			int err;
			err = substream->ops->silence(substream, -1, ofs, frames);
			snd_assert(err >= 0, );
		} else {
			char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, ofs);
			snd_pcm_format_set_silence(runtime->format, hwbuf, frames * runtime->channels);
		}
	} else {
		unsigned int c;
		unsigned int channels = runtime->channels;
		if (substream->ops->silence) {
			for (c = 0; c < channels; ++c) {
				int err;
				err = substream->ops->silence(substream, c, ofs, frames);
				snd_assert(err >= 0, );
			}
		} else {
			size_t dma_csize = runtime->dma_bytes / channels;
			for (c = 0; c < channels; ++c) {
				char *hwbuf = runtime->dma_area + (c * dma_csize) + samples_to_bytes(runtime, ofs);
				snd_pcm_format_set_silence(runtime->format, hwbuf, frames);
			}
		}
	}
	runtime->silenced_size += frames;
}

int snd_pcm_update_hw_ptr_interrupt(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t pos;
	snd_pcm_uframes_t old_hw_ptr, new_hw_ptr, hw_ptr_interrupt;
	snd_pcm_uframes_t avail;
	snd_pcm_sframes_t delta;

	old_hw_ptr = runtime->status->hw_ptr;
	pos = substream->ops->pointer(substream);
	if (runtime->tstamp_mode & SNDRV_PCM_TSTAMP_MMAP)
		snd_timestamp_now((snd_timestamp_t*)&runtime->status->tstamp);
	snd_assert(pos <= runtime->buffer_size, return 0);
	  
	pos -= pos % runtime->min_align;
	new_hw_ptr = runtime->hw_ptr_base + pos;

	hw_ptr_interrupt = runtime->hw_ptr_interrupt + runtime->period_size;

	delta = hw_ptr_interrupt - new_hw_ptr;
	if (delta > 0) {
		if (delta < runtime->buffer_size / 2) {
			snd_printd("Unexpected hw_pointer value (delta: -%ld): wrong interrupt acknowledge?\n", (long) delta);
			return 0;
		}
		runtime->hw_ptr_base += runtime->buffer_size;
		if (runtime->hw_ptr_base == runtime->boundary)
			runtime->hw_ptr_base = 0;
		new_hw_ptr = runtime->hw_ptr_base + pos;
	}
	runtime->status->hw_ptr = new_hw_ptr;
	runtime->hw_ptr_interrupt = new_hw_ptr - pos % runtime->period_size;

#if 0
	if (hw_ptr_interrupt == runtime->boundary)
		hw_ptr_interrupt = 0;

	if (runtime->hw_ptr_interrupt != hw_ptr_interrupt)
		snd_printd("Lost interrupt: hw_ptr = %d expected %d\n", runtime->hw_ptr_interrupt, hw_ptr_interrupt);
#endif

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		avail = snd_pcm_playback_avail(runtime);
	else
		avail = snd_pcm_capture_avail(runtime);
	if (avail > runtime->avail_max)
		runtime->avail_max = avail;
	if (avail >= runtime->stop_threshold) {
		snd_pcm_stop(substream,
			     runtime->status->state == SNDRV_PCM_STATE_DRAINING ?
			     SNDRV_PCM_STATE_SETUP : SNDRV_PCM_STATE_XRUN);
		return -EPIPE;
	}
	if (avail >= runtime->control->avail_min)
		wake_up(&runtime->sleep);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream);
	return 0;
}

/* CAUTION: call it with irq disabled */
int snd_pcm_update_hw_ptr(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t pos;
	snd_pcm_uframes_t old_hw_ptr, new_hw_ptr;
	snd_pcm_uframes_t avail;
	snd_pcm_sframes_t delta;
	old_hw_ptr = runtime->status->hw_ptr;
	pos = substream->ops->pointer(substream);
	if (runtime->tstamp_mode & SNDRV_PCM_TSTAMP_MMAP)
		snd_timestamp_now((snd_timestamp_t*)&runtime->status->tstamp);
	snd_assert(pos <= runtime->buffer_size, return 0);
	  
	pos -= pos % runtime->min_align;
	new_hw_ptr = runtime->hw_ptr_base + pos;

	delta = old_hw_ptr - new_hw_ptr;
	if (delta > 0) {
		if (delta < runtime->buffer_size / 2) {
			snd_printd("Unexpected hw_pointer value (delta: -%ld): hardware/driver is broken?\n", (long) delta);
			return 0;
		}
		runtime->hw_ptr_base += runtime->buffer_size;
		if (runtime->hw_ptr_base == runtime->boundary)
			runtime->hw_ptr_base = 0;
		new_hw_ptr = runtime->hw_ptr_base + pos;
	}
	runtime->status->hw_ptr = new_hw_ptr;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		avail = snd_pcm_playback_avail(runtime);
	else
		avail = snd_pcm_capture_avail(runtime);
	if (avail > runtime->avail_max)
		runtime->avail_max = avail;
	if (avail >= runtime->stop_threshold) {
#ifdef TARGET_OS2
                //MMPM/2 tends to query for the stream position just about
                //when it's done; stopping the stream here will prevent
                //our stream handler from getting any interrupts, so it
                //won't be able to return the last audio buffer
                return 0;
#else
		snd_pcm_stop(substream,
			     runtime->status->state == SNDRV_PCM_STATE_DRAINING ?
			     SNDRV_PCM_STATE_SETUP : SNDRV_PCM_STATE_XRUN);
		return -EPIPE;
#endif
	}
	if (avail >= runtime->control->avail_min)
		wake_up(&runtime->sleep);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream);
	return 0;
}

/*
 *  Operations
 */

void snd_pcm_set_ops(snd_pcm_t *pcm, int direction, snd_pcm_ops_t *ops)
{
	snd_pcm_str_t *stream = &pcm->streams[direction];
	snd_pcm_substream_t *substream;
	
	for (substream = stream->substream; substream != NULL; substream = substream->next)
		substream->ops = ops;
}

/*
 *  Sync
 */
 
void snd_pcm_set_sync(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	
	runtime->sync.id32[0] = substream->pcm->card->number;
	runtime->sync.id32[1] = -1;
	runtime->sync.id32[2] = -1;
	runtime->sync.id32[3] = -1;
}

/*
 *  Standard ioctl routine
 */

/* Code taken from alsa-lib */
#define assert(a) snd_assert((a), return -EINVAL)

static inline unsigned int div32(unsigned int a, unsigned int b, 
				 unsigned int *r)
{
	if (b == 0) {
		*r = 0;
		return UINT_MAX;
	}
	*r = a % b;
	return a / b;
}

static inline unsigned int div_down(unsigned int a, unsigned int b)
{
	if (b == 0)
		return UINT_MAX;
	return a / b;
}

static inline unsigned int div_up(unsigned int a, unsigned int b)
{
	unsigned int r;
	unsigned int q;
	if (b == 0)
		return UINT_MAX;
	q = div32(a, b, &r);
	if (r)
		++q;
	return q;
}

static inline unsigned int mul(unsigned int a, unsigned int b)
{
	if (a == 0)
		return 0;
	if (div_down(UINT_MAX, a) < b)
		return UINT_MAX;
	return a * b;
}

static inline unsigned int muldiv32(unsigned int a, unsigned int b,
				    unsigned int c, unsigned int *r)
{
	u_int64_t n = (u_int64_t) a * b;
	if (c == 0) {
		snd_assert(n > 0, );
		*r = 0;
		return UINT_MAX;
	}
	div64_32(&n, c, r);
	if (n >= UINT_MAX) {
		*r = 0;
		return UINT_MAX;
	}
	return n;
}

int snd_interval_refine_min(snd_interval_t *i, unsigned int min, int openmin)
{
	int changed = 0;
	assert(!snd_interval_empty(i));
	if (i->min < min) {
		i->min = min;
		i->openmin = openmin;
		changed = 1;
	} else if (i->min == min && !i->openmin && openmin) {
		i->openmin = 1;
		changed = 1;
	}
	if (i->integer) {
		if (i->openmin) {
			i->min++;
			i->openmin = 0;
		}
	}
	if (snd_interval_checkempty(i)) {
		snd_interval_none(i);
		return -EINVAL;
	}
	return changed;
}

int snd_interval_refine_max(snd_interval_t *i, unsigned int max, int openmax)
{
	int changed = 0;
	assert(!snd_interval_empty(i));
	if (i->max > max) {
		i->max = max;
		i->openmax = openmax;
		changed = 1;
	} else if (i->max == max && !i->openmax && openmax) {
		i->openmax = 1;
		changed = 1;
	}
	if (i->integer) {
		if (i->openmax) {
			i->max--;
			i->openmax = 0;
		}
	}
	if (snd_interval_checkempty(i)) {
		snd_interval_none(i);
		return -EINVAL;
	}
	return changed;
}

/* r <- v */
int snd_interval_refine(snd_interval_t *i, const snd_interval_t *v)
{
	int changed = 0;
	assert(!snd_interval_empty(i));
	if (i->min < v->min) {
		i->min = v->min;
		i->openmin = v->openmin;
		changed = 1;
	} else if (i->min == v->min && !i->openmin && v->openmin) {
		i->openmin = 1;
		changed = 1;
	}
	if (i->max > v->max) {
		i->max = v->max;
		i->openmax = v->openmax;
		changed = 1;
	} else if (i->max == v->max && !i->openmax && v->openmax) {
		i->openmax = 1;
		changed = 1;
	}
	if (!i->integer && v->integer) {
		i->integer = 1;
		changed = 1;
	}
	if (i->integer) {
		if (i->openmin) {
			i->min++;
			i->openmin = 0;
		}
		if (i->openmax) {
			i->max--;
			i->openmax = 0;
		}
	} else if (!i->openmin && !i->openmax && i->min == i->max)
		i->integer = 1;
	if (snd_interval_checkempty(i)) {
		snd_interval_none(i);
		return -EINVAL;
	}
	return changed;
}

int snd_interval_refine_first(snd_interval_t *i)
{
	assert(!snd_interval_empty(i));
	if (snd_interval_single(i))
		return 0;
	i->max = i->min;
	i->openmax = i->openmin;
	if (i->openmax)
		i->max++;
	return 1;
}

int snd_interval_refine_last(snd_interval_t *i)
{
	assert(!snd_interval_empty(i));
	if (snd_interval_single(i))
		return 0;
	i->min = i->max;
	i->openmin = i->openmax;
	if (i->openmin)
		i->min--;
	return 1;
}

int snd_interval_refine_set(snd_interval_t *i, unsigned int val)
{
	snd_interval_t t;
	t.empty = 0;
	t.min = t.max = val;
	t.openmin = t.openmax = 0;
	t.integer = 1;
	return snd_interval_refine(i, &t);
}

void snd_interval_mul(const snd_interval_t *a, const snd_interval_t *b, snd_interval_t *c)
{
	if (a->empty || b->empty) {
		snd_interval_none(c);
		return;
	}
	c->empty = 0;
	c->min = mul(a->min, b->min);
	c->openmin = (a->openmin || b->openmin);
	c->max = mul(a->max,  b->max);
	c->openmax = (a->openmax || b->openmax);
	c->integer = (a->integer && b->integer);
}

void snd_interval_div(const snd_interval_t *a, const snd_interval_t *b, snd_interval_t *c)
{
	unsigned int r;
	if (a->empty || b->empty) {
		snd_interval_none(c);
		return;
	}
	c->empty = 0;
	c->min = div32(a->min, b->max, &r);
	c->openmin = (r || a->openmin || b->openmax);
	if (b->min > 0) {
		c->max = div32(a->max, b->min, &r);
		if (r) {
			c->max++;
			c->openmax = 1;
		} else
			c->openmax = (a->openmax || b->openmin);
	} else {
		c->max = UINT_MAX;
		c->openmax = 0;
	}
	c->integer = 0;
}

/* a * b / k */
void snd_interval_muldivk(const snd_interval_t *a, const snd_interval_t *b,
		      unsigned int k, snd_interval_t *c)
{
	unsigned int r;
	if (a->empty || b->empty) {
		snd_interval_none(c);
		return;
	}
	c->empty = 0;
	c->min = muldiv32(a->min, b->min, k, &r);
	c->openmin = (r || a->openmin || b->openmin);
	c->max = muldiv32(a->max, b->max, k, &r);
	if (r) {
		c->max++;
		c->openmax = 1;
	} else
		c->openmax = (a->openmax || b->openmax);
	c->integer = 0;
}

/* a * k / b */
void snd_interval_mulkdiv(const snd_interval_t *a, unsigned int k,
		      const snd_interval_t *b, snd_interval_t *c)
{
	unsigned int r;
	if (a->empty || b->empty) {
		snd_interval_none(c);
		return;
	}
	c->empty = 0;
	c->min = muldiv32(a->min, k, b->max, &r);
	c->openmin = (r || a->openmin || b->openmax);
	if (b->min > 0) {
		c->max = muldiv32(a->max, k, b->min, &r);
		if (r) {
			c->max++;
			c->openmax = 1;
		} else
			c->openmax = (a->openmax || b->openmin);
	} else {
		c->max = UINT_MAX;
		c->openmax = 0;
	}
	c->integer = 0;
}

#undef assert
/* ---- */


int snd_interval_ratnum(snd_interval_t *i,
		    unsigned int rats_count, ratnum_t *rats,
		    unsigned int *nump, unsigned int *denp)
{
	unsigned int best_num, best_diff, best_den;
	unsigned int k;
	snd_interval_t t;
	int err;

	best_num = best_den = best_diff = 0;
	for (k = 0; k < rats_count; ++k) {
		unsigned int num = rats[k].num;
		unsigned int den;
		unsigned int q = i->min;
		int diff;
		if (q == 0)
			q = 1;
		den = div_down(num, q);
		if (den < rats[k].den_min)
			continue;
		if (den > rats[k].den_max)
			den = rats[k].den_max;
		else {
			unsigned int r;
			r = (den - rats[k].den_min) % rats[k].den_step;
			if (r != 0)
				den -= r;
		}
		diff = num - q * den;
		if (best_num == 0 ||
		    diff * best_den < best_diff * den) {
			best_diff = diff;
			best_den = den;
			best_num = num;
		}
	}
	if (best_den == 0) {
		i->empty = 1;
		return -EINVAL;
	}
	t.min = div_down(best_num, best_den);
	t.openmin = !!(best_num % best_den);
	
	best_num = best_den = best_diff = 0;
	for (k = 0; k < rats_count; ++k) {
		unsigned int num = rats[k].num;
		unsigned int den;
		unsigned int q = i->max;
		int diff;
		if (q == 0) {
			i->empty = 1;
			return -EINVAL;
		}
		den = div_up(num, q);
		if (den > rats[k].den_max)
			continue;
		if (den < rats[k].den_min)
			den = rats[k].den_min;
		else {
			unsigned int r;
			r = (den - rats[k].den_min) % rats[k].den_step;
			if (r != 0)
				den += rats[k].den_step - r;
		}
		diff = q * den - num;
		if (best_num == 0 ||
		    diff * best_den < best_diff * den) {
			best_diff = diff;
			best_den = den;
			best_num = num;
		}
	}
	if (best_den == 0) {
		i->empty = 1;
		return -EINVAL;
	}
	t.max = div_up(best_num, best_den);
	t.openmax = !!(best_num % best_den);
	t.integer = 0;
	err = snd_interval_refine(i, &t);
	if (err < 0)
		return err;

	if (snd_interval_single(i)) {
		if (nump)
			*nump = best_num;
		if (denp)
			*denp = best_den;
	}
	return err;
}

int snd_interval_ratden(snd_interval_t *i,
		    unsigned int rats_count, ratden_t *rats,
		    unsigned int *nump, unsigned int *denp)
{
	unsigned int best_num, best_diff, best_den;
	unsigned int k;
	snd_interval_t t;
	int err;

	best_num = best_den = best_diff = 0;
	for (k = 0; k < rats_count; ++k) {
		unsigned int num;
		unsigned int den = rats[k].den;
		unsigned int q = i->min;
		int diff;
		num = mul(q, den);
		if (num > rats[k].num_max)
			continue;
		if (num < rats[k].num_min)
			num = rats[k].num_max;
		else {
			unsigned int r;
			r = (num - rats[k].num_min) % rats[k].num_step;
			if (r != 0)
				num += rats[k].num_step - r;
		}
		diff = num - q * den;
		if (best_num == 0 ||
		    diff * best_den < best_diff * den) {
			best_diff = diff;
			best_den = den;
			best_num = num;
		}
	}
	if (best_den == 0) {
		i->empty = 1;
		return -EINVAL;
	}
	t.min = div_down(best_num, best_den);
	t.openmin = !!(best_num % best_den);
	
	best_num = best_den = best_diff = 0;
	for (k = 0; k < rats_count; ++k) {
		unsigned int num;
		unsigned int den = rats[k].den;
		unsigned int q = i->max;
		int diff;
		num = mul(q, den);
		if (num < rats[k].num_min)
			continue;
		if (num > rats[k].num_max)
			num = rats[k].num_max;
		else {
			unsigned int r;
			r = (num - rats[k].num_min) % rats[k].num_step;
			if (r != 0)
				num -= r;
		}
		diff = q * den - num;
		if (best_num == 0 ||
		    diff * best_den < best_diff * den) {
			best_diff = diff;
			best_den = den;
			best_num = num;
		}
	}
	if (best_den == 0) {
		i->empty = 1;
		return -EINVAL;
	}
	t.max = div_up(best_num, best_den);
	t.openmax = !!(best_num % best_den);
	t.integer = 0;
	err = snd_interval_refine(i, &t);
	if (err < 0)
		return err;

	if (snd_interval_single(i)) {
		if (nump)
			*nump = best_num;
		if (denp)
			*denp = best_den;
	}
	return err;
}

int snd_interval_list(snd_interval_t *i, unsigned int count, unsigned int *list, unsigned int mask)
{
        unsigned int k;
	int changed = 0;
        for (k = 0; k < count; k++) {
		if (mask && !(mask & (1 << k)))
			continue;
                if (i->min == list[k] && !i->openmin)
                        goto _l1;
                if (i->min < list[k]) {
                        i->min = list[k];
			i->openmin = 0;
			changed = 1;
                        goto _l1;
                }
        }
        i->empty = 1;
        return -EINVAL;
 _l1:
        for (k = count; k-- > 0;) {
		if (mask && !(mask & (1 << k)))
			continue;
                if (i->max == list[k] && !i->openmax)
                        goto _l2;
                if (i->max > list[k]) {
                        i->max = list[k];
			i->openmax = 0;
			changed = 1;
                        goto _l2;
                }
        }
        i->empty = 1;
        return -EINVAL;
 _l2:
	if (snd_interval_checkempty(i)) {
		i->empty = 1;
		return -EINVAL;
	}
        return changed;
}

int snd_interval_step(snd_interval_t *i, unsigned int min, unsigned int step)
{
	unsigned int n;
	int changed = 0;
	n = (i->min - min) % step;
	if (n != 0 || i->openmin) {
		i->min += step - n;
		changed = 1;
	}
	n = (i->max - min) % step;
	if (n != 0 || i->openmax) {
		i->max -= n;
		changed = 1;
	}
	if (snd_interval_checkempty(i)) {
		i->empty = 1;
		return -EINVAL;
	}
	return changed;
}

/* Info constraints helpers */

int snd_pcm_hw_rule_add(snd_pcm_runtime_t *runtime, unsigned int cond,
			int var,
			snd_pcm_hw_rule_func_t func, void *private,
			int dep, ...)
{
	snd_pcm_hw_constraints_t *constrs = &runtime->hw_constraints;
	snd_pcm_hw_rule_t *c;
	unsigned int k;
	va_list args;
	va_start(args, dep);
	if (constrs->rules_num >= constrs->rules_all) {
		snd_pcm_hw_rule_t *old = constrs->rules;
		if (constrs->rules_all == 0)
			constrs->rules_all = 32;
		else {
			old = constrs->rules;
			constrs->rules_all += 10;
		}
		constrs->rules = snd_kcalloc(constrs->rules_all * sizeof(*c),
					     GFP_KERNEL);
		if (!constrs->rules)
			return -ENOMEM;
		if (old) {
			memcpy(constrs->rules, old,
			       constrs->rules_num * sizeof(*c));
			kfree(old);
		}
	}
	c = &constrs->rules[constrs->rules_num];
	c->cond = cond;
	c->func = func;
	c->var = var;
	c->private = private;
	k = 0;
	while (1) {
		snd_assert(k < sizeof(c->deps) / sizeof(c->deps[0]), return -EINVAL);
		c->deps[k++] = dep;
		if (dep < 0)
			break;
		dep = va_arg(args, int);
	}
	constrs->rules_num++;
	va_end(args);
	return 0;
}				    

int snd_pcm_hw_constraint_mask(snd_pcm_runtime_t *runtime, snd_pcm_hw_param_t var,
			       unsigned int mask)
{
	snd_pcm_hw_constraints_t *constrs = &runtime->hw_constraints;
	unsigned int *maskp = constrs_mask(constrs, var);
	*maskp &= mask;
	if (*maskp == 0)
		return -EINVAL;
	return 0;
}

int snd_pcm_hw_constraint_integer(snd_pcm_runtime_t *runtime, snd_pcm_hw_param_t var)
{
	snd_pcm_hw_constraints_t *constrs = &runtime->hw_constraints;
	return snd_interval_setinteger(constrs_interval(constrs, var));
}

int snd_pcm_hw_constraint_minmax(snd_pcm_runtime_t *runtime, snd_pcm_hw_param_t var,
				 unsigned int min, unsigned int max)
{
	snd_pcm_hw_constraints_t *constrs = &runtime->hw_constraints;
	snd_interval_t t;
	t.min = min;
	t.max = max;
	t.openmin = t.openmax = 0;
	t.integer = 0;
	return snd_interval_refine(constrs_interval(constrs, var), &t);
}

static int snd_pcm_hw_rule_list(snd_pcm_hw_params_t *params,
				snd_pcm_hw_rule_t *rule)
{
	snd_pcm_hw_constraint_list_t *list = rule->private;
	return snd_interval_list(hw_param_interval(params, rule->var), list->count, list->list, list->mask);
}		


int snd_pcm_hw_constraint_list(snd_pcm_runtime_t *runtime,
			       unsigned int cond,
			       snd_pcm_hw_param_t var,
			       snd_pcm_hw_constraint_list_t *l)
{
	return snd_pcm_hw_rule_add(runtime, cond, var,
				   snd_pcm_hw_rule_list, l,
				   var, -1);
}

static int snd_pcm_hw_rule_ratnums(snd_pcm_hw_params_t *params,
				   snd_pcm_hw_rule_t *rule)
{
	snd_pcm_hw_constraint_ratnums_t *r = rule->private;
	unsigned int num = 0, den = 0;
	int err;
	err = snd_interval_ratnum(hw_param_interval(params, rule->var),
				  r->nrats, r->rats, &num, &den);
	if (err >= 0 && den && rule->var == SNDRV_PCM_HW_PARAM_RATE) {
		params->rate_num = num;
		params->rate_den = den;
	}
	return err;
}

int snd_pcm_hw_constraint_ratnums(snd_pcm_runtime_t *runtime, 
				  unsigned int cond,
				  snd_pcm_hw_param_t var,
				  snd_pcm_hw_constraint_ratnums_t *r)
{
	return snd_pcm_hw_rule_add(runtime, cond, var,
				   snd_pcm_hw_rule_ratnums, r,
				   var, -1);
}

static int snd_pcm_hw_rule_ratdens(snd_pcm_hw_params_t *params,
				   snd_pcm_hw_rule_t *rule)
{
	snd_pcm_hw_constraint_ratdens_t *r = rule->private;
	unsigned int num = 0, den = 0;
	int err = snd_interval_ratden(hw_param_interval(params, rule->var),
				  r->nrats, r->rats, &num, &den);
	if (err >= 0 && den && rule->var == SNDRV_PCM_HW_PARAM_RATE) {
		params->rate_num = num;
		params->rate_den = den;
	}
	return err;
}

int snd_pcm_hw_constraint_ratdens(snd_pcm_runtime_t *runtime, 
				  unsigned int cond,
				  snd_pcm_hw_param_t var,
				  snd_pcm_hw_constraint_ratdens_t *r)
{
	return snd_pcm_hw_rule_add(runtime, cond, var,
				   snd_pcm_hw_rule_ratdens, r,
				   var, -1);
}

static int snd_pcm_hw_rule_msbits(snd_pcm_hw_params_t *params,
				  snd_pcm_hw_rule_t *rule)
{
	unsigned int l = (unsigned long) rule->private;
	unsigned int width = l & 0xffff;
	unsigned int msbits = l >> 16;
	snd_interval_t *i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
	if (snd_interval_single(i) && snd_interval_value(i) == width)
		params->msbits = msbits;
	return 0;
}

int snd_pcm_hw_constraint_msbits(snd_pcm_runtime_t *runtime, 
				 unsigned int cond,
				 unsigned int width,
				 unsigned int msbits)
{
	unsigned long l = (msbits << 16) | width;
	return snd_pcm_hw_rule_add(runtime, cond, -1,
				    snd_pcm_hw_rule_msbits,
				    (void*) l,
				    SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
}

static int snd_pcm_hw_rule_step(snd_pcm_hw_params_t *params,
				snd_pcm_hw_rule_t *rule)
{
	unsigned long step = (unsigned long) rule->private;
	return snd_interval_step(hw_param_interval(params, rule->var), 0, step);
}

int snd_pcm_hw_constraint_step(snd_pcm_runtime_t *runtime,
			       unsigned int cond,
			       snd_pcm_hw_param_t var,
			       unsigned long step)
{
	return snd_pcm_hw_rule_add(runtime, cond, var, 
				   snd_pcm_hw_rule_step, (void *) step,
				   var, -1);
}


/* To use the same code we have in alsa-lib */
#define snd_pcm_t snd_pcm_substream_t
#define assert(i) snd_assert((i), return -EINVAL)
#ifndef INT_MIN
#define INT_MIN ((int)((unsigned int)INT_MAX+1))
#endif

void _snd_pcm_hw_param_any(snd_pcm_hw_params_t *params, snd_pcm_hw_param_t var)
{
	if (hw_is_mask(var)) {
		snd_mask_any(hw_param_mask(params, var));
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
		return;
	}
	if (hw_is_interval(var)) {
		snd_interval_any(hw_param_interval(params, var));
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
		return;
	}
	snd_BUG();
}

int snd_pcm_hw_param_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
			 snd_pcm_hw_param_t var)
{
	_snd_pcm_hw_param_any(params, var);
	return snd_pcm_hw_refine(pcm, params);
}

void _snd_pcm_hw_params_any(snd_pcm_hw_params_t *params)
{
	unsigned int k;
	memset(params, 0, sizeof(*params));
	for (k = 0; k <= SNDRV_PCM_HW_PARAM_LAST; k++)
		_snd_pcm_hw_param_any(params, k);
	params->info = ~0U;
}

/* Fill PARAMS with full configuration space boundaries */
int snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	_snd_pcm_hw_params_any(params);
	return snd_pcm_hw_refine(pcm, params);
}

/* Return the value for field PAR if it's fixed in configuration space 
   defined by PARAMS. Return -EINVAL otherwise
*/
int snd_pcm_hw_param_value(const snd_pcm_hw_params_t *params,
			   snd_pcm_hw_param_t var, int *dir)
{
	if (hw_is_mask(var)) {
		const snd_mask_t *mask = hw_param_mask_c(params, var);
		if (!snd_mask_single(mask))
			return -EINVAL;
		if (dir)
			*dir = 0;
		return snd_mask_value(mask);
	}
	if (hw_is_interval(var)) {
		const snd_interval_t *i = hw_param_interval_c(params, var);
		if (!snd_interval_single(i))
			return -EINVAL;
		if (dir)
			*dir = i->openmin;
		return snd_interval_value(i);
	}
	assert(0);
	return -EINVAL;
}

/* Return the minimum value for field PAR. */
unsigned int snd_pcm_hw_param_value_min(const snd_pcm_hw_params_t *params,
					snd_pcm_hw_param_t var, int *dir)
{
	if (hw_is_mask(var)) {
		if (dir)
			*dir = 0;
		return snd_mask_min(hw_param_mask_c(params, var));
	}
	if (hw_is_interval(var)) {
		const snd_interval_t *i = hw_param_interval_c(params, var);
		if (dir)
			*dir = i->openmin;
		return snd_interval_min(i);
	}
	assert(0);
	return -EINVAL;
}

/* Return the maximum value for field PAR. */
unsigned int snd_pcm_hw_param_value_max(const snd_pcm_hw_params_t *params,
					snd_pcm_hw_param_t var, int *dir)
{
	if (hw_is_mask(var)) {
		if (dir)
			*dir = 0;
		return snd_mask_max(hw_param_mask_c(params, var));
	}
	if (hw_is_interval(var)) {
		const snd_interval_t *i = hw_param_interval_c(params, var);
		if (dir)
			*dir = - (int) i->openmax;
		return snd_interval_max(i);
	}
	assert(0);
	return -EINVAL;
}

void _snd_pcm_hw_param_setempty(snd_pcm_hw_params_t *params,
				snd_pcm_hw_param_t var)
{
	if (hw_is_mask(var)) {
		snd_mask_none(hw_param_mask(params, var));
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	} else if (hw_is_interval(var)) {
		snd_interval_none(hw_param_interval(params, var));
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	} else {
		snd_BUG();
	}
}

int _snd_pcm_hw_param_setinteger(snd_pcm_hw_params_t *params,
				 snd_pcm_hw_param_t var)
{
	int changed;
	assert(hw_is_interval(var));
	changed = snd_interval_setinteger(hw_param_interval(params, var));
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}
	
/* Inside configuration space defined by PARAMS remove from PAR all 
   non integer values. Reduce configuration space accordingly.
   Return -EINVAL if the configuration space is empty
*/
int snd_pcm_hw_param_setinteger(snd_pcm_t *pcm, 
				snd_pcm_hw_params_t *params,
				snd_pcm_hw_param_t var)
{
	int changed = _snd_pcm_hw_param_setinteger(params, var);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		if (err < 0)
			return err;
	}
	return 0;
}

int _snd_pcm_hw_param_first(snd_pcm_hw_params_t *params,
			    snd_pcm_hw_param_t var)
{
	int changed;
	if (hw_is_mask(var))
		changed = snd_mask_refine_first(hw_param_mask(params, var));
	else if (hw_is_interval(var))
		changed = snd_interval_refine_first(hw_param_interval(params, var));
	else {
		assert(0);
		return -EINVAL;
	}
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}


/* Inside configuration space defined by PARAMS remove from PAR all 
   values > minimum. Reduce configuration space accordingly.
   Return the minimum.
*/
int snd_pcm_hw_param_first(snd_pcm_t *pcm, 
			   snd_pcm_hw_params_t *params, 
			   snd_pcm_hw_param_t var, int *dir)
{
	int changed = _snd_pcm_hw_param_first(params, var);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		assert(err >= 0);
	}
	return snd_pcm_hw_param_value(params, var, dir);
}

int _snd_pcm_hw_param_last(snd_pcm_hw_params_t *params,
			   snd_pcm_hw_param_t var)
{
	int changed;
	if (hw_is_mask(var))
		changed = snd_mask_refine_last(hw_param_mask(params, var));
	else if (hw_is_interval(var))
		changed = snd_interval_refine_last(hw_param_interval(params, var));
	else {
		assert(0);
		return -EINVAL;
	}
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}


/* Inside configuration space defined by PARAMS remove from PAR all 
   values < maximum. Reduce configuration space accordingly.
   Return the maximum.
*/
int snd_pcm_hw_param_last(snd_pcm_t *pcm, 
			  snd_pcm_hw_params_t *params,
			  snd_pcm_hw_param_t var, int *dir)
{
	int changed = _snd_pcm_hw_param_last(params, var);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		assert(err >= 0);
	}
	return snd_pcm_hw_param_value(params, var, dir);
}

int _snd_pcm_hw_param_min(snd_pcm_hw_params_t *params,
			  snd_pcm_hw_param_t var, unsigned int val, int dir)
{
	int changed;
	int open = 0;
	if (dir) {
		if (dir > 0) {
			open = 1;
		} else if (dir < 0) {
			if (val > 0) {
				open = 1;
				val--;
			}
		}
	}
	if (hw_is_mask(var))
		changed = snd_mask_refine_min(hw_param_mask(params, var), val + !!open);
	else if (hw_is_interval(var))
		changed = snd_interval_refine_min(hw_param_interval(params, var), val, open);
	else {
		assert(0);
		return -EINVAL;
	}
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}

/* Inside configuration space defined by PARAMS remove from PAR all 
   values < VAL. Reduce configuration space accordingly.
   Return new minimum or -EINVAL if the configuration space is empty
*/
int snd_pcm_hw_param_min(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
			 snd_pcm_hw_param_t var, unsigned int val, int *dir)
{
	int changed = _snd_pcm_hw_param_min(params, var, val, dir ? *dir : 0);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		if (err < 0)
			return err;
	}
	return snd_pcm_hw_param_value_min(params, var, dir);
}

int _snd_pcm_hw_param_max(snd_pcm_hw_params_t *params,
			   snd_pcm_hw_param_t var, unsigned int val, int dir)
{
	int changed;
	int open = 0;
	if (dir) {
		if (dir < 0) {
			open = 1;
		} else if (dir > 0) {
			open = 1;
			val++;
		}
	}
	if (hw_is_mask(var)) {
		if (val == 0 && open) {
			snd_mask_none(hw_param_mask(params, var));
			changed = -EINVAL;
		} else
			changed = snd_mask_refine_max(hw_param_mask(params, var), val - !!open);
	} else if (hw_is_interval(var))
		changed = snd_interval_refine_max(hw_param_interval(params, var), val, open);
	else {
		assert(0);
		return -EINVAL;
	}
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}

/* Inside configuration space defined by PARAMS remove from PAR all 
   values >= VAL + 1. Reduce configuration space accordingly.
   Return new maximum or -EINVAL if the configuration space is empty
*/
int snd_pcm_hw_param_max(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
			  snd_pcm_hw_param_t var, unsigned int val, int *dir)
{
	int changed = _snd_pcm_hw_param_max(params, var, val, dir ? *dir : 0);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		if (err < 0)
			return err;
	}
	return snd_pcm_hw_param_value_max(params, var, dir);
}

int _snd_pcm_hw_param_set(snd_pcm_hw_params_t *params,
			  snd_pcm_hw_param_t var, unsigned int val, int dir)
{
	int changed;
	if (hw_is_mask(var)) {
		snd_mask_t *m = hw_param_mask(params, var);
		if (val == 0 && dir < 0) {
			changed = -EINVAL;
			snd_mask_none(m);
		} else {
			if (dir > 0)
				val++;
			else if (dir < 0)
				val--;
			changed = snd_mask_refine_set(hw_param_mask(params, var), val);
		}
	} else if (hw_is_interval(var)) {
		snd_interval_t *i = hw_param_interval(params, var);
		if (val == 0 && dir < 0) {
			changed = -EINVAL;
			snd_interval_none(i);
		} else if (dir == 0)
			changed = snd_interval_refine_set(i, val);
		else {
			snd_interval_t t;
			t.openmin = 1;
			t.openmax = 1;
			t.empty = 0;
			t.integer = 0;
			if (dir < 0) {
				t.min = val - 1;
				t.max = val;
			} else {
				t.min = val;
				t.max = val+1;
			}
			changed = snd_interval_refine(i, &t);
		}
	} else {
		assert(0);
		return -EINVAL;
	}
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}

/* Inside configuration space defined by PARAMS remove from PAR all 
   values != VAL. Reduce configuration space accordingly.
   Return VAL or -EINVAL if the configuration space is empty
*/
int snd_pcm_hw_param_set(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
			 snd_pcm_hw_param_t var, unsigned int val, int dir)
{
	int changed = _snd_pcm_hw_param_set(params, var, val, dir);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		if (err < 0)
			return err;
	}
	return snd_pcm_hw_param_value(params, var, 0);
}

int _snd_pcm_hw_param_mask(snd_pcm_hw_params_t *params,
			   snd_pcm_hw_param_t var, const snd_mask_t *val)
{
	int changed;
	assert(hw_is_mask(var));
	changed = snd_mask_refine(hw_param_mask(params, var), val);
	if (changed) {
		params->cmask |= 1 << var;
		params->rmask |= 1 << var;
	}
	return changed;
}

/* Inside configuration space defined by PARAMS remove from PAR all values
   not contained in MASK. Reduce configuration space accordingly.
   This function can be called only for SNDRV_PCM_HW_PARAM_ACCESS,
   SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_HW_PARAM_SUBFORMAT.
   Return 0 on success or -EINVAL
   if the configuration space is empty
*/
int snd_pcm_hw_param_mask(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
			  snd_pcm_hw_param_t var, const snd_mask_t *val)
{
	int changed = _snd_pcm_hw_param_mask(params, var, val);
	if (changed < 0)
		return changed;
	if (params->rmask) {
		int err = snd_pcm_hw_refine(pcm, params);
		if (err < 0)
			return err;
	}
	return 0;
}

static int boundary_sub(int a, int adir,
			int b, int bdir,
			int *c, int *cdir)
{
	adir = adir < 0 ? -1 : (adir > 0 ? 1 : 0);
	bdir = bdir < 0 ? -1 : (bdir > 0 ? 1 : 0);
	*c = a - b;
	*cdir = adir - bdir;
	if (*cdir == -2) {
		assert(*c > INT_MIN);
		(*c)--;
	} else if (*cdir == 2) {
		assert(*c < INT_MAX);
		(*c)++;
	}
	return 0;
}

static int boundary_lt(unsigned int a, int adir,
		       unsigned int b, int bdir)
{
	assert(a > 0 || adir >= 0);
	assert(b > 0 || bdir >= 0);
	if (adir < 0) {
		a--;
		adir = 1;
	} else if (adir > 0)
		adir = 1;
	if (bdir < 0) {
		b--;
		bdir = 1;
	} else if (bdir > 0)
		bdir = 1;
	return a < b || (a == b && adir < bdir);
}

/* Return 1 if min is nearer to best than max */
static int boundary_nearer(int min, int mindir,
			   int best, int bestdir,
			   int max, int maxdir)
{
	int dmin, dmindir;
	int dmax, dmaxdir;
	boundary_sub(best, bestdir, min, mindir, &dmin, &dmindir);
	boundary_sub(max, maxdir, best, bestdir, &dmax, &dmaxdir);
	return boundary_lt(dmin, dmindir, dmax, dmaxdir);
}

/* Inside configuration space defined by PARAMS set PAR to the available value
   nearest to VAL. Reduce configuration space accordingly.
   This function cannot be called for SNDRV_PCM_HW_PARAM_ACCESS,
   SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_HW_PARAM_SUBFORMAT.
   Return the value found.
 */
int snd_pcm_hw_param_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
			  snd_pcm_hw_param_t var, unsigned int best, int *dir)
{
	snd_pcm_hw_params_t save;
	int v;
	unsigned int saved_min;
	int last = 0;
	int min, max;
	int mindir, maxdir;
	int valdir = dir ? *dir : 0;
	/* FIXME */
	if (best > INT_MAX)
		best = INT_MAX;
	min = max = best;
	mindir = maxdir = valdir;
	if (maxdir > 0)
		maxdir = 0;
	else if (maxdir == 0)
		maxdir = -1;
	else {
		maxdir = 1;
		max--;
	}
	save = *params;
	saved_min = min;
	min = snd_pcm_hw_param_min(pcm, params, var, min, &mindir);
	if (min >= 0) {
		snd_pcm_hw_params_t params1;
		if (max < 0)
			goto _end;
		if ((unsigned int)min == saved_min && mindir == valdir)
			goto _end;
		params1 = save;
		max = snd_pcm_hw_param_max(pcm, &params1, var, max, &maxdir);
		if (max < 0)
			goto _end;
		if (boundary_nearer(max, maxdir, best, valdir, min, mindir)) {
			*params = params1;
			last = 1;
		}
	} else {
		*params = save;
		max = snd_pcm_hw_param_max(pcm, params, var, max, &maxdir);
		assert(max >= 0);
		last = 1;
	}
 _end:
	if (last)
		v = snd_pcm_hw_param_last(pcm, params, var, dir);
	else
		v = snd_pcm_hw_param_first(pcm, params, var, dir);
	assert(v >= 0);
	return v;
}

/* Choose one configuration from configuration space defined by PARAMS
   The configuration choosen is that obtained fixing in this order:
   first access
   first format
   first subformat
   min channels
   min rate
   min period time
   max buffer size
   min tick time
*/
int snd_pcm_hw_params_choose(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	int err;

	err = snd_pcm_hw_param_first(pcm, params, SNDRV_PCM_HW_PARAM_ACCESS, 0);
	assert(err >= 0);

	err = snd_pcm_hw_param_first(pcm, params, SNDRV_PCM_HW_PARAM_FORMAT, 0);
	assert(err >= 0);

	err = snd_pcm_hw_param_first(pcm, params, SNDRV_PCM_HW_PARAM_SUBFORMAT, 0);
	assert(err >= 0);

	err = snd_pcm_hw_param_first(pcm, params, SNDRV_PCM_HW_PARAM_CHANNELS, 0);
	assert(err >= 0);

	err = snd_pcm_hw_param_first(pcm, params, SNDRV_PCM_HW_PARAM_RATE, 0);
	assert(err >= 0);

#ifndef TARGET_OS2
	err = snd_pcm_hw_param_first(pcm, params, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 0);
	assert(err >= 0);
#endif

	err = snd_pcm_hw_param_last(pcm, params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0);
	assert(err >= 0);

	err = snd_pcm_hw_param_first(pcm, params, SNDRV_PCM_HW_PARAM_TICK_TIME, 0);
	assert(err >= 0);

	return 0;
}

#undef snd_pcm_t
#undef assert

static int snd_pcm_lib_ioctl_reset(snd_pcm_substream_t *substream,
				   void *arg)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (snd_pcm_running(substream) &&
	    snd_pcm_update_hw_ptr(substream) >= 0) {
		runtime->status->hw_ptr %= runtime->buffer_size;
		return 0;
	}
	runtime->status->hw_ptr = 0;
	return 0;
}

static int snd_pcm_lib_ioctl_channel_info(snd_pcm_substream_t *substream,
					  void *arg)
{
	snd_pcm_channel_info_t *info = arg;
	snd_pcm_runtime_t *runtime = substream->runtime;
	int width;
	if (!(runtime->info & SNDRV_PCM_INFO_MMAP)) {
		info->offset = -1;
		return 0;
	}
	width = snd_pcm_format_physical_width(runtime->format);
	if (width < 0)
		return width;
	info->offset = 0;
	switch (runtime->access) {
	case SNDRV_PCM_ACCESS_MMAP_INTERLEAVED:
	case SNDRV_PCM_ACCESS_RW_INTERLEAVED:
		info->first = info->channel * width;
		info->step = runtime->channels * width;
		break;
	case SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED:
	case SNDRV_PCM_ACCESS_RW_NONINTERLEAVED:
	{
		size_t size = runtime->dma_bytes / runtime->channels;
		info->first = info->channel * size * 8;
		info->step = width;
		break;
	}
	default:
		snd_BUG();
		break;
	}
	return 0;
}

int snd_pcm_lib_ioctl(snd_pcm_substream_t *substream,
		      unsigned int cmd, void *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL1_INFO:
		return 0;
	case SNDRV_PCM_IOCTL1_RESET:
		return snd_pcm_lib_ioctl_reset(substream, arg);
	case SNDRV_PCM_IOCTL1_CHANNEL_INFO:
		return snd_pcm_lib_ioctl_channel_info(substream, arg);
#ifdef TARGET_OS2
    case SNDRV_PCM_IOCTL1_SETVOLUME:
    case SNDRV_PCM_IOCTL1_GETVOLUME:
        return -EPERM;
#endif
	}
	return -ENXIO;
}

/*
 *  Conditions
 */

int snd_pcm_playback_ready(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	return snd_pcm_playback_avail(runtime) >= runtime->control->avail_min;
}

int snd_pcm_capture_ready(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	return snd_pcm_capture_avail(runtime) >= runtime->control->avail_min;
}

int snd_pcm_playback_data(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	return snd_pcm_playback_avail(runtime) < runtime->buffer_size;
}

int snd_pcm_playback_empty(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	return snd_pcm_playback_avail(runtime) >= runtime->buffer_size;
}

int snd_pcm_capture_empty(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	return snd_pcm_capture_avail(runtime) == 0;
}

static void snd_pcm_system_tick_set(snd_pcm_substream_t *substream, 
				    unsigned long ticks)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (ticks == 0)
		del_timer(&runtime->tick_timer);
	else
		mod_timer(&runtime->tick_timer, jiffies + ticks);
}

/* Temporary alias */
void snd_pcm_tick_set(snd_pcm_substream_t *substream, unsigned long ticks)
{
	snd_pcm_system_tick_set(substream, ticks);
}

void snd_pcm_tick_prepare(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t frames = ULONG_MAX;
	snd_pcm_uframes_t avail, dist;
	unsigned int ticks;
	u_int64_t n;
	u_int32_t r;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (runtime->silence_size > 0 &&
		    runtime->silenced_size < runtime->buffer_size) {
			snd_pcm_sframes_t noise_dist;
			noise_dist = snd_pcm_playback_hw_avail(runtime) + runtime->silenced_size;
			snd_assert(noise_dist <= runtime->silence_threshold, );
			frames = noise_dist - runtime->silence_threshold;
		}
		avail = snd_pcm_playback_avail(runtime);
	} else {
		avail = snd_pcm_capture_avail(runtime);
	}
	if (avail < runtime->control->avail_min) {
		snd_pcm_sframes_t n = runtime->control->avail_min - avail;
		if (n > 0 && frames > n)
			frames = n;
	}
	if (avail < runtime->buffer_size) {
		snd_pcm_sframes_t n = runtime->buffer_size - avail;
		if (n > 0 && frames > n)
			frames = n;
	}
	if (frames == ULONG_MAX) {
		snd_pcm_tick_set(substream, 0);
		return;
	}
	dist = runtime->status->hw_ptr - runtime->hw_ptr_base;
	/* Distance to next interrupt */
	dist = runtime->period_size - dist % runtime->period_size;
	if (dist <= frames) {
		snd_pcm_tick_set(substream, 0);
		return;
	}
	n = frames;
	n *= 1000000;
	div64_32(&n, runtime->tick_time * runtime->rate, &r);
	ticks = n + 1;
	if (ticks < runtime->sleep_min)
		ticks = runtime->sleep_min;
	snd_pcm_tick_set(substream, (unsigned long) n + 1);
}

void snd_pcm_tick_elapsed(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime;
	snd_assert(substream != NULL, return);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return);

	spin_lock_irq(&runtime->lock);
	if (!snd_pcm_running(substream) ||
	    snd_pcm_update_hw_ptr(substream) < 0)
		goto _end;
	if (runtime->sleep_min)
		snd_pcm_tick_prepare(substream);
 _end:
	spin_unlock_irq(&runtime->lock);
}

void snd_pcm_period_elapsed(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime;
	snd_assert(substream != NULL, return);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return);

	if (runtime->transfer_ack_begin)
		runtime->transfer_ack_begin(substream);

	spin_lock(&runtime->lock);
	if (!snd_pcm_running(substream) ||
	    snd_pcm_update_hw_ptr_interrupt(substream) < 0)
		goto _end;

	if (runtime->timer_running)
		snd_timer_interrupt(substream->timer, 1);
	if (runtime->sleep_min)
		snd_pcm_tick_prepare(substream);
 _end:
	spin_unlock(&runtime->lock);
	if (runtime->transfer_ack_end)
		runtime->transfer_ack_end(substream);
	snd_kill_fasync(&runtime->fasync, SIGIO, POLL_IN);
}

static int snd_pcm_lib_write_transfer(snd_pcm_substream_t *substream,
				      unsigned int hwoff,
				      void *data, unsigned int off,
				      snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;
	char *buf = (char *) data + frames_to_bytes(runtime, off);
	if (substream->ops->copy) {
		if ((err = substream->ops->copy(substream, -1, hwoff, buf, frames)) < 0)
			return err;
	} else {
		char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, hwoff);
		snd_assert(runtime->dma_area, return -EFAULT);
		if (copy_from_user(hwbuf, buf, frames_to_bytes(runtime, frames)))
			return -EFAULT;
	}
	return 0;
}
 
typedef int (*transfer_f)(snd_pcm_substream_t *substream, unsigned int hwoff,
			  void *data, unsigned int off, snd_pcm_uframes_t size);

static snd_pcm_sframes_t snd_pcm_lib_write1(snd_pcm_substream_t *substream, 
					    const void *data, snd_pcm_uframes_t size,
					    int nonblock,
					    transfer_f transfer)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t xfer = 0;
	snd_pcm_uframes_t offset = 0;
	int err = 0;

	if (size == 0)
		return 0;
	if (size > runtime->xfer_align)
		size -= size % runtime->xfer_align;

	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_RUNNING:
		break;
	case SNDRV_PCM_STATE_XRUN:
		err = -EPIPE;
		goto _end_unlock;
	default:
		err = -EBADFD;
		goto _end_unlock;
	}

	while (size > 0) {
		snd_pcm_uframes_t frames, appl_ptr, appl_ofs;
		snd_pcm_uframes_t avail = snd_pcm_playback_avail(runtime);
		snd_pcm_uframes_t cont;
		if (runtime->status->state == SNDRV_PCM_STATE_PREPARED) {
			if (avail == 0) {
				err = -EPIPE;
				goto _end_unlock;
			}
		} else if (avail == 0 ||
			   (size >= runtime->xfer_align && 
			    avail < runtime->xfer_align)) {
			wait_queue_t wait;
			enum { READY, SIGNALED, ERROR, EXPIRED } state;
			if (nonblock) {
				err = -EAGAIN;
				goto _end_unlock;
			}

			init_waitqueue_entry(&wait, current);
			add_wait_queue(&runtime->sleep, &wait);
			while (1) {
				set_current_state(TASK_INTERRUPTIBLE);
				if (signal_pending(current)) {
					state = SIGNALED;
					break;
				}
				spin_unlock_irq(&runtime->lock);
				if (schedule_timeout(10 * HZ) == 0) {
					spin_lock_irq(&runtime->lock);
					state = EXPIRED;
					break;
				}
				spin_lock_irq(&runtime->lock);
				if (runtime->status->state == SNDRV_PCM_STATE_XRUN) {
					state = ERROR;
					break;
				}
				avail = snd_pcm_playback_avail(runtime);
				if (avail >= runtime->control->avail_min) {
					state = READY;
					break;
				}
			}
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&runtime->sleep, &wait);

			switch (state) {
			case ERROR:
				err = -EPIPE;
				goto _end_unlock;
			case SIGNALED:
				err = -ERESTARTSYS;
				goto _end_unlock;
			case EXPIRED:
				snd_printd("playback write error (DMA or IRQ trouble?)\n");
				err = -EIO;
				goto _end_unlock;
			default:
				break;
			}
		}
		if (avail > runtime->xfer_align)
			avail -= avail % runtime->xfer_align;
		frames = size;
		if (frames > avail)
			frames = avail;
		cont = runtime->buffer_size - runtime->control->appl_ptr % runtime->buffer_size;
		if (frames > cont)
			frames = cont;
		snd_assert(frames != 0,
			   spin_unlock_irq(&runtime->lock);
			   return -EINVAL);
		appl_ptr = runtime->control->appl_ptr;
		appl_ofs = appl_ptr % runtime->buffer_size;
		spin_unlock_irq(&runtime->lock);
		if ((err = transfer(substream, appl_ofs, (void *)data, offset, frames)) < 0)
			goto _end;
		spin_lock_irq(&runtime->lock);
		if (runtime->status->state == SNDRV_PCM_STATE_XRUN) {
			err = -EPIPE;
			goto _end_unlock;
		}
		appl_ptr += frames;
		if (appl_ptr >= runtime->boundary) {
			runtime->control->appl_ptr = 0;
		} else {
			runtime->control->appl_ptr = appl_ptr;
		}

		offset += frames;
		size -= frames;
		xfer += frames;
		if (runtime->status->state == SNDRV_PCM_STATE_PREPARED &&
		    snd_pcm_playback_hw_avail(runtime) >= runtime->start_threshold) {
			err = snd_pcm_start(substream);
			if (err < 0)
				goto _end_unlock;
		}
		if (runtime->sleep_min &&
		    runtime->status->state == SNDRV_PCM_STATE_RUNNING)
			snd_pcm_tick_prepare(substream);
	}
 _end_unlock:
	spin_unlock_irq(&runtime->lock);
 _end:
	return xfer > 0 ? xfer : err;
}

snd_pcm_sframes_t snd_pcm_lib_write(snd_pcm_substream_t *substream, const void *buf, snd_pcm_uframes_t size)
{
	snd_pcm_runtime_t *runtime;
	int nonblock;

	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
	snd_assert(substream->ops->copy != NULL || runtime->dma_area != NULL, return -EINVAL);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	snd_assert(substream->ffile != NULL, return -ENXIO);
	nonblock = !!(substream->ffile->f_flags & O_NONBLOCK);
#ifdef CONFIG_SND_OSSEMUL
	if (substream->oss.oss) {
		snd_pcm_oss_setup_t *setup = substream->oss.setup;
		if (setup != NULL) {
			if (setup->nonblock)
				nonblock = 1;
			else if (setup->block)
				nonblock = 0;
		}
	}
#endif

	if (runtime->access != SNDRV_PCM_ACCESS_RW_INTERLEAVED)
		return -EINVAL;
	return snd_pcm_lib_write1(substream, buf, size, nonblock,
				  snd_pcm_lib_write_transfer);
}

static int snd_pcm_lib_writev_transfer(snd_pcm_substream_t *substream,
				       unsigned int hwoff,
				       void *data, unsigned int off,
				       snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;
	void **bufs = data;
	int channels = runtime->channels;
	int c;
	if (substream->ops->copy) {
		snd_assert(substream->ops->silence != NULL, return -EINVAL);
		for (c = 0; c < channels; ++c, ++bufs) {
			if (*bufs == NULL) {
				if ((err = substream->ops->silence(substream, c, hwoff, frames)) < 0)
					return err;
			} else {
#ifdef TARGET_OS2
				char *buf = (char *)*bufs + samples_to_bytes(runtime, off);
#else
				char *buf = *bufs + samples_to_bytes(runtime, off);
#endif
				if ((err = substream->ops->copy(substream, c, hwoff, buf, frames)) < 0)
					return err;
			}
		}
	} else {
		/* default transfer behaviour */
		size_t dma_csize = runtime->dma_bytes / channels;
		snd_assert(runtime->dma_area, return -EFAULT);
		for (c = 0; c < channels; ++c, ++bufs) {
			char *hwbuf = runtime->dma_area + (c * dma_csize) + samples_to_bytes(runtime, hwoff);
			if (*bufs == NULL) {
				snd_pcm_format_set_silence(runtime->format, hwbuf, frames);
			} else {
#ifdef TARGET_OS2
				char *buf = (char *)*bufs + samples_to_bytes(runtime, off);
#else
				char *buf = *bufs + samples_to_bytes(runtime, off);
#endif
				if (copy_from_user(hwbuf, buf, samples_to_bytes(runtime, frames)))
					return -EFAULT;
			}
		}
	}
	return 0;
}
 
snd_pcm_sframes_t snd_pcm_lib_writev(snd_pcm_substream_t *substream, void **bufs,
				     snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime;
	int nonblock;

	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
	snd_assert(substream->ops->copy != NULL || runtime->dma_area != NULL, return -EINVAL);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	snd_assert(substream->ffile != NULL, return -ENXIO);
	nonblock = !!(substream->ffile->f_flags & O_NONBLOCK);
#ifdef CONFIG_SND_OSSEMUL
	if (substream->oss.oss) {
		snd_pcm_oss_setup_t *setup = substream->oss.setup;
		if (setup != NULL) {
			if (setup->nonblock)
				nonblock = 1;
			else if (setup->block)
				nonblock = 0;
		}
	}
#endif

	if (runtime->access != SNDRV_PCM_ACCESS_RW_NONINTERLEAVED)
		return -EINVAL;
	return snd_pcm_lib_write1(substream, bufs, frames, nonblock,
				  snd_pcm_lib_writev_transfer);
}

static int snd_pcm_lib_read_transfer(snd_pcm_substream_t *substream, 
				     unsigned int hwoff,
				     void *data, unsigned int off,
				     snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;
	char *buf = (char *) data + frames_to_bytes(runtime, off);
	if (substream->ops->copy) {
		if ((err = substream->ops->copy(substream, -1, hwoff, buf, frames)) < 0)
			return err;
	} else {
		char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, hwoff);
		snd_assert(runtime->dma_area, return -EFAULT);
		if (copy_to_user(buf, hwbuf, frames_to_bytes(runtime, frames)))
			return -EFAULT;
	}
	return 0;
}

static snd_pcm_sframes_t snd_pcm_lib_read1(snd_pcm_substream_t *substream, void *data, snd_pcm_uframes_t size, int nonblock,
					   transfer_f transfer)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t xfer = 0;
	snd_pcm_uframes_t offset = 0;
	int err = 0;

	if (size == 0)
		return 0;
	if (size > runtime->xfer_align)
		size -= size % runtime->xfer_align;

	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
		if (size >= runtime->start_threshold) {
			err = snd_pcm_start(substream);
			if (err < 0)
				goto _end_unlock;
		}
		break;
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_RUNNING:
		break;
	case SNDRV_PCM_STATE_XRUN:
		err = -EPIPE;
		goto _end_unlock;
	default:
		err = -EBADFD;
		goto _end_unlock;
	}

	while (size > 0) {
		snd_pcm_uframes_t frames, appl_ptr, appl_ofs;
		snd_pcm_uframes_t avail = snd_pcm_capture_avail(runtime);
		snd_pcm_uframes_t cont;
		if (runtime->status->state == SNDRV_PCM_STATE_DRAINING) {
			if (avail == 0) {
				runtime->status->state = SNDRV_PCM_STATE_SETUP;
				err = -EPIPE;
				goto _end_unlock;
			}
		} else if (avail == 0 ||
			   (size >= runtime->xfer_align && 
			    avail < runtime->xfer_align)) {
			wait_queue_t wait;
			enum { READY, SIGNALED, ERROR, EXPIRED } state;
			if (nonblock) {
				err = -EAGAIN;
				goto _end_unlock;
			}

			init_waitqueue_entry(&wait, current);
			add_wait_queue(&runtime->sleep, &wait);
			while (1) {
				set_current_state(TASK_INTERRUPTIBLE);
				if (signal_pending(current)) {
					state = SIGNALED;
					break;
				}
				spin_unlock_irq(&runtime->lock);
				if (schedule_timeout(10 * HZ) == 0) {
					spin_lock_irq(&runtime->lock);
					state = EXPIRED;
					break;
				}
				spin_lock_irq(&runtime->lock);
				if (runtime->status->state == SNDRV_PCM_STATE_XRUN) {
					state = ERROR;
					break;
				}
				avail = snd_pcm_capture_avail(runtime);
				if (avail >= runtime->control->avail_min) {
					state = READY;
					break;
				}
			}
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&runtime->sleep, &wait);

			switch (state) {
			case ERROR:
				err = -EPIPE;
				goto _end_unlock;
			case SIGNALED:
				err = -ERESTARTSYS;
				goto _end_unlock;
			case EXPIRED:
				snd_printd("capture read error (DMA or IRQ trouble?)\n");
				err = -EIO;
				goto _end_unlock;
			default:
				break;
			}
		}
		if (avail > runtime->xfer_align)
			avail -= avail % runtime->xfer_align;
		frames = size;
		if (frames > avail)
			frames = avail;
		cont = runtime->buffer_size - runtime->control->appl_ptr % runtime->buffer_size;
		if (frames > cont)
			frames = cont;
		snd_assert(frames != 0,
			   spin_unlock_irq(&runtime->lock);
			   return -EINVAL);
		appl_ptr = runtime->control->appl_ptr;
		appl_ofs = appl_ptr % runtime->buffer_size;
		spin_unlock_irq(&runtime->lock);
		if ((err = transfer(substream, appl_ofs, (void *)data, offset, frames)) < 0)
			goto _end;
		spin_lock_irq(&runtime->lock);
		if (runtime->status->state == SNDRV_PCM_STATE_XRUN) {
			err = -EPIPE;
			goto _end_unlock;
		}
		appl_ptr += frames;
		if (appl_ptr >= runtime->boundary) {
			runtime->control->appl_ptr = 0;
		} else {
			runtime->control->appl_ptr = appl_ptr;
		}

		offset += frames;
		size -= frames;
		xfer += frames;
		if (runtime->sleep_min &&
		    runtime->status->state == SNDRV_PCM_STATE_RUNNING)
			snd_pcm_tick_prepare(substream);
	}
 _end_unlock:
	spin_unlock_irq(&runtime->lock);
 _end:
	return xfer > 0 ? xfer : err;
}

snd_pcm_sframes_t snd_pcm_lib_read(snd_pcm_substream_t *substream, void *buf, snd_pcm_uframes_t size)
{
	snd_pcm_runtime_t *runtime;
	int nonblock;
	
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
	snd_assert(substream->ops->copy != NULL || runtime->dma_area != NULL, return -EINVAL);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	snd_assert(substream->ffile != NULL, return -ENXIO);
	nonblock = !!(substream->ffile->f_flags & O_NONBLOCK);
#ifdef CONFIG_SND_OSSEMUL
	if (substream->oss.oss) {
		snd_pcm_oss_setup_t *setup = substream->oss.setup;
		if (setup != NULL) {
			if (setup->nonblock)
				nonblock = 1;
			else if (setup->block)
				nonblock = 0;
		}
	}
#endif
	if (runtime->access != SNDRV_PCM_ACCESS_RW_INTERLEAVED)
		return -EINVAL;
	return snd_pcm_lib_read1(substream, buf, size, nonblock, snd_pcm_lib_read_transfer);
}

static int snd_pcm_lib_readv_transfer(snd_pcm_substream_t *substream,
				      unsigned int hwoff,
				      void *data, unsigned int off,
				      snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;
	void **bufs = data;
	int channels = runtime->channels;
	int c;
	if (substream->ops->copy) {
		for (c = 0; c < channels; ++c, ++bufs) {
			char *buf;
			if (*bufs == NULL)
				continue;
#ifdef TARGET_OS2
			buf = (char *)*bufs + samples_to_bytes(runtime, off);
#else
			buf = *bufs + samples_to_bytes(runtime, off);
#endif
			if ((err = substream->ops->copy(substream, c, hwoff, buf, frames)) < 0)
				return err;
		}
	} else {
		snd_pcm_uframes_t dma_csize = runtime->dma_bytes / channels;
		snd_assert(runtime->dma_area, return -EFAULT);
		for (c = 0; c < channels; ++c, ++bufs) {
			char *hwbuf, *buf;
			if (*bufs == NULL)
				continue;

			hwbuf = runtime->dma_area + (c * dma_csize) + samples_to_bytes(runtime, hwoff);
#ifdef TARGET_OS2
			buf = (char *)*bufs + samples_to_bytes(runtime, off);
#else
			buf = *bufs + samples_to_bytes(runtime, off);
#endif
			if (copy_to_user(buf, hwbuf, samples_to_bytes(runtime, frames)))
				return -EFAULT;
		}
	}
	return 0;
}
 
snd_pcm_sframes_t snd_pcm_lib_readv(snd_pcm_substream_t *substream, void **bufs,
				    snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime;
	int nonblock;

	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
	snd_assert(substream->ops->copy != NULL || runtime->dma_area != NULL, return -EINVAL);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	snd_assert(substream->ffile != NULL, return -ENXIO);
	nonblock = !!(substream->ffile->f_flags & O_NONBLOCK);
#ifdef CONFIG_SND_OSSEMUL
	if (substream->oss.oss) {
		snd_pcm_oss_setup_t *setup = substream->oss.setup;
		if (setup != NULL) {
			if (setup->nonblock)
				nonblock = 1;
			else if (setup->block)
				nonblock = 0;
		}
	}
#endif

	if (runtime->access != SNDRV_PCM_ACCESS_RW_NONINTERLEAVED)
		return -EINVAL;
	return snd_pcm_lib_read1(substream, bufs, frames, nonblock, snd_pcm_lib_readv_transfer);
}

/*
 *  Exported symbols
 */

EXPORT_SYMBOL(snd_interval_refine);
EXPORT_SYMBOL(snd_interval_list);
EXPORT_SYMBOL(snd_interval_ratnum);
EXPORT_SYMBOL(snd_interval_ratden);
EXPORT_SYMBOL(snd_interval_muldivk);
EXPORT_SYMBOL(snd_interval_mulkdiv);
EXPORT_SYMBOL(snd_interval_div);
EXPORT_SYMBOL(_snd_pcm_hw_params_any);
EXPORT_SYMBOL(_snd_pcm_hw_param_min);
EXPORT_SYMBOL(_snd_pcm_hw_param_set);
EXPORT_SYMBOL(_snd_pcm_hw_param_setempty);
EXPORT_SYMBOL(_snd_pcm_hw_param_setinteger);
EXPORT_SYMBOL(snd_pcm_hw_param_value_min);
EXPORT_SYMBOL(snd_pcm_hw_param_value_max);
EXPORT_SYMBOL(snd_pcm_hw_param_mask);
EXPORT_SYMBOL(snd_pcm_hw_param_first);
EXPORT_SYMBOL(snd_pcm_hw_param_last);
EXPORT_SYMBOL(snd_pcm_hw_param_near);
EXPORT_SYMBOL(snd_pcm_hw_refine);
EXPORT_SYMBOL(snd_pcm_hw_constraints_init);
EXPORT_SYMBOL(snd_pcm_hw_constraints_complete);
EXPORT_SYMBOL(snd_pcm_hw_constraint_list);
EXPORT_SYMBOL(snd_pcm_hw_constraint_step);
EXPORT_SYMBOL(snd_pcm_hw_constraint_ratnums);
EXPORT_SYMBOL(snd_pcm_hw_constraint_ratdens);
EXPORT_SYMBOL(snd_pcm_hw_constraint_msbits);
EXPORT_SYMBOL(snd_pcm_hw_constraint_minmax);
EXPORT_SYMBOL(snd_pcm_hw_constraint_integer);
EXPORT_SYMBOL(snd_pcm_hw_rule_add);
EXPORT_SYMBOL(snd_pcm_set_ops);
EXPORT_SYMBOL(snd_pcm_set_sync);
EXPORT_SYMBOL(snd_pcm_lib_ioctl);
EXPORT_SYMBOL(snd_pcm_playback_ready);
EXPORT_SYMBOL(snd_pcm_capture_ready);
EXPORT_SYMBOL(snd_pcm_playback_data);
EXPORT_SYMBOL(snd_pcm_capture_empty);
EXPORT_SYMBOL(snd_pcm_stop);
EXPORT_SYMBOL(snd_pcm_period_elapsed);
EXPORT_SYMBOL(snd_pcm_lib_write);
EXPORT_SYMBOL(snd_pcm_lib_read);
EXPORT_SYMBOL(snd_pcm_lib_writev);
EXPORT_SYMBOL(snd_pcm_lib_readv);
EXPORT_SYMBOL(snd_pcm_lib_buffer_bytes);
EXPORT_SYMBOL(snd_pcm_lib_period_bytes);
/* pcm_memory.c */
EXPORT_SYMBOL(snd_pcm_lib_preallocate_pages);
EXPORT_SYMBOL(snd_pcm_lib_preallocate_pages_for_all);
EXPORT_SYMBOL(snd_pcm_lib_preallocate_free);
EXPORT_SYMBOL(snd_pcm_lib_preallocate_free_for_all);
EXPORT_SYMBOL(snd_pcm_lib_malloc_pages);
EXPORT_SYMBOL(snd_pcm_lib_free_pages);
#ifdef CONFIG_PCI
EXPORT_SYMBOL(snd_pcm_lib_preallocate_pci_pages);
EXPORT_SYMBOL(snd_pcm_lib_preallocate_pci_pages_for_all);
#endif
