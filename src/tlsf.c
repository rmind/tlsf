/*
 * Copyright (c) 2015 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * TLSF: two-level segregated fit allocator with O(1) time complexity.
 * This implementation also provides a variation, let's call it TLSF-EXT,
 * supporting the externalised block header allocation.  Therefore, it
 * can be used to manage arbitrary resources, e.g. address space.
 *
 * Reference:
 *
 *	M. Masmano, I. Ripoll, A. Crespo, and J. Real.
 *	TLSF: a new dynamic memory allocator for real-time systems.
 *	In Proc. ECRTS (2004), IEEE Computer Society, pp. 79-86.
 *
 * Notes
 *
 *	As the name of TLSF suggests, there are two levels of segregation.
 *	The first level represents the division of the space into classes
 *	which are power of 2 apart from each other.  The first-level-index
 *	is calculated using log2() function.  The second level subdivides
 *	the first level into configurable number of equal ranges (e.g. 32).
 *	However, second-level-index has to additionally take into account
 *	the minimum block size (e.g. we want to round-up allocation sizes
 *	to the 16-byte boundary).
 *
 *	Basically:
 *		FLI is in the [0 .. TLSF_FLI_COUNT] range.
 *		SLI is in the [0 .. TLSF_SLI_COUNT] range.
 *
 *	Allocation is performed by rounding up the size to a next size
 *	class and looking for a free block in that or a higher class.
 *	If the block is larger than we need - it gets split and the
 *	remaining part is reinserted as a small block.
 *
 *	The free operation is performed by first attempting to merge the
 *	returned block with the adjacent physical blocks; then the block
 *	inserted into the free list.
 */

#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <strings.h>

#include "tlsf.h"
#include "utils.h"

/*
 * The maximum number of L1 items: just use 2^64 on 64-bit architectures.
 * Otherwise, log2(4 GB) = 32.
 */
#define	TLSF_FLI_MAX		(CHAR_BIT * sizeof(unsigned long))

/*
 * The number of subdivisions (second-level-index), expressed as an
 * exponent of 2 for bitwise shifting.  2^5 = 32 subdivisions.
 */
#define	TLSF_SLI_SHIFT		5
#define	TLSF_SLI_MAX		(1UL << TLSF_SLI_SHIFT)

/*
 * Minimum block size to round up the given size (for optimisation).
 */
#define	TLSF_MBS		32

/*
 * Each memory block is tracked using a block header.  There are two
 * cases: TLSF-INT and TLSF-EXT i.e. internalised or externalised block
 * header use.
 *
 * - TLSF-INT case has block header (tlsf_blk_t) prepended at the
 *   beginning of the allocated space.  Therefore, allocation function
 *   always increments the requested size by the TLSF_BLKHDR_LEN.
 *
 * - TLSF-INT: Used (allocated) memory blocks use the whole tlsf_blk_t,
 *   while free blocks have do not use segregation list entries,
 *   therefore their effective size is reduced TLSF_BLKHDR_LEN.
 *
 * - TLSF-EXT case has a separate header (tlsf_extblk_t) allocated
 *   externally for the both cases i.e. allocated and free.  The header
 *   additionally stores the address (tlsf_blk_t::addr).
 *
 * - All block headers are linked in the order of the physical address
 *   they represent.  TLSF-INT uses 'prevblk' member and is linked only
 *   backwards (next header can be worked out based on the block length).
 *   TLSF-EXT uses doubly linked list (tlsf_extblk_t::entry).
 *
 * - Free blocks are additionally linked within their size class.
 *
 * - The length field stores the block length excluding the header.
 */

#define	TLSF_BLK_FREE		0x1UL

struct tlsf_blk {
	/*
	 * Length and:
	 * - TLSF-EXT: real address
	 * - TLSF-INT: previous block
	 */
	size_t			len;
	union {
		uintptr_t	addr;
		struct tlsf_blk *prevblk;
	};

	/* Segregation list entries. */
	struct tlsf_blk *	next;
	struct tlsf_blk *	prev;
};

#define	TLSF_BLKHDR_LEN		(offsetof(tlsf_blk_t, next))

typedef struct tlsf_extblk {
	/*
	 * The main header and the physical block chain.
	 */
	tlsf_blk_t		hdr;
	TAILQ_ENTRY(tlsf_extblk) entry;
} tlsf_extblk_t;

struct tlsf {
	/* Base pointer, size of the whole space. */
	uintptr_t		baseptr;
	size_t			size;
	size_t			free;

