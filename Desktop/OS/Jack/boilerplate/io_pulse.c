/*
 * io_pulse.c — I/O-bound workload for scheduler experiments (Phase 5)
 *
 * Alternates between short bursts of work and I/O waits.
 * Demonstrates Linux CFS interactivity bonus for I/O-bound tasks.
 *
 * Usage: ./io_pulse [cycles]
 *        Default: 500 cycles of work + sleep
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define DEFAULT_CYCLES  500
#define WORK_ITERS      500000
#define SLEEP_US        10000   /* 10 ms I/O sleep */

int main(int argc, char *argv[])
{
    int cycles = DEFAULT_CYCLES;

    if (argc > 1)
        cycles = atoi(argv[1]);

    printf("io_pulse: starting %d cycles (work + %d us sleep)\n",
           cycles, SLEEP_US);
    fflush(stdout);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int c = 0; c < cycles; c++) {
        /* CPU burst */
        volatile long x = 0;
        for (int i = 0; i < WORK_ITERS; i++)
            x += i;

        /* Simulate I/O wait */
        usleep(SLEEP_US);

        if (c % 100 == 0) {
            printf("io_pulse: cycle %d/%d\n", c, cycles);
            fflush(stdout);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("io_pulse: done. elapsed=%.3f s\n", elapsed);
    return 0;
}
