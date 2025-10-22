#include "fntrace_probe.h"

__attribute__((noinline)) void sr_fntrace_probe() {
    // Do a bit of work so the function doesn't get optimized away.
    volatile int v = 0;
    for (int i = 0; i < 1000; ++i) v += i;
    (void)v;
}
