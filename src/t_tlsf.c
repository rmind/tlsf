/*
 * This file is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <time.h>
#include <err.h>

#include "tlsf.h"
#include "utils.h"

static void
basic_test(void)
{
	const size_t len = 32 + 32 + 32;
	uint8_t space[len + 1]; // + magic byte
	tlsf_t *tlsf;
	void *ptr;

	/* Fill the magic value. */
	space[len] = 0xa5;

	tlsf = tlsf_create((uintptr_t)&space, len, 0, TLSF_INT);
	assert(tlsf != NULL);

	ptr = tlsf_alloc(tlsf, 1);
	assert(ptr != NULL);
	assert(tlsf_unused_space(tlsf) > 0);
	assert(tlsf_avail_space(tlsf) > 0);

	ptr = tlsf_alloc(tlsf, 1);
	assert(ptr != NULL);
	assert(tlsf_unused_space(tlsf) == 0);
	assert(tlsf_avail_space(tlsf) == 0);

	ptr = tlsf_alloc(tlsf, 1);
	assert(ptr == NULL);

	tlsf_destroy(tlsf);

	/* Check the magic value. */
	assert(space[len] == 0xa5);
}

static void
random_test(const size_t spacelen, const size_t cap, tlsf_mode_t mode)
{
	const size_t maxitems = spacelen;
	size_t len, bytesfree;
	uint8_t *space, *data;
	unsigned i = 0;
	tlsf_t *tlsf;
	void **p;

	space = malloc(spacelen);
	if (space == NULL) {
		err(EXIT_FAILURE, "malloc");
	}
	p = malloc(maxitems * sizeof(void *));
	if (p == NULL) {
		err(EXIT_FAILURE, "malloc");
	}

	tlsf = tlsf_create((uintptr_t)space, spacelen, 0, mode);
	assert(tlsf != NULL);
	bytesfree = tlsf_unused_space(tlsf);

	/*
	 * Allocate random sizes up to the cap threshold.
	 * Track them in an array.
	 */
	for (;;) {
		len = (random() % cap) + 1;
		p[i] = (mode == TLSF_EXT) ?
		    tlsf_ext_alloc(tlsf, len) :
		    tlsf_alloc(tlsf, len);
		if (!p[i])
			break;

		/* Fill with magic (only when testing up to 1MB). */
		data = (mode == TLSF_EXT) ?
		    (void *)tlsf_ext_getaddr(p[i], NULL) :
		    p[i];
		if (spacelen <= 1024 * 1024) {
			memset(data, 0, len);
		}
		data[0] = 0xa5;

		if (i++ == maxitems)
			break;
	}

	/*
	 * Randomly deallocate the memory blocks until all of them are freed.
	 * The free space should match the free space after initialisation.
	 */
	for (unsigned n = i; n;) {
		unsigned target = random() % i;
		if (p[target] == NULL)
			continue;
		data = (mode == TLSF_EXT) ?
		    (void *)tlsf_ext_getaddr(p[target], NULL) :
		    p[target];
		assert(data[0] == 0xa5);
		if (mode == TLSF_EXT) {
			tlsf_ext_free(tlsf, p[target]);
		} else {
			tlsf_free(tlsf, p[target]);
		}
		p[target] = NULL;
		n--;
	}
	assert(tlsf_unused_space(tlsf) == bytesfree);

	tlsf_destroy(tlsf);
	free(space);
	free(p);
}

static void
random_sizes_test(tlsf_mode_t mode)
{
	const size_t sizes[] = {
		128, 1024, 1024 * 1024, 128 * 1024 * 1024
	};

	for (unsigned i = 0; i < __arraycount(sizes); i++) {
		unsigned n = 1024;

		while (n--) {
			size_t cap = random() % sizes[i] + 1;
			random_test(sizes[i], cap, mode);
		}
	}
}

int
main(void)
{
	srandom(time(NULL) ^ getpid());
	basic_test();
	random_sizes_test(TLSF_INT);
	random_sizes_test(TLSF_EXT);
	puts("ok");
	return 0;
}
