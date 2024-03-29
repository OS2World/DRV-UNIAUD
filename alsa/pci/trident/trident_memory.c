/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *  Copyright (c) by Scott McNab <sdm@fractalgraphics.com.au>
 *
 *  Trident 4DWave-NX memory page allocation (TLB area)
 *  Trident chip can handle only 16MByte of the memory at the same time.
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
#include <sound/trident.h>

/* page arguments of these two macros are Trident page (4096 bytes), not like
 * aligned pages in others
 */
#define __set_tlb_bus(trident,page,ptr,addr) \
	do { (trident)->tlb.entries[page] = (addr) & ~(SNDRV_TRIDENT_PAGE_SIZE-1); \
	     (trident)->tlb.shadow_entries[page] = (ptr); } while (0)
#define __tlb_to_ptr(trident,page) \
	(void*)((trident)->tlb.shadow_entries[page])
#define __tlb_to_addr(trident,page) \
	(dma_addr_t)((trident->tlb.entries[page]) & ~(SNDRV_TRIDENT_PAGE_SIZE - 1))

#if PAGE_SIZE == 4096
/* page size == SNDRV_TRIDENT_PAGE_SIZE */
#define ALIGN_PAGE_SIZE		PAGE_SIZE	/* minimum page size for allocation */
#define MAX_ALIGN_PAGES		SNDRV_TRIDENT_MAX_PAGES	/* maxmium aligned pages */
/* fill TLB entrie(s) corresponding to page with ptr */
#define set_tlb_bus(trident,page,ptr,addr) __set_tlb_bus(trident,page,ptr,addr)
/* fill TLB entrie(s) corresponding to page with silence pointer */
#define set_silent_tlb(trident,page)	__set_tlb_bus(trident, page, (unsigned long)trident->tlb.silent_page, trident->tlb.silent_page_dmaaddr)
/* get aligned page from offset address */
#define get_aligned_page(offset)	((offset) >> 12)
/* get offset address from aligned page */
#define aligned_page_offset(page)	((page) << 12)
/* get buffer address from aligned page */
#define page_to_ptr(trident,page)	__tlb_to_ptr(trident, page)
/* get PCI physical address from aligned page */
#define page_to_addr(trident,page)	__tlb_to_addr(trident, page)

#elif PAGE_SIZE == 8192
/* page size == SNDRV_TRIDENT_PAGE_SIZE x 2*/
#define ALIGN_PAGE_SIZE		PAGE_SIZE
#define MAX_ALIGN_PAGES		(SNDRV_TRIDENT_MAX_PAGES / 2)
#define get_aligned_page(offset)	((offset) >> 13)
#define aligned_page_offset(page)	((page) << 13)
#define page_to_ptr(trident,page)	__tlb_to_ptr(trident, (page) << 1)

/* fill TLB entries -- we need to fill two entries */
static inline void set_tlb_bus(trident_t *trident, int page, unsigned long ptr, dma_addr_t addr)
{
	page <<= 1;
	__set_tlb_bus(trident, page, ptr, addr);
	__set_tlb_bus(trident, page+1, ptr + SNDRV_TRIDENT_PAGE_SIZE, addr + SNDRV_TRIDENT_PAGE_SIZE);
}
static inline void set_silent_tlb(trident_t *trident, int page)
{
	page <<= 1;
	__set_tlb_bus(trident, page, (unsigned long)trident->tlb.silent_page, trident->tlb.silent_page_dmaaddr);
	__set_tlb_bus(trident, page+1, (unsigned long)trident->tlb.silent_page, trident->tlb.silent_page_dmaaddr);
}

#else
/* arbitrary size */
#define UNIT_PAGES		(PAGE_SIZE / SNDRV_TRIDENT_PAGE_SIZE)
#define ALIGN_PAGE_SIZE		(SNDRV_TRIDENT_PAGE_SIZE * UNIT_PAGES)
#define MAX_ALIGN_PAGES		(SNDRV_TRIDENT_MAX_PAGES / UNIT_PAGES)
/* Note: if alignment doesn't match to the maximum size, the last few blocks
 * become unusable.  To use such blocks, you'll need to check the validity
 * of accessing page in set_tlb_bus and set_silent_tlb.  search_empty()
 * should also check it, too.
 */
#define get_aligned_page(offset)	((offset) / ALIGN_PAGE_SIZE)
#define aligned_page_offset(page)	((page) * ALIGN_PAGE_SIZE)
#define page_to_ptr(trident,page)	__tlb_to_ptr(trident, (page) * UNIT_PAGES)
#define page_to_addr(trident,page)	__tlb_to_addr(trident, (page) * UNIT_PAGES)

/* fill TLB entries -- UNIT_PAGES entries must be filled */
static inline void set_tlb_bus(trident_t *trident, int page, unsigned long ptr, dma_addr_t addr)
{
	int i;
	page *= UNIT_PAGES;
	for (i = 0; i < UNIT_PAGES; i++, page++) {
		__set_tlb_bus(trident, page, ptr, addr);
		ptr += SNDRV_TRIDENT_PAGE_SIZE;
		addr += SNDRV_TRIDENT_PAGE_SIZE;
	}
}
static inline void set_silent_tlb(trident_t *trident, int page)
{
	int i;
	page *= UNIT_PAGES;
	for (i = 0; i < UNIT_PAGES; i++, page++)
		__set_tlb_bus(trident, page, (unsigned long)trident->tlb.silent_page, trident->tlb.silent_page_dmaaddr);
}