	/*
	 * Prepended the block header length (TLSF-INT only).
	 * If zero, TLSF-EXT is being used.
	 */
	unsigned		blk_hdr_len;
	TAILQ_HEAD(tlsf_extblk_qh, tlsf_extblk) blklist;

	unsigned long		l1_free;
	unsigned long		l2_free[TLSF_FLI_MAX];
	tlsf_blk_t *		map[TLSF_FLI_MAX][TLSF_SLI_MAX];
};

#ifndef flsl
static inline int
flsl(unsigned long x)
{
	return __predict_true(x) ?
	    (sizeof(unsigned long) * CHAR_BIT) - __builtin_clzl(x) : 0;
}
#endif
#define	ilog2(x)	(flsl(x) - 1)

/*
 * get_mapping: given the size return FLI and SLI.
 */
static inline void
get_mapping(size_t size, unsigned *fli, unsigned *sli)
{
	/*
	 * => First-level-index (FLI) = log2(size)
	 * => Second-level-index (SLI) = (size - 2^f) * (2^SLI / 2^f)
	 *
	 * The SLI can be calculated using bitwise operations:
	 * - We clear 2^FLI bit to get the subsize for SLI.
	 * - FLI itself is the maximum subsize within the FL class.
	 *
	 * Therefore:
	 *
	 *	subsize = (size ^ (1U << FLI))
	 *	SLI = (subsize * TLSF_SLI_MAX) / max_subsize
	 *	    = (subsize * TLSF_SLI_MAX) / 2^FLI
	 *	    = (subsize << TLSF_SLI_SHIFT) >> FLI
	 *	    = subsize >> (FLI - TLSF_SLI)
	 */
	*fli = ilog2(size);
	*sli = (size ^ (1UL << *fli)) >> (*fli - TLSF_SLI_SHIFT);
	ASSERT(*fli < TLSF_FLI_MAX);
	ASSERT(*sli < TLSF_SLI_MAX);
}

static inline size_t
block_length(const tlsf_blk_t *blk)
{
	return (blk->len & ~TLSF_BLK_FREE);
}

static inline bool
block_free_p(const tlsf_blk_t *blk)
{
	return (blk->len & TLSF_BLK_FREE) != 0;
}

/*
 * get_{prev,next}_physblk: given the block header, return the previous
 * or next physical block.
 */

static inline tlsf_blk_t *
get_prev_physblk(const tlsf_t *tlsf, tlsf_blk_t *blk)
{
	if (tlsf->blk_hdr_len) {
		ASSERT(tlsf->blk_hdr_len == TLSF_BLKHDR_LEN);
		ASSERT(TAILQ_EMPTY(&tlsf->blklist));
		return blk->prevblk;
	} else {
		tlsf_extblk_t *extblk = (void *)blk;
		return (void *)TAILQ_PREV(extblk, tlsf_extblk_qh, entry);
	}
}

static inline tlsf_blk_t *
get_next_physblk(const tlsf_t *tlsf, tlsf_blk_t *blk)
{
	if (tlsf->blk_hdr_len) {
		uintptr_t space_end = (uintptr_t)tlsf->baseptr + tlsf->size;
		uintptr_t nblkptr;

		ASSERT(tlsf->blk_hdr_len == TLSF_BLKHDR_LEN);
		ASSERT(TAILQ_EMPTY(&tlsf->blklist));
		nblkptr = (uintptr_t)blk + TLSF_BLKHDR_LEN + block_length(blk);
		ASSERT(nblkptr <= space_end);
		return nblkptr < space_end ? (tlsf_blk_t *)nblkptr : NULL;
	} else {
		tlsf_extblk_t *extblk = (void *)blk;
		return (void *)TAILQ_NEXT(extblk, entry);
	}
}

#ifndef NDEBUG
/*
 * validate_blkhdr: diagnostic function to validate the consistency of
 * the given block header and pointers to its physical neighbours.
 */
static bool
validate_blkhdr(const tlsf_t *tlsf, tlsf_blk_t *blk)
{
	const uintptr_t addr = tlsf->blk_hdr_len ? (uintptr_t)blk : blk->addr;
	const uintptr_t space_start = tlsf->baseptr;
	const uintptr_t space_end = tlsf->baseptr + tlsf->size;
	tlsf_blk_t *nextblk = get_next_physblk(tlsf, blk);
	tlsf_blk_t *prevblk = get_prev_physblk(tlsf, blk);
	const size_t blen = block_length(blk);

	/* The block should be at least MBS, but not more than total. */
	ASSERT(blen >= TLSF_MBS);
	ASSERT(blen <= tlsf->size);

	/* The block should be within the boundaries. */
	ASSERT(addr >= space_start);
	ASSERT(addr < space_end);

	/*
	 * The "next" (based on calculation) of the previous block should
	 * point to us and the next block should have a link to us.  Unless
	 * this is the first or the last physical block respectively.
	 */
	ASSERT(addr == space_start || get_next_physblk(tlsf, prevblk) == blk);
	ASSERT(!nextblk || get_prev_physblk(tlsf, nextblk) == blk);
	return true;
}
#endif

