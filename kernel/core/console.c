#include "types.h"
#include "spinlock.h"
#include "defs.h"

#define BACKSPACE 0x100
#define INPUT_BUF 128

#define C(x) ((x) - '@')

static struct {
    struct spinlock lock;
    int locking;

    char buf[INPUT_BUF];
    uint r; // Read index
    uint w; // Write index (committed)
    uint e; // Edit index
} cons;

void consoleinit(void) {
    initlock(&cons.lock, "console");
    cons.locking = 1;
    cons.r = cons.w = cons.e = 0;
}

void consolepanic(void) {
    // Caller is panicking; avoid deadlock on the console lock.
    cons.locking = 0;
}

void consoleacquire(void) {
    if (cons.locking) {
        acquire(&cons.lock);
    }
}

void consolerelease(void) {
    if (cons.locking) {
        release(&cons.lock);
    }
}

// Output a character to the UART. Caller should hold the console lock
// (or have disabled console locking via consolepanic()).
void consputc(int c) {
    if (c == BACKSPACE) {
        uart_putc('\b');
        uart_putc(' ');
        uart_putc('\b');
    } else {
        uart_putc((char)c);
    }
}

// Called from UART interrupt handler with a single input byte.
void consoleintr(int c) {
    consoleacquire();

    switch (c) {
    case C('U'):
        // Kill line.
        while (cons.e != cons.w && cons.buf[(cons.e - 1) % INPUT_BUF] != '\n') {
            cons.e--;
            consputc(BACKSPACE);
        }
        break;
    case C('H'):
    case 0x7f:
        // Backspace.
        if (cons.e != cons.w) {
            cons.e--;
            consputc(BACKSPACE);
        }
        break;
    default:
        if (c != 0 && (cons.e - cons.r) < INPUT_BUF) {
            if (c == '\r') {
                c = '\n';
            }

            cons.buf[cons.e % INPUT_BUF] = (char)c;
            cons.e++;
            consputc(c);

            // Make the line available to readers.
            if (c == '\n' || c == C('D') || cons.e == cons.r + INPUT_BUF) {
                cons.w = cons.e;
                wakeup(&cons.r);
            }
        }
        break;
    }

    consolerelease();
}

// Return the next buffered input character, or -1 if none.
int consolegetc(void) {
    int c = -1;

    consoleacquire();
    if (cons.r != cons.w) {
        c = cons.buf[cons.r % INPUT_BUF];
        cons.r++;
    }
    consolerelease();

    return c;
}

// Read from the console input buffer into user memory.
// Returns number of bytes read, 0 on EOF (^D), or -1 on error.
int consoleread(pagetable_t pagetable, uint64 dstva, int n) {
    int i = 0;

    for (i = 0; i < n; i++) {
        int c;

        consoleacquire();
        while (cons.r == cons.w) {
            // Wait for input to become available.
            sleep(&cons.r, &cons.lock);
        }
        c = cons.buf[cons.r % INPUT_BUF];
        cons.r++;
        consolerelease();

        if (c == C('D')) {
            // EOF.
            if (i == 0) {
                return 0;
            }
            return i;
        }

        char ch = (char)c;
        if (copyout(pagetable, dstva + (uint64)i, &ch, 1) < 0) {
            return -1;
        }
        if (ch == '\n') {
            return i + 1;
        }
    }

    return i;
}
