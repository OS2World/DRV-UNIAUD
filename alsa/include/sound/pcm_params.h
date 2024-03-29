#ifndef __PCM_PARAMS_H
#define __PCM_PARAMS_H

/*
 *  PCM params helpers
 *  Copyright (c) by Abramo Bagnara <abramo@alsa-project.org>
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

extern int snd_pcm_hw_param_mask(snd_pcm_substream_t *pcm, snd_pcm_hw_params_t *params,
				 snd_pcm_hw_param_t var, const snd_mask_t *val);
extern unsigned int snd_pcm_hw_param_value_min(const snd_pcm_hw_params_t *params,
					       snd_pcm_hw_param_t var, int *dir);
extern unsigned int snd_pcm_hw_param_value_max(const snd_pcm_hw_params_t *params,
					       snd_pcm_hw_param_t var, int *dir);
extern int _snd_pcm_hw_param_min(snd_pcm_hw_params_t *params,
				 snd_pcm_hw_param_t var, unsigned int val, int dir);
extern int _snd_pcm_hw_param_setinteger(snd_pcm_hw_params_t *params,
					snd_pcm_hw_param_t var);
extern int _snd_pcm_hw_param_set(snd_pcm_hw_params_t *params,
				 snd_pcm_hw_param_t var, unsigned int val, int dir);

/* To share the same code we have  alsa-lib */
#define snd_mask_bits(mask) (*(mask))
#define INLINE static inline

#define assert(a)

INLINE unsigned int ld2(u_int32_t v)
{
        unsigned r = 0;

        if (v >= 0x10000) {
                v >>= 16;
                r += 16;
        }
        if (v >= 0x100) {
                v >>= 8;
                r += 8;
        }
        if (v >= 0x10) {
                v >>= 4;
                r += 4;
        }
        if (v >= 4) {
                v >>= 2;
                r += 2;
        }
        if (v >= 2)
                r++;
        return r;
}

INLINE size_t snd_mask_sizeof(void)
{
	return sizeof(snd_mask_t);
}

INLINE void snd_mask_none(snd_mask_t *mask)
{
	snd_mask_bits(mask) = 0;
}

INLINE void snd_mask_any(snd_mask_t *mask)
{
	snd_mask_bits(mask) = ~0U;
}

INLINE void snd_mask_load(snd_mask_t *mask, unsigned int msk)
{
	snd_mask_bits(mask) = msk;
}

INLINE int snd_mask_empty(const snd_mask_t *mask)
{
	return snd_mask_bits(mask) == 0;
}

INLINE unsigned int snd_mask_min(const snd_mask_t *mask)
{
	assert(!snd_mask_empty(mask));
	return ffs(snd_mask_bits(mask)) - 1;
}

INLINE unsigned int snd_mask_max(const snd_mask_t *mask)
{
	assert(!snd_mask_empty(mask));
	return ld2(snd_mask_bits(mask));
}

INLINE void snd_mask_set(snd_mask_t *mask, unsigned int val)
{
	assert(val <= SND_MASK_MAX);
	snd_mask_bits(mask) |= (1U << val);
}

INLINE void snd_mask_reset(snd_mask_t *mask, unsigned int val)
{
	assert(val <= SND_MASK_MAX);
	snd_mask_bits(mask) &= ~(1U << val);
}

INLINE void snd_mask_set_range(snd_mask_t *mask, unsigned int from, unsigned int to)
{
	assert(to <= SND_MASK_MAX && from <= to);
	snd_mask_bits(mask) |= ((1U << (from - to + 1)) - 1) << from;
}

INLINE void snd_mask_reset_range(snd_mask_t *mask, unsigned int from, unsigned int to)
{
	assert(to <= SND_MASK_MAX && from <= to);
	snd_mask_bits(mask) &= ~(((1U << (from - to + 1)) - 1) << from);
}

INLINE void snd_mask_leave(snd_mask_t *mask, unsigned int val)
{
	assert(val <= SND_MASK_MAX);
	snd_mask_bits(mask) &= 1U << val;
}

INLINE void snd_mask_intersect(snd_mask_t *mask, const snd_mask_t *v)
{
	snd_mask_bits(mask) &= snd_mask_bits(v);
}

INLINE int snd_mask_eq(const snd_mask_t *mask, const snd_mask_t *v)
{
	return snd_mask_bits(mask) == snd_mask_bits(v);
}

INLINE void snd_mask_copy(snd_mask_t *mask, const snd_mask_t *v)
{
	snd_mask_bits(mask) = snd_mask_bits(v);
}

INLINE int snd_mask_test(const snd_mask_t *mask, unsigned int val)
{
	assert(val <= SND_MASK_MAX);
	return snd_mask_bits(mask) & (1U << val);
}