static inline tlsf_blk_t *
block_hdr_alloc(tlsf_t *tlsf, tlsf_blk_t *parent, size_t len)
{
	tlsf_blk_t *blk;

	if (tlsf->blk_hdr_len) {
		const size_t plen = block_length(parent);
		tlsf_blk_t *nblk;

		/*
		 * Set the previous *physical* block pointers: both for our
		 * block and the block after the newly created block.
		 */
		blk = (void *)((uint8_t *)parent + TLSF_BLKHDR_LEN + plen);
		blk->prevblk = parent;
		nblk = get_next_physblk(tlsf, blk);
		if (nblk) {
			nblk->prevblk = blk;
		}
	} else {
		tlsf_extblk_t *extblk, *pextblk = (void *)parent;

		if ((extblk = malloc(sizeof(tlsf_extblk_t))) == NULL) {
			return NULL;
		}
		blk = &extblk->hdr;
		blk->addr = parent->addr + parent->len;
		TAILQ_INSERT_AFTER(&tlsf->blklist, pextblk, extblk, entry);
	}
	blk->len = len;
	return blk;
}

static inline void
block_hdr_free(tlsf_t *tlsf, tlsf_blk_t *blk)
{
	ASSERT(!block_free_p(blk));

	if (tlsf->blk_hdr_len) {
		tlsf_blk_t *nextblk;

		if ((nextblk = get_next_physblk(tlsf, blk)) != NULL) {
			nextblk->prevblk = blk->prevblk;
			ASSERT(validate_blkhdr(tlsf, nextblk));
		}
		ASSERT(memset(blk, 0, sizeof(tlsf_blk_t)));
	} else {
		tlsf_extblk_t *extblk = (void *)blk;
		TAILQ_REMOVE(&tlsf->blklist, extblk, entry);
		ASSERT(memset(extblk, 0, sizeof(tlsf_extblk_t)));
		free(extblk);
	}
}

static void
insert_block(tlsf_t *tlsf, tlsf_blk_t *blk)
{
	unsigned fli, sli;
	tlsf_blk_t *head;

	ASSERT(validate_blkhdr(tlsf, blk));
	ASSERT(!block_free_p(blk));

	/*
	 * Get the FLI/SLI and insert the block.
	 */
	get_mapping(blk->len, &fli, &sli);
	head = tlsf->map[fli][sli];
	if (head) {
		head->prev = blk;
	}
	blk->prev = NULL;
	blk->next = head;
	tlsf->map[fli][sli] = blk;

	/* Mark the block as free. */
	tlsf->free += blk->len;
	blk->len |= TLSF_BLK_FREE;

	/* Finally, indicate that the lists have free blocks. */
	tlsf->l1_free |= (1UL << fli);
	tlsf->l2_free[fli] |= (1UL << sli);
}

static tlsf_blk_t *
remove_block(tlsf_t *tlsf, tlsf_blk_t *target, unsigned fli, unsigned sli)
{
	tlsf_blk_t *blk = target;

	/*
	 * Take a block from the map, unless explicitly specified.
	 */
	if (!target) {
		blk = tlsf->map[fli][sli];
		ASSERT(blk);
	}

	/*
	 * Unlink the block.
	 */
	if (blk->next) {
		blk->next->prev = blk->prev;
	}
	if (blk->prev) {
		blk->prev->next = blk->next;
	}
	if (tlsf->map[fli][sli] == blk) {
		tlsf->map[fli][sli] = blk->next;
	}

	/* Clear the free flag. */
	ASSERT(block_free_p(blk));
	blk->len &= ~TLSF_BLK_FREE;
	tlsf->free -= blk->len;

	/*
	 * Last block in SL?  Clear the "free" flag.  If there are SL
	 * lists with free blocks in the FL class - clear the FL too.
	 */
	if (!blk->next) {
		tlsf->l2_free[fli] &= ~(1UL << sli);
		if (tlsf->l2_free[fli] == 0) {
			tlsf->l1_free &= ~(1UL << fli);
		}
	}
	ASSERT(validate_blkhdr(tlsf, blk));
	return blk;
}

