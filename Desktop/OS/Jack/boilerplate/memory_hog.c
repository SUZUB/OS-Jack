/*
 * memory_hog.c — Memory-consuming workload for kernel module testing (Phase 4)
 *
 * Gradually allocates memory in chunks to trigger soft then hard memory limits
 * enforced by the container_monitor kernel module.
 *
 * Usage: ./memory_hog [chunk_mib] [delay_ms]
 *        Default: 10 MiB chunks, 500 ms between allocations
 *
 * Expected behaviour:
 *   - Soft limit: dmesg shows WARNING from container_monitor
 *   - Hard limit: process receives SIGKILL from kernel module
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CHUNK_MIB  10
#define DEFAULT_DELAY_MS   500
#define MAX_ALLOCS         256

int main(int argc, char *argv[])
{
    int chunk_mib = DEFAULT_CHUNK_MIB;
    int delay_ms  = DEFAULT_DELAY_MS;

    if (argc > 1) chunk_mib = atoi(argv[1]);
    if (argc > 2) delay_ms  = atoi(argv[2]);

    size_t chunk_bytes = (size_t)chunk_mib * 1024 * 1024;
    void  *allocs[MAX_ALLOCS];
    int    count = 0;

    printf("memory_hog: allocating %d MiB chunks every %d ms\n",
           chunk_mib, delay_ms);
    printf("memory_hog: watch dmesg for soft/hard limit messages\n");
    fflush(stdout);

    while (count < MAX_ALLOCS) {
        void *p = malloc(chunk_bytes);
        if (!p) {
            fprintf(stderr, "memory_hog: malloc failed at allocation %d\n", count);
            break;
        }

        /* Touch every page to ensure RSS actually grows (not just virtual) */
        memset(p, 0xAB, chunk_bytes);
        allocs[count++] = p;

        long total_mib = (long)count * chunk_mib;
        printf("memory_hog: allocated %d chunks = %ld MiB total RSS\n",
               count, total_mib);
        fflush(stdout);

        usleep((unsigned int)delay_ms * 1000);
    }

    printf("memory_hog: reached max allocations (%d). Holding...\n", count);
    /* Hold memory until killed */
    while (1)
        sleep(1);

    return 0;
}
