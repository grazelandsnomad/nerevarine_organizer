/*
 * Polyfills for glibc functions that bundled CachyOS libraries
 * (notably libglib-2.0) reference but that don't exist on
 * older glibc (Steam Deck = 2.41).
 *
 * The size/alignment hints are advisory: the underlying allocator
 * tracks them itself, so plain free()/malloc() handle the call
 * correctly.  Loaded via LD_PRELOAD from AppRun.
 */

#include <stdlib.h>

void free_aligned_sized(void *ptr, size_t alignment, size_t size)
{
    (void)alignment;
    (void)size;
    free(ptr);
}

void free_sized(void *ptr, size_t size)
{
    (void)size;
    free(ptr);
}