#endif /* PAGE_SIZE */

/* calculate buffer pointer from offset address */
inline static void *offset_ptr(trident_t *trident, int offset)
{
	char *ptr;
	ptr = page_to_ptr(trident, get_aligned_page(offset));
	ptr += offset % ALIGN_PAGE_SIZE;
	return (void*)ptr;
}

/* first and last (aligned) pages of memory block */
#define firstpg(blk)	(((snd_trident_memblk_arg_t*)snd_util_memblk_argptr(blk))->first_page)
#define lastpg(blk)	(((snd_trident_memblk_arg_t*)snd_util_memblk_argptr(blk))->last_page)

/*
 * search empty pages which may contain given size
 */
static snd_util_memblk_t *
search_empty(snd_util_memhdr_t *hdr, int size)
{
	snd_util_memblk_t *blk, *prev;
	int page, psize;
	struct list_head *p;

	psize = get_aligned_page(size + ALIGN_PAGE_SIZE -1);
	prev = NULL;
	page = 0;
	list_for_each(p, &hdr->block) {
		blk = list_entry(p, snd_util_memblk_t, list);
		if (page + psize <= firstpg(blk))
			goto __found_pages;
		page = lastpg(blk) + 1;
	}
	if (page + psize > MAX_ALIGN_PAGES)
		return NULL;

__found_pages:
	/* create a new memory block */
	blk = __snd_util_memblk_new(hdr, psize * ALIGN_PAGE_SIZE, p->prev);
	if (blk == NULL)
		return NULL;
	blk->offset = aligned_page_offset(page); /* set aligned offset */
	firstpg(blk) = page;
	lastpg(blk) = page + psize - 1;
	return blk;
}


/*
 * check if the given pointer is valid for pages
 */
static int is_valid_page(void *pages)
{
	unsigned long ptr = (unsigned long)virt_to_phys(pages);
	if (ptr & ~0x3fffffffUL) {
		snd_printk("max memory size is 1GB!!\n");
		return 0;
	}
	if (ptr & (SNDRV_TRIDENT_PAGE_SIZE-1)) {
		snd_printk("page is not aligned\n");
		return 0;
	}
	return 1;
}

/*
 * page allocation for DMA
 */
snd_util_memblk_t *
snd_trident_alloc_pages(trident_t *trident, void *pages, dma_addr_t addr, unsigned long size)
{
	unsigned long ptr;
	snd_util_memhdr_t *hdr;
	snd_util_memblk_t *blk;
	int page;

	snd_assert(trident != NULL, return NULL);
	snd_assert(size > 0 && size < SNDRV_TRIDENT_MAX_PAGES * SNDRV_TRIDENT_PAGE_SIZE, return NULL);
	hdr = trident->tlb.memhdr;
	snd_assert(hdr != NULL, return NULL);

	if (! is_valid_page(pages))
		return NULL;

	down(&hdr->block_mutex);
	blk = search_empty(hdr, size);
	if (blk == NULL) {
		up(&hdr->block_mutex);
		return NULL;
	}
	/* set TLB entries */
	ptr = (unsigned long)pages;
	for (page = firstpg(blk); page <= lastpg(blk); page++) {
		set_tlb_bus(trident, page, ptr, addr);
		ptr += ALIGN_PAGE_SIZE;
		addr += ALIGN_PAGE_SIZE;
	}
	up(&hdr->block_mutex);
	return blk;
}


/*
 * release DMA buffer from page table
 */
int snd_trident_free_pages(trident_t *trident, snd_util_memblk_t *blk)
{
	snd_util_memhdr_t *hdr;
	int page;

	snd_assert(trident != NULL, return -EINVAL);
	snd_assert(blk != NULL, return -EINVAL);

	hdr = trident->tlb.memhdr;
	down(&hdr->block_mutex);
	/* reset TLB entries */
	for (page = firstpg(blk); page <= lastpg(blk); page++)
		set_silent_tlb(trident, page);
	/* free memory block */
	__snd_util_mem_free(hdr, blk);
	up(&hdr->block_mutex);
	return 0;
}


/*----------------------------------------------------------------
 * memory allocation using multiple pages (for synth)
 *----------------------------------------------------------------
 * Unlike the DMA allocation above, non-contiguous pages are
 * assigned to TLB.
 *----------------------------------------------------------------*/

/*
 */
static int synth_alloc_pages(trident_t *hw, snd_util_memblk_t *blk);
static int synth_free_pages(trident_t *hw, snd_util_memblk_t *blk);

/*
 * allocate a synth sample area
 */