static inline tlsf_blk_t *
split_block(tlsf_t *tlsf, tlsf_blk_t *blk, size_t size)
{
	tlsf_blk_t *remblk;
	size_t remsize;

	/* Calculate the remaining size and set the new size. */
	remsize = block_length(blk) - tlsf->blk_hdr_len - size;
	ASSERT((remsize & TLSF_BLK_FREE) == 0);
	ASSERT((size & TLSF_BLK_FREE) == 0);
	blk->len = size;

	/*
	 * Allocate a new block, inheriting the remaining memory
	 * from the parent block.
	 */
	remblk = block_hdr_alloc(tlsf, blk, remsize);
	if (remblk) {
		ASSERT(!block_free_p(blk));
		ASSERT(!block_free_p(remblk));
	} else {
		blk->len = size + remsize;
	}
	return remblk;
}

/*
 * merge_blocks: merge two physically adjacent blocks - the target block
 * and a block next to it.
 */
static inline tlsf_blk_t *
merge_blocks(tlsf_t *tlsf, tlsf_blk_t *blk, tlsf_blk_t *blk2)
{
	const size_t addlen = block_length(blk2);
	unsigned fli, sli;

	ASSERT(validate_blkhdr(tlsf, blk));
	ASSERT(validate_blkhdr(tlsf, blk2));

	/* Ensure that both blocks are removed from the list. */
	if (block_free_p(blk)) {
		get_mapping(block_length(blk), &fli, &sli);
		(void)remove_block(tlsf, blk, fli, sli);
	}
	if (block_free_p(blk2)) {
		get_mapping(addlen, &fli, &sli);
		(void)remove_block(tlsf, blk2, fli, sli);
	}

	/*
	 * Add the extra space to the first block.  Finally,
	 * remove and destroy the second block.
	 */
	blk->len += tlsf->blk_hdr_len + addlen;
	block_hdr_free(tlsf, blk2);
	return blk;
}

tlsf_blk_t *
tlsf_ext_alloc(tlsf_t *tlsf, size_t size)
{
	unsigned fli, sli;
	tlsf_blk_t *blk;
	size_t target;

	/*
	 * Round up the size to TLSF_MBS and then the next size class.
	 * Get the FL/SL indexes of the size.
	 */
	size = roundup2(size, TLSF_MBS);
	target = size + (1UL << (ilog2(size) - TLSF_SLI_SHIFT)) - 1;
	get_mapping(target, &fli, &sli);

	/*
	 * Find a free block.  Fast path: look at the current FLI.
	 * Otherwise, look at next FLI starting with zero SLI.
	 */
	sli = ffsl(tlsf->l2_free[fli] & (~0UL << sli));
	if (sli == 0) {
		fli = ffsl(tlsf->l1_free & (~0UL << ++fli));
		if (__predict_false(fli == 0)) {
			return NULL;
		}
		sli = ffsl(tlsf->l2_free[--fli]);
		ASSERT(sli != 0);
	}
	sli--;

	/*
	 * Remove a block from the list.
	 */
	blk = remove_block(tlsf, NULL, fli, sli);
	ASSERT(blk != NULL);
	ASSERT(block_length(blk) >= size);

	/*
	 * If the block is larger than the threshold, then split it.
	 */
	if ((blk->len - size) >= (TLSF_MBS + tlsf->blk_hdr_len)) {
		tlsf_blk_t *remblk;

		remblk = split_block(tlsf, blk, size);
		if (remblk) {
			insert_block(tlsf, remblk);
		}
	}
	return blk;
}

void *
tlsf_alloc(tlsf_t *tlsf, size_t size)
{
	tlsf_blk_t *blk;
	void *ptr;

	ASSERT(tlsf->blk_hdr_len == TLSF_BLKHDR_LEN);
	blk = tlsf_ext_alloc(tlsf, size);
	if (blk == NULL) {
		return NULL;
	}
	ptr = (uint8_t *)blk + TLSF_BLKHDR_LEN;
	ASSERT(((uintptr_t)ptr & (sizeof(unsigned long) - 1)) == 0);
	return ptr;
}

void
tlsf_ext_free(tlsf_t *tlsf, tlsf_blk_t *blk)
{
	tlsf_blk_t *prevblk, *nextblk;

	ASSERT(!block_free_p(blk)); /* use-after-free guard */

	/* Get the adjacent blocks. */
	prevblk = get_prev_physblk(tlsf, blk);
	nextblk = get_next_physblk(tlsf, blk);

	/*
	 * Try to merge adjacent blocks.
	 */
	if (prevblk && block_free_p(prevblk)) {
		blk = merge_blocks(tlsf, prevblk, blk);
	}
	if (nextblk && block_free_p(nextblk)) {
		blk = merge_blocks(tlsf, blk, nextblk);
	}
	insert_block(tlsf, blk);
}

