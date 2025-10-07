#ifndef LIB_H
#define LIB_H

#include <stddef.h>
#include <stdint.h>

void *util_memcpy(void *dest, const void *src, size_t n);
void *util_memset(void *dest, int value, size_t n);
int util_memcmp(const void *lhs, const void *rhs, size_t n);

uint16_t util_htons(uint16_t value);
uint32_t util_htonl(uint32_t value);
uint16_t util_ntohs(uint16_t value);
uint32_t util_ntohl(uint32_t value);

#endif /* LIB_H */