snd_util_memblk_t *
snd_trident_synth_alloc(trident_t *hw, unsigned int size)
{
	snd_util_memblk_t *blk;
	snd_util_memhdr_t *hdr = hw->tlb.memhdr; 

	down(&hdr->block_mutex);
	blk = __snd_util_mem_alloc(hdr, size);
	if (blk == NULL) {
		up(&hdr->block_mutex);
		return NULL;
	}
	if (synth_alloc_pages(hw, blk)) {
		__snd_util_mem_free(hdr, blk);
		up(&hdr->block_mutex);
		return NULL;
	}
	up(&hdr->block_mutex);
	return blk;
}


/*
 * free a synth sample area
 */
int
snd_trident_synth_free(trident_t *hw, snd_util_memblk_t *blk)
{
	snd_util_memhdr_t *hdr = hw->tlb.memhdr; 

	down(&hdr->block_mutex);
	synth_free_pages(hw, blk);
	 __snd_util_mem_free(hdr, blk);
	up(&hdr->block_mutex);
	return 0;
}


/*
 * reset TLB entry and free kernel page
 */
static void clear_tlb(trident_t *trident, int page)
{
	void *ptr = page_to_ptr(trident, page);
	dma_addr_t addr = page_to_addr(trident, page);
	set_silent_tlb(trident, page);
	snd_free_pci_pages(trident->pci, ALIGN_PAGE_SIZE, ptr, addr);
}

/* check new allocation range */
static void get_single_page_range(snd_util_memhdr_t *hdr, snd_util_memblk_t *blk, int *first_page_ret, int *last_page_ret)
{
	struct list_head *p;
	snd_util_memblk_t *q;
	int first_page, last_page;
	first_page = firstpg(blk);
	if ((p = blk->list.prev) != &hdr->block) {
		q = list_entry(p, snd_util_memblk_t, list);
		if (lastpg(q) == first_page)
			first_page++;  /* first page was already allocated */
	}
	last_page = lastpg(blk);
	if ((p = blk->list.next) != &hdr->block) {
		q = list_entry(p, snd_util_memblk_t, list);
		if (firstpg(q) == last_page)
			last_page--; /* last page was already allocated */
	}
	*first_page_ret = first_page;
	*last_page_ret = last_page;
}

/*
 * allocate kernel pages and assign them to TLB
 */
static int synth_alloc_pages(trident_t *hw, snd_util_memblk_t *blk)
{
	int page, first_page, last_page;
	void *ptr;
	dma_addr_t addr;

	firstpg(blk) = get_aligned_page(blk->offset);
	lastpg(blk) = get_aligned_page(blk->offset + blk->size - 1);
	get_single_page_range(hw->tlb.memhdr, blk, &first_page, &last_page);

	/* allocate a kernel page for each Trident page -
	 * fortunately Trident page size and kernel PAGE_SIZE is identical!
	 */
	for (page = first_page; page <= last_page; page++) {
		ptr = snd_malloc_pci_pages(hw->pci, ALIGN_PAGE_SIZE, &addr);
		if (ptr == NULL)
			goto __fail;
		if (! is_valid_page(ptr)) {
			snd_free_pci_pages(hw->pci, ALIGN_PAGE_SIZE, ptr, addr);
			goto __fail;
		}
		set_tlb_bus(hw, page, (unsigned long)ptr, addr);
	}
	return 0;

__fail:
	/* release allocated pages */
	last_page = page - 1;
	for (page = first_page; page <= last_page; page++)
		clear_tlb(hw, page);

	return -ENOMEM;
}

/*
 * free pages
 */
static int synth_free_pages(trident_t *trident, snd_util_memblk_t *blk)
{
	int page, first_page, last_page;

	get_single_page_range(trident->tlb.memhdr, blk, &first_page, &last_page);
	for (page = first_page; page <= last_page; page++)
		clear_tlb(trident, page);

	return 0;
}

/*
 * bzero(blk + offset, size)
 */
int snd_trident_synth_bzero(trident_t *trident, snd_util_memblk_t *blk, int offset, int size)
{
	int page, nextofs, end_offset, temp, temp1;

	offset += blk->offset;
	end_offset = offset + size;
	page = get_aligned_page(offset) + 1;
	do {
		nextofs = aligned_page_offset(page);
		temp = nextofs - offset;
		temp1 = end_offset - offset;
		if (temp1 < temp)
			temp = temp1;
		memset(offset_ptr(trident, offset), 0, temp);
		offset = nextofs;
		page++;
	} while (offset < end_offset);
	return 0;
}

/*
 * copy_from_user(blk + offset, data, size)
 */
int snd_trident_synth_copy_from_user(trident_t *trident, snd_util_memblk_t *blk, int offset, const char *data, int size)
{
	int page, nextofs, end_offset, temp, temp1;

	offset += blk->offset;
	end_offset = offset + size;
	page = get_aligned_page(offset) + 1;
	do {
		nextofs = aligned_page_offset(page);
		temp = nextofs - offset;
		temp1 = end_offset - offset;
		if (temp1 < temp)
			temp = temp1;
		if (copy_from_user(offset_ptr(trident, offset), data, temp))
			return -EFAULT;
		offset = nextofs;
		data += temp;
		page++;
	} while (offset < end_offset);
	return 0;
}

