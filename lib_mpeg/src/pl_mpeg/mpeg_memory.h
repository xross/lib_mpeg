#ifndef LIB_MPEG_MEMORY_H_
#define LIB_MPEG_MEMORY_H_
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void mpeg_memory_reset(void);
void *mpeg_memory_alloc(size_t size);
void *mpeg_memory_resize(void *ptr, size_t size);
void mpeg_memory_free(void *ptr);
#ifdef __cplusplus
}
#endif
#endif
