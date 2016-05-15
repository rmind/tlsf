# TLSF: two-level segregated fit O(1) allocator

TLSF: two-level segregated fit allocator which guarantees O(1) time.
This implementation also provides a variation, let's call it _TLSF-EXT_,
supporting the externalised block header allocation.  Therefore, it can
be used to manage arbitrary resources, e.g. address or disk space.

Reference:

	M. Masmano, I. Ripoll, A. Crespo, and J. Real.
	TLSF: a new dynamic memory allocator for real-time systems.
	In Proc. ECRTS (2004), IEEE Computer Society, pp. 79-86.

The implementation is written in C11 and distributed under the
2-clause BSD license.

## API

* `tlsf_t *tlsf_create(uintptr_t baseptr, size_t size, bool exthdr)`
  * Construct a resource allocation object to manage the space starting
  at the specified base pointer of the specified length.

  If `exthdr` is true, then block headers will be externalised and
  allocations can be made only through `tlsf_ext_alloc` and `tlsf_ext_free`.
  Note: the allocator will not attempt to access the given space;
  _malloc(3)_ will be used to allocate the block headers.

  If `exthdr` is false, then the given base pointer is treated as
  accessible memory and the block headers will be inlined in the
  allocated blocks of space.

* `void tlsf_destroy(tlsf_t *tlsf)`
  * Destroy the TLSF object.

* `void *tlsf_alloc(tlsf_t *tlsf, size_t size)`
  * Allocates the requested `size` bytes of memory and returns a
  pointer to it.  On failure, returns `NULL`.

* `void tlsf_free(tlsf_t *tlsf, void *ptr)`
  * Releases the previously allocated memory, given the pointer.
