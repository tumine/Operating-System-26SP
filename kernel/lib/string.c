#include "types.h"

void *memset(void *dst, int c, uint n) {
    uint8 *p = (uint8 *)dst;
    for (uint i = 0; i < n; i++) {
        p[i] = (uint8)c;
    }
    return dst;
}

void *memmove(void *dst, const void *src, uint n) {
    const uint8 *s = (const uint8 *)src;
    uint8 *d = (uint8 *)dst;

    if (s < d && d < s + n) {
        // Overlap: copy backwards.
        for (uint i = n; i != 0; i--) {
            d[i - 1] = s[i - 1];
        }
    } else {
        for (uint i = 0; i < n; i++) {
            d[i] = s[i];
        }
    }
    return dst;
}

