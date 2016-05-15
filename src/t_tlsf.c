/*
 * This file is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <err.h>

#include "tlsf.h"
#include "utils.h"

static void
random_test(const size_t spacelen, const size_t cap, bool exthdr)
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

	tlsf = tlsf_create((uintptr_t)space, spacelen, exthdr);
	assert(tlsf != NULL);
	bytesfree = tlsf_unused_space(tlsf);

	/*
	 * Allocate random sizes up to the cap threshold.
	 * Track them in an array.
	 */
	for (;;) {
		len = (random() % cap) + 1;
		p[i] = exthdr ?
		    tlsf_ext_alloc(tlsf, len) :
		    tlsf_alloc(tlsf, len);
		if (!p[i])
			break;

		/* Fill with magic (only when testing up to 1MB). */
		data = exthdr ? (void *)tlsf_ext_getaddr(p[i], NULL) : p[i];
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
		data = exthdr ?
		    (void *)tlsf_ext_getaddr(p[target], NULL) :
		    p[target];
		assert(data[0] == 0xa5);
		if (exthdr) {
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
random_sizes_test(bool exthdr)
{
	const size_t sizes[] = {
		128, 1024, 1024 * 1024, 128 * 1024 * 1024
	};

	for (unsigned i = 0; i < __arraycount(sizes); i++) {
		unsigned n = 1024;

		while (n--) {
			size_t cap = random() % sizes[i] + 1;
			random_test(sizes[i], cap, exthdr);
		}
	}
}

int
main(void)
{
	srandom(time(NULL) ^ getpid());
	random_sizes_test(false);
	random_sizes_test(true);
	puts("ok");
	return 0;
}
