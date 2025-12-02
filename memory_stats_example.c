/*
 * Example: Get memory statistics from jemalloc
 *
 * Compile with:
 *   gcc -o memory_stats_example memory_stats_example.c -ljemalloc
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <jemalloc/jemalloc.h>

// Helper function to format bytes
static const char* format_bytes(size_t bytes) {
    static char buf[64];
    if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

int main(void) {
    int ret;
    size_t len;
    uint64_t epoch = 1;
    unsigned narenas;
    size_t narenas_len = sizeof(unsigned);

    // IMPORTANT: Refresh statistics before reading
    // This updates all cached statistics
    len = sizeof(uint64_t);
    ret = mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));
    if (ret != 0) {
        fprintf(stderr, "Failed to refresh stats: %d\n", ret);
        return 1;
    }

    // Get number of arenas
    ret = mallctl("arenas.narenas", &narenas, &narenas_len, NULL, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to get number of arenas: %d\n", ret);
        return 1;
    }

    printf("=== Global Memory Statistics ===\n\n");

    // Global allocated memory (currently allocated by application)
    size_t allocated;
    len = sizeof(size_t);
    ret = mallctl("stats.allocated", &allocated, &len, NULL, 0);
    if (ret == 0) {
        printf("Allocated:        %s (%zu bytes)\n", format_bytes(allocated), allocated);
    }

    // Active memory (pages currently in use)
    size_t active;
    len = sizeof(size_t);
    ret = mallctl("stats.active", &active, &len, NULL, 0);
    if (ret == 0) {
        printf("Active:            %s (%zu bytes)\n", format_bytes(active), active);
    }

    // Resident memory (physical memory currently in use)
    size_t resident;
    len = sizeof(size_t);
    ret = mallctl("stats.resident", &resident, &len, NULL, 0);
    if (ret == 0) {
        printf("Resident:          %s (%zu bytes)\n", format_bytes(resident), resident);
    }

    // Mapped memory (total virtual memory mapped)
    size_t mapped;
    len = sizeof(size_t);
    ret = mallctl("stats.mapped", &mapped, &len, NULL, 0);
    if (ret == 0) {
        printf("Mapped:            %s (%zu bytes)\n", format_bytes(mapped), mapped);
    }

    // Retained memory (virtual memory retained for reuse)
    size_t retained;
    len = sizeof(size_t);
    ret = mallctl("stats.retained", &retained, &len, NULL, 0);
    if (ret == 0) {
        printf("Retained:          %s (%zu bytes)\n", format_bytes(retained), retained);
    }

    // Metadata overhead
    size_t metadata;
    len = sizeof(size_t);
    ret = mallctl("stats.metadata", &metadata, &len, NULL, 0);
    if (ret == 0) {
        printf("Metadata:          %s (%zu bytes)\n", format_bytes(metadata), metadata);
    }

    printf("\n=== Per-Arena Statistics ===\n\n");

    // Iterate through all arenas
    for (unsigned i = 0; i < narenas; i++) {
        size_t small_allocated = 0, large_allocated = 0;
        size_t arena_mapped = 0, arena_resident = 0, arena_retained = 0;
        size_t pactive = 0, pdirty = 0, pmuzzy = 0;
        char mib_name[128];
        bool has_data = false;

        // Small allocations
        len = sizeof(size_t);
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.small.allocated", i);
        if (mallctl(mib_name, &small_allocated, &len, NULL, 0) == 0 && small_allocated > 0) {
            has_data = true;
        }

        // Large allocations
        len = sizeof(size_t);
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.large.allocated", i);
        if (mallctl(mib_name, &large_allocated, &len, NULL, 0) == 0 && large_allocated > 0) {
            has_data = true;
        }

        // Arena memory stats
        len = sizeof(size_t);
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.mapped", i);
        mallctl(mib_name, &arena_mapped, &len, NULL, 0);

        len = sizeof(size_t);
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.resident", i);
        mallctl(mib_name, &arena_resident, &len, NULL, 0);

        len = sizeof(size_t);
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.retained", i);
        mallctl(mib_name, &arena_retained, &len, NULL, 0);

        // Page stats
        len = sizeof(size_t);
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.pactive", i);
        mallctl(mib_name, &pactive, &len, NULL, 0);

        len = sizeof(size_t);
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.pdirty", i);
        mallctl(mib_name, &pdirty, &len, NULL, 0);

        len = sizeof(size_t);
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.pmuzzy", i);
        mallctl(mib_name, &pmuzzy, &len, NULL, 0);

        // Only print if arena has data
        if (has_data || arena_mapped > 0) {
            printf("Arena %u:\n", i);
            if (small_allocated > 0 || large_allocated > 0) {
                printf("  Allocated:       %s (small: %s, large: %s)\n",
                       format_bytes(small_allocated + large_allocated),
                       format_bytes(small_allocated),
                       format_bytes(large_allocated));
            }
            printf("  Mapped:          %s\n", format_bytes(arena_mapped));
            printf("  Resident:        %s\n", format_bytes(arena_resident));
            printf("  Retained:        %s\n", format_bytes(arena_retained));
            printf("  Active pages:    %zu\n", pactive);
            printf("  Dirty pages:     %zu\n", pdirty);
            printf("  Muzzy pages:     %zu\n", pmuzzy);
            printf("\n");
        }
    }

    // Demonstrate with some allocations
    printf("=== Demonstration: Allocating Memory ===\n\n");

    void *ptr1 = malloc(1024 * 1024);  // 1 MB
    void *ptr2 = malloc(512 * 1024);   // 512 KB
    void *ptr3 = malloc(256 * 1024);  // 256 KB

    if (ptr1 && ptr2 && ptr3) {
        printf("Allocated 1.75 MB of memory\n");

        // Refresh stats after allocation
        epoch = 1;
        mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));

        // Read updated stats
        len = sizeof(size_t);
        mallctl("stats.allocated", &allocated, &len, NULL, 0);
        len = sizeof(size_t);
        mallctl("stats.active", &active, &len, NULL, 0);
        len = sizeof(size_t);
        mallctl("stats.resident", &resident, &len, NULL, 0);

        printf("After allocation:\n");
        printf("  Allocated: %s\n", format_bytes(allocated));
        printf("  Active:    %s\n", format_bytes(active));
        printf("  Resident:  %s\n", format_bytes(resident));

        // Free memory
        free(ptr1);
        free(ptr2);
        free(ptr3);
        printf("\nFreed all memory\n");

        // Refresh stats after deallocation
        epoch = 1;
        mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));

        // Read updated stats
        len = sizeof(size_t);
        mallctl("stats.allocated", &allocated, &len, NULL, 0);
        len = sizeof(size_t);
        mallctl("stats.active", &active, &len, NULL, 0);

        printf("After deallocation:\n");
        printf("  Allocated: %s\n", format_bytes(allocated));
        printf("  Active:    %s\n", format_bytes(active));
        printf("  (Note: Active may still be non-zero due to caching)\n");
    }

    return 0;
}
