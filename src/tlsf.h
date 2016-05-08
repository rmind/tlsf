/*
 * Copyright (c) 2015 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _TLSF_H_
#define _TLSF_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

__BEGIN_DECLS

struct tlsf;
typedef struct tlsf tlsf_t;

struct tlsf_blk;
typedef struct tlsf_blk tlsf_blk_t;

tlsf_t *	tlsf_create(uintptr_t, size_t, bool);
void		tlsf_destroy(tlsf_t *);

size_t		tlsf_avail_space(tlsf_t *);
size_t		tlsf_unused_space(tlsf_t *);

void *		tlsf_alloc(tlsf_t *, size_t);
void		tlsf_free(tlsf_t *, void *);

tlsf_blk_t *	tlsf_ext_alloc(tlsf_t *, size_t);
void		tlsf_ext_free(tlsf_t *, tlsf_blk_t *);
uintptr_t	tlsf_ext_getaddr(const tlsf_blk_t *, size_t *);

__END_DECLS

#endif
