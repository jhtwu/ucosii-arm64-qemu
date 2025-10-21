#ifndef BSP_CACHE_H
#define BSP_CACHE_H

#include <stddef.h>

void cache_clean_range(const void *addr, size_t size);
void cache_invalidate_range(void *addr, size_t size);
void cache_clean_invalidate_range(void *addr, size_t size);

#endif /* BSP_CACHE_H */
