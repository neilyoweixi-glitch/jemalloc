/*
 * Example: Custom Allocator for PyTorch using jemalloc Extent Hooks
 *
 * Compile with:
 *   gcc -shared -fPIC -o libpytorch_custom_allocator.so \
 *       custom_pytorch_allocator_example.c -ljemalloc
 *
 * Use in Python:
 *   import ctypes
 *   lib = ctypes.CDLL('./libpytorch_custom_allocator.so')
 *   lib.install_custom_allocator()
 *   import torch
 *   tensor = torch.randn(1000, 1000)
 */

#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// Statistics tracking
typedef struct {
    size_t total_allocations;
    size_t total_bytes_allocated;
    size_t total_bytes_freed;
    size_t current_bytes;
    size_t peak_bytes;
} allocator_stats_t;

static allocator_stats_t stats = {0};

// Custom extent allocation hook
static void *
custom_extent_alloc(extent_hooks_t *extent_hooks, void *new_addr,
    size_t size, size_t alignment, bool *zero, bool *commit,
    unsigned arena_ind) {
    
    // Use mmap for allocation
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    
    void *ptr;
    if (new_addr != NULL) {
        flags |= MAP_FIXED;
        ptr = mmap(new_addr, size, prot, flags, -1, 0);
    } else {
        ptr = mmap(NULL, size, prot, flags, -1, 0);
    }
    
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "Custom allocator: mmap failed for size %zu\n", size);
        return NULL;
    }
    
    // Zero memory if requested
    if (*zero) {
        memset(ptr, 0, size);
    }
    
    // Update statistics
    stats.total_allocations++;
    stats.total_bytes_allocated += size;
    stats.current_bytes += size;
    if (stats.current_bytes > stats.peak_bytes) {
        stats.peak_bytes = stats.current_bytes;
    }
    
    return ptr;
}

// Custom extent deallocation hook
static bool
custom_extent_dalloc(extent_hooks_t *extent_hooks, void *addr,
    size_t size, bool committed, unsigned arena_ind) {
    
    // Return false to let jemalloc manage deallocation
    // We'll handle actual freeing in destroy hook
    return false;
}

// Custom extent destroy hook
static void
custom_extent_destroy(extent_hooks_t *extent_hooks, void *addr,
    size_t size, bool committed, unsigned arena_ind) {
    
    if (munmap(addr, size) == 0) {
        stats.total_bytes_freed += size;
        if (stats.current_bytes >= size) {
            stats.current_bytes -= size;
        }
    } else {
        fprintf(stderr, "Custom allocator: munmap failed for %p\n", addr);
    }
}

// Custom extent commit hook
static bool
custom_extent_commit(extent_hooks_t *extent_hooks, void *addr,
    size_t size, size_t offset, size_t length, unsigned arena_ind) {
    
    // Memory is already committed via mmap
    return false; // Use default behavior
}

// Custom extent decommit hook
static bool
custom_extent_decommit(extent_hooks_t *extent_hooks, void *addr,
    size_t size, size_t offset, size_t length, unsigned arena_ind) {
    
    // Use madvise to decommit pages
    if (madvise((char*)addr + offset, length, MADV_DONTNEED) == 0) {
        return false; // Success
    }
    return true; // Failure
}

// Custom extent purge hooks
static bool
custom_extent_purge_lazy(extent_hooks_t *extent_hooks, void *addr,
    size_t size, size_t offset, size_t length, unsigned arena_ind) {
    
    return madvise((char*)addr + offset, length, MADV_FREE) != 0;
}

static bool
custom_extent_purge_forced(extent_hooks_t *extent_hooks, void *addr,
    size_t size, size_t offset, size_t length, unsigned arena_ind) {
    
    return madvise((char*)addr + offset, length, MADV_DONTNEED) != 0;
}

// Custom extent split hook
static bool
custom_extent_split(extent_hooks_t *extent_hooks, void *addr,
    size_t size, size_t size_a, size_t size_b, bool committed,
    unsigned arena_ind) {
    
    // Allow splitting
    return false;
}

// Custom extent merge hook
static bool
custom_extent_merge(extent_hooks_t *extent_hooks, void *addr_a,
    size_t size_a, void *addr_b, size_t size_b, bool committed,
    unsigned arena_ind) {
    
    // Check if extents are adjacent
    if ((char*)addr_a + size_a == (char*)addr_b) {
        return false; // Allow merging
    }
    return true; // Don't allow merging
}

// Initialize custom extent hooks
static extent_hooks_t custom_extent_hooks = {
    custom_extent_alloc,      // alloc
    custom_extent_dalloc,     // dalloc
    custom_extent_destroy,    // destroy
    custom_extent_commit,     // commit
    custom_extent_decommit,   // decommit
    custom_extent_purge_lazy, // purge_lazy
    custom_extent_purge_forced,// purge_forced
    custom_extent_split,      // split
    custom_extent_merge       // merge
};

// Install custom hooks for arena 0
void install_custom_allocator(void) {
    size_t hooks_size = sizeof(extent_hooks_t);
    extent_hooks_t *old_hooks;
    size_t old_size = sizeof(extent_hooks_t*);
    
    int ret = mallctl("arena.0.extent_hooks", &old_hooks, &old_size,
                      &custom_extent_hooks, hooks_size);
    
    if (ret == 0) {
        printf("Custom allocator hooks installed successfully\n");
    } else {
        fprintf(stderr, "Failed to install custom hooks: %d\n", ret);
    }
}

// Print allocator statistics
void print_allocator_stats(void) {
    printf("=== Custom Allocator Statistics ===\n");
    printf("Total allocations:      %zu\n", stats.total_allocations);
    printf("Total bytes allocated:  %zu (%.2f MB)\n", 
           stats.total_bytes_allocated,
           stats.total_bytes_allocated / (1024.0 * 1024.0));
    printf("Total bytes freed:      %zu (%.2f MB)\n",
           stats.total_bytes_freed,
           stats.total_bytes_freed / (1024.0 * 1024.0));
    printf("Current bytes:           %zu (%.2f MB)\n",
           stats.current_bytes,
           stats.current_bytes / (1024.0 * 1024.0));
    printf("Peak bytes:              %zu (%.2f MB)\n",
           stats.peak_bytes,
           stats.peak_bytes / (1024.0 * 1024.0));
}

// Get current allocated bytes (for Python)
size_t get_current_bytes(void) {
    return stats.current_bytes;
}

// Reset statistics
void reset_stats(void) {
    memset(&stats, 0, sizeof(stats));
}
