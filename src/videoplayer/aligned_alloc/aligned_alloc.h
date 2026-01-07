#ifndef _ALIGNED_ALLOC_H_
#define _ALIGNED_ALLOC_H_

#include <stddef.h> 

#ifdef __cplusplus
extern "C" {
#endif

void* aligned_malloc(size_t alignment, size_t size);
void aligned_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif