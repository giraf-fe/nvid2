/*
  Cross-platform aligned allocation implemented purely on top of malloc/free.

  - aligned_malloc(alignment, size) returns a pointer aligned to `alignment`.
  - aligned_free(ptr) must be used to free pointers returned by aligned_malloc.

  Requirements enforced:
  - alignment must be a power of two
  - alignment must be >= sizeof(void*)
*/

#include <stddef.h>   // size_t
#include <stdint.h>   // uintptr_t
#include <stdlib.h>   // malloc, free

static int is_power_of_two_size_t(size_t x) {
    return x && ((x & (x - 1)) == 0);
}

void* aligned_malloc(size_t alignment, size_t size) {
    if (size == 0) {
        // Match common malloc behavior: may return NULL or a unique pointer.
        // We'll return NULL for simplicity and predictability.
        return NULL;
    }

    if (!is_power_of_two_size_t(alignment) || alignment < sizeof(void*)) {
        return NULL;
    }

    // We allocate:
    //   [raw malloc block.........................]
    //    ^ raw
    //    + sizeof(void*) bytes reserved to store `raw`
    //    + (alignment - 1) to ensure we can align up
    //
    // total = size + (alignment - 1) + sizeof(void*)
    size_t extra = (alignment - 1) + sizeof(void*);
    if (size > (SIZE_MAX - extra)) {
        return NULL; // overflow
    }

    void* raw = malloc(size + extra);
    if (!raw) {
        return NULL;
    }

    uintptr_t start = (uintptr_t)raw + sizeof(void*);
    uintptr_t aligned = (start + (alignment - 1)) & ~(uintptr_t)(alignment - 1);

    // Store the original allocation pointer immediately before the aligned pointer.
    ((void**)aligned)[-1] = raw;

    return (void*)aligned;
}

void aligned_free(void* ptr) {
    if (!ptr) return;

    // Retrieve the original pointer and free it.
    void* raw = ((void**)ptr)[-1];
    free(raw);
}