void
tlsf_free(tlsf_t *tlsf, void *ptr)
{
	tlsf_blk_t *blk;

	ASSERT(tlsf->blk_hdr_len == TLSF_BLKHDR_LEN);
	blk = (tlsf_blk_t *)((uint8_t *)ptr - TLSF_BLKHDR_LEN);
	tlsf_ext_free(tlsf, blk);
}

uintptr_t
tlsf_ext_getaddr(const tlsf_blk_t *blk, size_t *length)
{
	if (length) {
		*length = block_length(blk);
	}
	return blk->addr;
}

/*
 * tlsf_create: construct a resource allocation object to manage the
 * space starting at the specified base pointer of the specified length.
 *
 * => If 'exthdr' is true, then block headers will be externalised and
 *    allocations can be made only through tlsf_ext_{alloc,free} API.
 *    Note: the allocator will not attempt to access the given space.
 *
 * => If 'exthdr' is false, then the given base pointer is treated as
 *    accessible memory and the block headers will be inlined in the
 *    allocated blocks of space.
 */
tlsf_t *
tlsf_create(uintptr_t baseptr, size_t size, bool exthdr)
{
	tlsf_blk_t *blk;
	tlsf_t *tlsf;

	tlsf = calloc(1, sizeof(tlsf_t));
	if (tlsf == NULL)
		return NULL;

	/* Round down to have the size aligned. */
	size = roundup2(size + 1, TLSF_MBS) - TLSF_MBS;

	/* Initialise the TLSF object itself. */
	tlsf->baseptr = baseptr;
	tlsf->size = size;
	tlsf->free = 0;
	TAILQ_INIT(&tlsf->blklist);

	/* Initialise and insert the first block. */
	if (exthdr) {
		tlsf_extblk_t *extblk;

		extblk = calloc(1, sizeof(tlsf_extblk_t));
		if (extblk == NULL) {
			free(tlsf);
			return NULL;
		}
		blk = &extblk->hdr;
		blk->addr = baseptr;
		blk->len = size;
		TAILQ_INSERT_HEAD(&tlsf->blklist, extblk, entry);
		tlsf->blk_hdr_len = 0;
	} else {
		blk = (void *)baseptr;
		blk->len = size - TLSF_BLKHDR_LEN;
		blk->prevblk = NULL;
		tlsf->blk_hdr_len = TLSF_BLKHDR_LEN;
	}
	insert_block(tlsf, blk);

	return tlsf;
}

void
tlsf_destroy(tlsf_t *tlsf)
{
	tlsf_extblk_t *extblk;

	while ((extblk = TAILQ_FIRST(&tlsf->blklist)) != NULL) {
		TAILQ_REMOVE(&tlsf->blklist, extblk, entry);
		free(extblk);
	}
	free(tlsf);
}

/*
 * tlsf_unused_space: return the total unused space.  This is a sum of all
 * free blocks, which is not necessary allocatable, see tlsf_avail_space().
 */
size_t
tlsf_unused_space(tlsf_t *tlsf)
{
	return tlsf->free;
}

/*
 * tlsf_avail_space: return the available space i.e. the maximum free
 * block which represents a contiguous allocatable space.
 */
size_t
tlsf_avail_space(tlsf_t *tlsf)
{
	unsigned fli, sli;
	tlsf_blk_t *blk;
	size_t len;

	/*
	 * Find the last block: look at the highest free FLI and SLI
	 */
	if ((fli = flsl(tlsf->l1_free)) == 0) {
		return 0;
	}
	if ((sli = flsl(tlsf->l2_free[--fli])) == 0) {
		return 0;
	}
	blk = tlsf->map[fli][--sli];

	/*
	 * Get its length.
	 */
	ASSERT(blk);
	ASSERT(validate_blkhdr(tlsf, blk));
	len = block_length(blk);
	ASSERT(tlsf_unused_space(tlsf) >= len);

	/*
	 * Get the previous size class: we want to return the real
	 * available size on which tls_alloc() would succeed.
	 */
	len = roundup2(len + 1, TLSF_MBS) - TLSF_MBS;
	return (len + 1) - (1UL << (ilog2(len) - TLSF_SLI_SHIFT));
}
