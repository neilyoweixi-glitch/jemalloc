/*
 * Example: Disable tcache in jemalloc
 *
 * Compile with:
 *   gcc -o disable_tcache_example disable_tcache_example.c -ljemalloc
 *
 * Or set environment variable:
 *   MALLOC_CONF="tcache:false" ./disable_tcache_example
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <jemalloc/jemalloc.h>

int main(void) {
    size_t len;
    bool tcache_enabled;
    
    printf("=== Tcache Status Check ===\n");
    
    // Check current global tcache status
    len = sizeof(bool);
    if (mallctl("opt.tcache", &tcache_enabled, &len, NULL, 0) == 0) {
        printf("Global tcache setting: %s\n", 
               tcache_enabled ? "enabled" : "disabled");
    }
    
    // Check per-thread tcache status
    len = sizeof(bool);
    if (mallctl("thread.tcache.enabled", &tcache_enabled, &len, NULL, 0) == 0) {
        printf("Current thread tcache: %s\n", 
               tcache_enabled ? "enabled" : "disabled");
    }
    
    // Method 1: Disable for current thread
    printf("\n=== Method 1: Disable tcache for current thread ===\n");
    bool disable = false;
    len = sizeof(bool);
    int ret = mallctl("thread.tcache.enabled", NULL, NULL, 
                      &disable, sizeof(bool));
    if (ret == 0) {
        printf("✓ Successfully disabled tcache for current thread\n");
    } else {
        printf("✗ Failed to disable tcache: %d\n", ret);
    }
    
    // Verify it's disabled
    len = sizeof(bool);
    mallctl("thread.tcache.enabled", &tcache_enabled, &len, NULL, 0);
    printf("Current thread tcache: %s\n", 
           tcache_enabled ? "enabled" : "disabled");
    
    // Allocate some memory (will not use tcache)
    printf("\n=== Allocating memory without tcache ===\n");
    void *ptr1 = malloc(1024);
    void *ptr2 = malloc(2048);
    
    if (ptr1 && ptr2) {
        printf("Allocated memory without tcache\n");
        
        // Free memory (will not use tcache)
        free(ptr1);
        free(ptr2);
        printf("Freed memory without tcache\n");
    }
    
    // Method 2: Use mallocx to explicitly avoid tcache
    printf("\n=== Method 2: Using mallocx with MALLOCX_TCACHE_NONE ===\n");
    void *ptr3 = mallocx(1024, MALLOCX_TCACHE_NONE);
    if (ptr3) {
        printf("Allocated with mallocx(MALLOCX_TCACHE_NONE)\n");
        dallocx(ptr3, MALLOCX_TCACHE_NONE);
        printf("Freed with dallocx(MALLOCX_TCACHE_NONE)\n");
    }
    
    // Check tcache statistics
    printf("\n=== Tcache Statistics ===\n");
    size_t tcache_bytes;
    len = sizeof(size_t);
    if (mallctl("stats.arenas.0.tcache_bytes", &tcache_bytes, &len, NULL, 0) == 0) {
        printf("Tcache bytes: %zu\n", tcache_bytes);
    }
    
    printf("\n=== Summary ===\n");
    printf("To disable tcache globally, use:\n");
    printf("  MALLOC_CONF=\"tcache:false\" ./your_program\n");
    printf("\n");
    printf("To disable tcache per-thread at runtime:\n");
    printf("  bool disable = false;\n");
    printf("  mallctl(\"thread.tcache.enabled\", NULL, NULL, &disable, sizeof(bool));\n");
    
    return 0;
}
