#include "lib.h"

void *util_memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    for (size_t i = 0u; i < n; ++i) {
        d[i] = s[i];
    }

    return dest;
}

void *util_memset(void *dest, int value, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    uint8_t v = (uint8_t)(value & 0xFF);

    for (size_t i = 0u; i < n; ++i) {
        d[i] = v;
    }

    return dest;
}

int util_memcmp(const void *lhs, const void *rhs, size_t n)
{
    const uint8_t *a = (const uint8_t *)lhs;
    const uint8_t *b = (const uint8_t *)rhs;

    for (size_t i = 0u; i < n; ++i) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }

    return 0;
}

uint16_t util_htons(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

uint32_t util_htonl(uint32_t value)
{
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

uint16_t util_ntohs(uint16_t value)
{
    return util_htons(value);
}

uint32_t util_ntohl(uint32_t value)
{
    return util_htonl(value);
}
