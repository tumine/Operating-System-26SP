#include "types.h"
#include "cpu.h"
#include "riscv.h"

extern void panic(char *s);

// push_off/pop_off implement a nesting interrupt-disable discipline.

void push_off(void) {
    int old = intr_get();
    intr_off();

    struct cpu *c = mycpu();
    if (c->noff == 0) {
        c->intena = old;
    }
    c->noff += 1;
}

void pop_off(void) {
    if (intr_get()) {
        panic("pop_off - interruptible");
    }
    struct cpu *c = mycpu();
    if (c->noff < 1) {
        panic("pop_off");
    }
    c->noff -= 1;
    if (c->noff == 0 && c->intena) {
        intr_on();
    }
}
