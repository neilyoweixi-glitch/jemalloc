# How to Disable Tcache (Thread Cache) in jemalloc

This guide explains how to disable tcache (thread cache) in jemalloc 5.3.0.

## What is Tcache?

Tcache (thread cache) is a per-thread cache that stores recently freed memory for fast reuse. It improves allocation performance but:
- Increases memory usage per thread
- Can delay memory return to the arena
- May cause memory fragmentation

## Methods to Disable Tcache

### Method 1: Environment Variable (Recommended for Application-Wide)

Set the `MALLOC_CONF` environment variable before running your program:

```bash
export MALLOC_CONF="tcache:false"
./your_program
```

Or inline:
```bash
MALLOC_CONF="tcache:false" ./your_program
```

**Note**: This must be set **before** the program starts. It cannot be changed at runtime.

### Method 2: Global Option via mallctl (Runtime)

Disable tcache globally for all threads (must be done before any allocations):

```c
#include <jemalloc/jemalloc.h>

int main(void) {
    // Disable tcache globally
    bool tcache_enabled = false;
    size_t len = sizeof(bool);
    
    int ret = mallctl("opt.tcache", NULL, NULL, &tcache_enabled, sizeof(bool));
    if (ret != 0) {
        fprintf(stderr, "Failed to disable tcache: %d\n", ret);
        return 1;
    }
    
    // Note: This only affects NEW threads created after this call
    // Existing threads may still have tcache enabled
    
    // Your code here...
    return 0;
}
```

**Important**: `opt.tcache` is read-only at runtime. You can only read it, not set it. This method won't work for runtime disabling.

### Method 3: Per-Thread Disable (Runtime)

Disable tcache for the current thread:

```c
#include <jemalloc/jemalloc.h>

// Disable tcache for current thread
bool tcache_enabled = false;
size_t len = sizeof(bool);

int ret = mallctl("thread.tcache.enabled", NULL, NULL, 
                  &tcache_enabled, sizeof(bool));
if (ret != 0) {
    fprintf(stderr, "Failed to disable tcache: %d\n", ret);
} else {
    printf("Tcache disabled for current thread\n");
}
```

**Note**: This only affects the current thread. Each thread must disable its own tcache.

### Method 4: Flush Tcache (Clear but Don't Disable)

If you want to clear cached memory but keep tcache enabled:

```c
#include <jemalloc/jemalloc.h>

// Flush tcache for current thread (clears cached memory)
mallctl("thread.tcache.flush", NULL, NULL, NULL, 0);
```

### Method 5: Per-Allocation Disable (Using mallocx)

Disable tcache for specific allocations:

```c
#include <jemalloc/jemalloc.h>

// Allocate without using tcache
void *ptr = mallocx(size, MALLOCX_TCACHE_NONE);

// Free without using tcache
dallocx(ptr, MALLOCX_TCACHE_NONE);
```

## Complete Example

```c
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
    
    // Check current tcache status
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
    printf("\n=== Disabling tcache for current thread ===\n");
    bool disable = false;
    len = sizeof(bool);
    int ret = mallctl("thread.tcache.enabled", NULL, NULL, 
                      &disable, sizeof(bool));
    if (ret == 0) {
        printf("Successfully disabled tcache for current thread\n");
    } else {
        printf("Failed to disable tcache: %d\n", ret);
    }
    
    // Verify it's disabled
    len = sizeof(bool);
    mallctl("thread.tcache.enabled", &tcache_enabled, &len, NULL, 0);
    printf("Current thread tcache: %s\n", 
           tcache_enabled ? "enabled" : "disabled");
    
    // Allocate some memory (will not use tcache)
    void *ptr1 = malloc(1024);
    void *ptr2 = malloc(2048);
    
    if (ptr1 && ptr2) {
        printf("\nAllocated memory without tcache\n");
        
        // Free memory (will not use tcache)
        free(ptr1);
        free(ptr2);
        printf("Freed memory without tcache\n");
    }
    
    // Method 2: Use mallocx to explicitly avoid tcache
    printf("\n=== Using mallocx with MALLOCX_TCACHE_NONE ===\n");
    void *ptr3 = mallocx(1024, MALLOCX_TCACHE_NONE);
    if (ptr3) {
        printf("Allocated with mallocx(MALLOCX_TCACHE_NONE)\n");
        dallocx(ptr3, MALLOCX_TCACHE_NONE);
        printf("Freed with dallocx(MALLOCX_TCACHE_NONE)\n");
    }
    
    return 0;
}
```

