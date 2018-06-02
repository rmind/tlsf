# TLSF: two-level segregated fit O(1) allocator

[![Build Status](https://travis-ci.org/rmind/tlsf.svg?branch=master)](https://travis-ci.org/rmind/tlsf)

TLSF: two-level segregated fit allocator which guarantees O(1) time.
This implementation provides two variations: _TLSF-INT_ which inlines the
block headers and _TLSF-EXT_ which uses externalised block header allocation.
Therefore, _TLSF-EXT_ can be used to manage arbitrary resources, e.g.
address or disk space, unique IDs within a limited range, etc.

Reference:

	M. Masmano, I. Ripoll, A. Crespo, and J. Real.
	TLSF: a new dynamic memory allocator for real-time systems.
	In Proc. ECRTS (2004), IEEE Computer Society, pp. 79-86.

The implementation is written in C99 and distributed under the
2-clause BSD license.

## API

* `tlsf_t *tlsf_create(uintptr_t baseptr, size_t size, unsigned mbs, tlsf_mode_t mode)`
  * Construct a resource allocation object to manage the space starting
  at the specified base pointer of the specified length.  The base pointer
  must be at least word aligned.  If the TLSF object allocation fails or
  the base pointer is not aligned, then `NULL` is returned.
  * A custom minimum block size (MBS) can be specified; zero can be used
  for an optimal default chosen by the allocator.  Currently, the default
  minimum allocation unit (represented by MBS) is 32.  That is, any given
  sizes will be rounded up to the minimum block size (MBS) of 32 bytes/units.
  * If _mode_ is `TLSF_INT`, then the given base pointer is treated as
  accessible memory area and the block headers will be inlined within the
  allocated blocks of memory.
  * If _mode_ is `TLSF_EXT`, then the block headers will be externalised
  and allocations can be made only through the `tlsf_ext_alloc` and
  `tlsf_ext_free` functions.  The allocator will not attempt to access the
  given space and _malloc(3)_ will be used to allocate the block headers.

* `void tlsf_destroy(tlsf_t *tlsf)`
  * Destroy the TLSF object.

* `void *tlsf_alloc(tlsf_t *tlsf, size_t size)`
  * Allocates the requested `size` bytes of memory and returns a
  pointer to it.  On failure, returns `NULL`.

* `void tlsf_free(tlsf_t *tlsf, void *ptr)`
  * Releases the previously allocated memory, given the pointer.

* `tlsf_blk_t *tlsf_ext_alloc(tlsf_t *tlsf, size_t size)`
  * Allocates the requested `size` of space and returns a reference
  (pointer to an opaque `tlsf_blk_t` type).  On failure, returns `NULL`.

* `void tlsf_ext_free(tlsf_t *tlsf, tlsf_blk_t *blk)`
  * Release the previously allocated space, given the block reference.

* `uintptr_t tlsf_ext_getaddr(const tlsf_blk_t *blk, size_t *length)`
  * Returns an offset (relative from the base address) and the length of
  the allocated space, given the block reference.

## Caveats

The TLSF-INT requires at least word-aligned base pointer; it also guarantees
the word-aligned allocations.
The allocator uses a minimum allocation unit of 32.  That is, any given
sizes will be rounded up to the minimum block size (MBS) of 32 bytes/units.

The maximum allocation size is limited to the half of the space represented
by the word size of the CPU architecture.  On 32-bit systems, it is 2^31
(~2 billion) and on 64-bit systems it is 2^63.

## Example

The following is an illustration of using TLSF as a memory allocator backed
by a memory-mapped area:
```c
#include <tlsf.h>

tlsf_t *tlsf;
void *baseptr;
struct obj *obj;

baseptr = mmap(...);
if (baseptr == MAP_FAILED)
	err(EXIT_FAILURE, "mmap");

tlsf = tlsf_create(baseptr, space_size, 0, TLSF_INT);
if (!tlsf)
	err(EXIT_FAILURE, "tlsf_create");

obj = tlsf_alloc(tlsf, sizeof(struct obj));
...
tlsf_free(tlsf, obj);
```

The following is an illustration of _TLSF-EXT_ use:
```c
tlsf_t *tlsf;
tlsf_blk_t *blk;
uintptr_t base_addr;

base_addr = get_some_address_space();

tlsf = tlsf_create(base_addr, space_size, 0, TLSF_EXT);
if (!tlsf)
	err(EXIT_FAILURE, "tlsf_create");

blk = tlsf_ext_alloc(tlsf, size);
if (blk) {
	uintptr_t off;
	size_t len;

	off = tlsf_ext_getaddr(blk, &len);
	do_something(base_addr, off, len);

	...

	tlsf_ext_free(tlsf, blk);
}
```

## Packages

Just build the package, install it and link the library using the
`-ltlsf` flag.
* RPM (tested on RHEL/CentOS 7): `cd pkg && make rpm`
* DEB (tested on Debian 9): `cd pkg && make deb`
