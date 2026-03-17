#include "user.h"

// Force a RW segment and multi-page mapping (bss).
char big[8192];

int main(void) {
    const char msg[] = "[user] hello from U-mode!\n";
    printf("%s", msg);

    // Basic heap growth test.
    char *p = (char *)sbrk(4096);
    if (p == (char *)-1) {
        const char fail[] = "[user] sbrk failed\n";
        printf("%s", fail);
        exit(1);
    }
    const char ok[] = "[user] sbrk ok\n";
    memcpy(p, ok, sizeof(ok));
    printf("%s", p);

    // Touch .bss to ensure RW pages are mapped and writable.
    big[0] = 'O';
    big[8191] = 'K';

    exit(0);
}