## Checking Tcache Status

### Check Global Setting

```c
bool tcache_enabled;
size_t len = sizeof(bool);
mallctl("opt.tcache", &tcache_enabled, &len, NULL, 0);
printf("Global tcache: %s\n", tcache_enabled ? "enabled" : "disabled");
```

### Check Per-Thread Setting

```c
bool tcache_enabled;
size_t len = sizeof(bool);
mallctl("thread.tcache.enabled", &tcache_enabled, &len, NULL, 0);
printf("Thread tcache: %s\n", tcache_enabled ? "enabled" : "disabled");
```

### Check Tcache Statistics

```c
size_t tcache_bytes;
size_t len = sizeof(size_t);
mallctl("stats.arenas.0.tcache_bytes", &tcache_bytes, &len, NULL, 0);
printf("Tcache bytes: %zu\n", tcache_bytes);
```

## Important Notes

### 1. Environment Variable Must Be Set Early

The `MALLOC_CONF` environment variable must be set **before** the program starts. It cannot be changed at runtime.

```bash
# Correct
MALLOC_CONF="tcache:false" ./program

# Wrong - too late
./program
export MALLOC_CONF="tcache:false"  # Program already started
```

### 2. Per-Thread vs Global

- **`opt.tcache`**: Global setting (read-only at runtime, set via MALLOC_CONF)
- **`thread.tcache.enabled`**: Per-thread setting (can be changed at runtime)

### 3. Default Behavior

By default, tcache is **enabled** (`opt_tcache = true` in source code).

### 4. Thread Safety

- Disabling tcache per-thread is thread-safe
- Each thread manages its own tcache state
- New threads inherit the global `opt.tcache` setting

### 5. Performance Impact

Disabling tcache will:
- ✅ Reduce per-thread memory overhead
- ✅ Return memory to arena immediately on free
- ✅ Reduce memory fragmentation
- ❌ Increase allocation/deallocation latency
- ❌ Reduce allocation performance

## Multi-Threaded Applications

For multi-threaded applications, you have two options:

### Option 1: Disable Globally (Before Program Starts)

```bash
MALLOC_CONF="tcache:false" ./your_program
```

This disables tcache for all threads.

### Option 2: Disable Per-Thread (In Each Thread)

```c
void* thread_function(void* arg) {
    // Disable tcache for this thread
    bool disable = false;
    mallctl("thread.tcache.enabled", NULL, NULL, &disable, sizeof(bool));
    
    // Thread code here...
    return NULL;
}
```

## Verification

After disabling tcache, verify it's disabled:

```c
#include <jemalloc/jemalloc.h>
#include <stdio.h>

void verify_tcache_disabled(void) {
    bool tcache_enabled;
    size_t len = sizeof(bool);
    
    // Check global setting
    mallctl("opt.tcache", &tcache_enabled, &len, NULL, 0);
    printf("Global tcache: %s\n", tcache_enabled ? "enabled" : "disabled");
    
    // Check thread setting
    mallctl("thread.tcache.enabled", &tcache_enabled, &len, NULL, 0);
    printf("Thread tcache: %s\n", tcache_enabled ? "enabled" : "disabled");
    
    if (!tcache_enabled) {
        printf("✓ Tcache is disabled\n");
    } else {
        printf("✗ Tcache is still enabled\n");
    }
}
```

## Summary

| Method | Scope | When to Use |
|--------|-------|-------------|
| **MALLOC_CONF="tcache:false"** | Global, all threads | Application-wide disable (recommended) |
| **thread.tcache.enabled** | Per-thread | Disable for specific threads at runtime |
| **MALLOCX_TCACHE_NONE** | Per-allocation | Disable for specific allocations only |
| **thread.tcache.flush** | Per-thread | Clear cache but keep tcache enabled |

**Recommended**: Use `MALLOC_CONF="tcache:false"` for application-wide disabling.
