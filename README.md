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
