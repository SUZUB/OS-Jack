/*
 * cpu_hog.c — CPU-bound workload for scheduler experiments (Phase 5)
 *
 * Runs a tight computation loop for a fixed number of iterations.
 * Use with nice(0) vs nice(19) to observe CFS scheduling differences.
 *
 * Usage: ./cpu_hog [iterations]
 *        Default: 1,000,000,000 iterations
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_ITERATIONS 1000000000ULL

int main(int argc, char *argv[])
{
    unsigned long long iters = DEFAULT_ITERATIONS;

    if (argc > 1)
        iters = strtoull(argv[1], NULL, 10);

    printf("cpu_hog: starting %llu iterations\n", iters);
    fflush(stdout);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Tight CPU loop — prevents compiler from optimising away */
    volatile unsigned long long x = 0;
    for (unsigned long long i = 0; i < iters; i++)
        x += i;

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("cpu_hog: done. result=%llu  elapsed=%.3f s\n", x, elapsed);
    return 0;
}