INLINE int snd_mask_single(const snd_mask_t *mask)
{
	assert(!snd_mask_empty(mask));
	return !(snd_mask_bits(mask) & (snd_mask_bits(mask) - 1));
}

INLINE int snd_mask_refine(snd_mask_t *mask, const snd_mask_t *v)
{
	snd_mask_t old;
	assert(!snd_mask_empty(mask));
	snd_mask_copy(&old, mask);
	snd_mask_intersect(mask, v);
	if (snd_mask_empty(mask))
		return -EINVAL;
	return !snd_mask_eq(mask, &old);
}

INLINE int snd_mask_refine_first(snd_mask_t *mask)
{
	assert(!snd_mask_empty(mask));
	if (snd_mask_single(mask))
		return 0;
	snd_mask_leave(mask, snd_mask_min(mask));
	return 1;
}

INLINE int snd_mask_refine_last(snd_mask_t *mask)
{
	assert(!snd_mask_empty(mask));
	if (snd_mask_single(mask))
		return 0;
	snd_mask_leave(mask, snd_mask_max(mask));
	return 1;
}

INLINE int snd_mask_refine_min(snd_mask_t *mask, unsigned int val)
{
	assert(!snd_mask_empty(mask));
	if (snd_mask_min(mask) >= val)
		return 0;
	snd_mask_reset_range(mask, 0, val - 1);
	if (snd_mask_empty(mask))
		return -EINVAL;
	return 1;
}

INLINE int snd_mask_refine_max(snd_mask_t *mask, unsigned int val)
{
	assert(!snd_mask_empty(mask));
	if (snd_mask_max(mask) <= val)
		return 0;
	snd_mask_reset_range(mask, val + 1, SND_MASK_MAX);
	if (snd_mask_empty(mask))
		return -EINVAL;
	return 1;
}

INLINE int snd_mask_refine_set(snd_mask_t *mask, unsigned int val)
{
	int changed;
	assert(!snd_mask_empty(mask));
	changed = !snd_mask_single(mask);
	snd_mask_leave(mask, val);
	if (snd_mask_empty(mask))
		return -EINVAL;
	return changed;
}

INLINE int snd_mask_value(const snd_mask_t *mask)
{
	assert(!snd_mask_empty(mask));
	return snd_mask_min(mask);
}

INLINE void snd_interval_any(snd_interval_t *i)
{
	i->min = 0;
	i->openmin = 0;
	i->max = UINT_MAX;
	i->openmax = 0;
	i->integer = 0;
	i->empty = 0;
}

INLINE void snd_interval_none(snd_interval_t *i)
{
	i->empty = 1;
}

INLINE int snd_interval_checkempty(const snd_interval_t *i)
{
	return (i->min > i->max ||
		(i->min == i->max && (i->openmin || i->openmax)));
}

INLINE int snd_interval_empty(const snd_interval_t *i)
{
	return i->empty;
}

INLINE int snd_interval_single(const snd_interval_t *i)
{
	assert(!snd_interval_empty(i));
	return (i->min == i->max || 
		(i->min + 1 == i->max && i->openmax));
}

INLINE int snd_interval_value(const snd_interval_t *i)
{
	assert(snd_interval_single(i));
	return i->min;
}

INLINE int snd_interval_min(const snd_interval_t *i)
{
	assert(!snd_interval_empty(i));
	return i->min;
}

INLINE int snd_interval_max(const snd_interval_t *i)
{
	unsigned int v;
	assert(!snd_interval_empty(i));
	v = i->max;
	if (i->openmax)
		v--;
	return v;
}

INLINE int snd_interval_test(const snd_interval_t *i, unsigned int val)
{
	return !((i->min > val || (i->min == val && i->openmin) ||
		  i->max < val || (i->max == val && i->openmax)));
}

INLINE void snd_interval_copy(snd_interval_t *d, const snd_interval_t *s)
{
	*d = *s;
}

INLINE int snd_interval_setinteger(snd_interval_t *i)
{
	if (i->integer)
		return 0;
	if (i->openmin && i->openmax && i->min == i->max)
		return -EINVAL;
	i->integer = 1;
	return 1;
}

INLINE int snd_interval_eq(const snd_interval_t *i1, const snd_interval_t *i2)
{
	if (i1->empty)
		return i2->empty;
	if (i2->empty)
		return i1->empty;
	return i1->min == i2->min && i1->openmin == i2->openmin &&
		i1->max == i2->max && i1->openmax == i2->openmax;
}

INLINE unsigned int add(unsigned int a, unsigned int b)
{
	if (a >= UINT_MAX - b)
		return UINT_MAX;
	return a + b;
}

INLINE unsigned int sub(unsigned int a, unsigned int b)
{
	if (a > b)
		return a - b;
	return 0;
}

#undef INLINE
#undef assert

#endif
