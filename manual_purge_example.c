/*
 * Example: Disable dirty_decay and muzzy_decay, and manually trigger
 * a global purge operation to call MADV_DONTNEED for all unused pages.
 *
 * Compile with:
 *   gcc -o manual_purge_example manual_purge_example.c -ljemalloc
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <jemalloc/jemalloc.h>

int main(void) {
    int ret;
    ssize_t old_dirty_decay_ms, old_muzzy_decay_ms;
    size_t old_len;
    unsigned narenas;
    size_t narenas_len = sizeof(unsigned);

    // Optional: Disable background threads for full manual control
    // Note: This should ideally be done before any allocations
    bool bg_thread_enable = false;
    size_t bg_thread_len = sizeof(bool);
    bool bg_thread_old;
    ret = mallctl("background_thread", &bg_thread_old, &bg_thread_len,
                  &bg_thread_enable, sizeof(bool));
    if (ret == 0) {
        printf("Background threads: %s (was: %s)\n",
               bg_thread_enable ? "enabled" : "disabled",
               bg_thread_old ? "enabled" : "disabled");
    }

    // Get the number of arenas
    ret = mallctl("arenas.narenas", &narenas, &narenas_len, NULL, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to get number of arenas: %d\n", ret);
        return 1;
    }
    printf("Number of arenas: %u\n", narenas);

    // Method 1: Disable decay for all arenas using MALLCTL_ARENAS_ALL
    // This sets dirty_decay_ms and muzzy_decay_ms to -1 for all arenas
    printf("\n=== Disabling decay for all arenas ===\n");
    
    ssize_t disable_decay = -1;
    old_len = sizeof(ssize_t);
    
    // Disable dirty decay for all arenas
    ret = mallctl("arena.4096.dirty_decay_ms", &old_dirty_decay_ms, &old_len,
                  &disable_decay, sizeof(ssize_t));
    if (ret != 0) {
        fprintf(stderr, "Failed to disable dirty_decay_ms: %d\n", ret);
        return 1;
    }
    printf("Disabled dirty_decay_ms (old value: %zd)\n", old_dirty_decay_ms);
    
    // Disable muzzy decay for all arenas
    old_len = sizeof(ssize_t);
    ret = mallctl("arena.4096.muzzy_decay_ms", &old_muzzy_decay_ms, &old_len,
                  &disable_decay, sizeof(ssize_t));
    if (ret != 0) {
        fprintf(stderr, "Failed to disable muzzy_decay_ms: %d\n", ret);
        return 1;
    }
    printf("Disabled muzzy_decay_ms (old value: %zd)\n", old_muzzy_decay_ms);

    // Method 2: Alternatively, disable decay for individual arenas
    // Uncomment the following to disable decay for specific arenas:
    /*
    for (unsigned i = 0; i < narenas; i++) {
        char mib_name[64];
        
        snprintf(mib_name, sizeof(mib_name), "arena.%u.dirty_decay_ms", i);
        ret = mallctl(mib_name, NULL, NULL, &disable_decay, sizeof(ssize_t));
        if (ret == 0) {
            printf("Disabled dirty_decay_ms for arena %u\n", i);
        }
        
        snprintf(mib_name, sizeof(mib_name), "arena.%u.muzzy_decay_ms", i);
        ret = mallctl(mib_name, NULL, NULL, &disable_decay, sizeof(ssize_t));
        if (ret == 0) {
            printf("Disabled muzzy_decay_ms for arena %u\n", i);
        }
    }
    */

    // Allocate some memory to create dirty/muzzy pages
    printf("\n=== Allocating memory to create unused pages ===\n");
    void *ptr1 = malloc(1024 * 1024);  // 1 MB
    void *ptr2 = malloc(1024 * 1024);   // 1 MB
    void *ptr3 = malloc(1024 * 1024);   // 1 MB
    
    if (ptr1 && ptr2 && ptr3) {
        printf("Allocated 3 MB of memory\n");
        
        // Write to memory to make it dirty
        memset(ptr1, 0xAA, 1024 * 1024);
        memset(ptr2, 0xBB, 1024 * 1024);
        memset(ptr3, 0xCC, 1024 * 1024);
        printf("Wrote to memory (making pages dirty)\n");
        
        // Free the memory - pages become dirty/muzzy
        free(ptr1);
        free(ptr2);
        free(ptr3);
        printf("Freed memory (pages are now dirty/muzzy)\n");
    }

    // Method 3: Manually trigger a global purge operation
    // This will call MADV_DONTNEED for all unused pages in all arenas
    printf("\n=== Triggering manual global purge ===\n");
    
    // Purge all arenas using MALLCTL_ARENAS_ALL (4096)
    ret = mallctl("arena.4096.purge", NULL, NULL, NULL, 0);
    if (ret != 0) {
        fprintf(stderr, "Failed to purge all arenas: %d\n", ret);
        return 1;
    }
    printf("Successfully triggered purge for all arenas\n");
    printf("MADV_DONTNEED has been called for all unused pages\n");

    // Optional: Check purge statistics
    printf("\n=== Purge Statistics ===\n");
    for (unsigned i = 0; i < narenas; i++) {
        uint64_t dirty_npurge, dirty_purged, muzzy_npurge, muzzy_purged;
        size_t len = sizeof(uint64_t);
        char mib_name[64];
        
        snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.dirty_npurge", i);
        if (mallctl(mib_name, &dirty_npurge, &len, NULL, 0) == 0) {
            snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.dirty_purged", i);
            if (mallctl(mib_name, &dirty_purged, &len, NULL, 0) == 0) {
                snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.muzzy_npurge", i);
                if (mallctl(mib_name, &muzzy_npurge, &len, NULL, 0) == 0) {
                    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.muzzy_purged", i);
                    if (mallctl(mib_name, &muzzy_purged, &len, NULL, 0) == 0) {
                        if (dirty_npurge > 0 || muzzy_npurge > 0) {
                            printf("Arena %u: dirty_npurge=%lu dirty_purged=%lu "
                                   "muzzy_npurge=%lu muzzy_purged=%lu\n",
                                   i, dirty_npurge, dirty_purged, muzzy_npurge, muzzy_purged);
                        }
                    }
                }
            }
        }
    }

    printf("\n=== Summary ===\n");
    printf("1. Decay has been disabled (dirty_decay_ms=-1, muzzy_decay_ms=-1)\n");
    printf("2. Manual purge has been triggered for all arenas\n");
    printf("3. MADV_DONTNEED has been called for all unused pages\n");
    printf("\nNote: To re-enable decay, set decay_ms values to positive numbers\n");
    printf("      (e.g., 10000 for 10 seconds)\n");

    return 0;
}
