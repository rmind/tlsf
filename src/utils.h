/*
 * Copyright (c) 2017 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef	_UTILS_H_
#define	_UTILS_H_

#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <assert.h>

/*
 * A regular assert (debug/diagnostic only).
 */
#if defined(DEBUG)
#define	ASSERT		assert
#else
#define	ASSERT(x)
#endif

/*
 * Branch prediction hints.
 */

#ifndef __predict_true
#define	__predict_true(x)	__builtin_expect((x) != 0, 1)
#define	__predict_false(x)	__builtin_expect((x) != 0, 0)
#endif

#ifndef __arraycount
#define	__arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

/*
 * Rounding macro(s).
 */

#ifndef roundup2
#define	roundup2(x, m)	(((x) + (m) - 1) & ~((m) - 1))
#endif

/*
 * DSO visibility attributes (for ELF targets).
 */

#if defined(__GNUC__) && __GNUC__ >= (4)
#define	__dso_hidden	__attribute__((__visibility__("hidden")))
#else
#define	__dso_hidden
#endif

/*
 * Find first/last bit and ilog2().
 */

#ifndef ffsl
#define	ffsl(x)		__builtin_ffsl(x)
#endif

#ifndef flsl
static inline int
flsl(unsigned long x)
{
	return __predict_true(x) ?
	    (sizeof(unsigned long) * CHAR_BIT) - __builtin_clzl(x) : 0;
}
#endif

#ifndef ilog2
#define	ilog2(x)	(flsl(x) - 1)
#endif

#endif
