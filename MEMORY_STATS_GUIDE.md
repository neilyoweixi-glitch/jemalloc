# Guide: Getting Memory Statistics from jemalloc

This guide explains how to retrieve memory statistics from jemalloc 5.3.0 using the `mallctl` interface.

## Overview

jemalloc provides comprehensive memory statistics through the `mallctl` interface. These statistics are cached and need to be refreshed before reading to get current values.

## Important: Refreshing Statistics

**Always refresh statistics before reading them** by calling:

```c
uint64_t epoch = 1;
size_t len = sizeof(uint64_t);
mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));
```

This updates all cached statistics. Without this, you may read stale values.

## Global Memory Statistics

### Available Statistics

| Statistic | Description | Mallctl Name |
|-----------|-------------|--------------|
| **Allocated** | Total memory currently allocated by application | `stats.allocated` |
| **Active** | Pages currently in use (may be larger than allocated due to internal fragmentation) | `stats.active` |
| **Resident** | Physical memory currently in use | `stats.resident` |
| **Mapped** | Total virtual memory mapped from OS | `stats.mapped` |
| **Retained** | Virtual memory retained for reuse (not returned to OS) | `stats.retained` |
| **Metadata** | Memory overhead for jemalloc metadata | `stats.metadata` |

### Example: Reading Global Stats

```c
#include <jemalloc/jemalloc.h>

// Refresh stats
uint64_t epoch = 1;
mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));

// Read statistics
size_t allocated, active, resident, mapped, retained, metadata;
size_t len = sizeof(size_t);

mallctl("stats.allocated", &allocated, &len, NULL, 0);
mallctl("stats.active", &active, &len, NULL, 0);
mallctl("stats.resident", &resident, &len, NULL, 0);
mallctl("stats.mapped", &mapped, &len, NULL, 0);
mallctl("stats.retained", &retained, &len, NULL, 0);
mallctl("stats.metadata", &metadata, &len, NULL, 0);

printf("Allocated: %zu bytes\n", allocated);
printf("Active:    %zu bytes\n", active);
printf("Resident:  %zu bytes\n", resident);
printf("Mapped:    %zu bytes\n", mapped);
printf("Retained:  %zu bytes\n", retained);
printf("Metadata:  %zu bytes\n", metadata);
```

## Per-Arena Statistics

Each arena has its own statistics. Use `stats.arenas.<i>.*` to access per-arena stats.

### Getting Number of Arenas

```c
unsigned narenas;
size_t len = sizeof(unsigned);
mallctl("arenas.narenas", &narenas, &len, NULL, 0);
```

### Per-Arena Statistics

| Statistic | Description | Mallctl Name |
|-----------|-------------|--------------|
| **Small Allocated** | Small allocations in this arena | `stats.arenas.<i>.small.allocated` |
| **Large Allocated** | Large allocations in this arena | `stats.arenas.<i>.large.allocated` |
| **Mapped** | Virtual memory mapped for this arena | `stats.arenas.<i>.mapped` |
| **Resident** | Physical memory used by this arena | `stats.arenas.<i>.resident` |
| **Retained** | Virtual memory retained by this arena | `stats.arenas.<i>.retained` |
| **Active Pages** | Number of active pages | `stats.arenas.<i>.pactive` |
| **Dirty Pages** | Number of dirty pages | `stats.arenas.<i>.pdirty` |
| **Muzzy Pages** | Number of muzzy pages | `stats.arenas.<i>.pmuzzy` |

### Example: Reading Per-Arena Stats

```c
unsigned narenas;
size_t len = sizeof(unsigned);
mallctl("arenas.narenas", &narenas, &len, NULL, 0);

// Refresh stats
uint64_t epoch = 1;
mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));

for (unsigned i = 0; i < narenas; i++) {
    size_t small_allocated, large_allocated, mapped, resident, retained;
    size_t pactive, pdirty, pmuzzy;
    char mib_name[128];
    
    len = sizeof(size_t);
    
    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.small.allocated", i);
    mallctl(mib_name, &small_allocated, &len, NULL, 0);
    
    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.large.allocated", i);
    mallctl(mib_name, &large_allocated, &len, NULL, 0);
    
    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.mapped", i);
    mallctl(mib_name, &mapped, &len, NULL, 0);
    
    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.resident", i);
    mallctl(mib_name, &resident, &len, NULL, 0);
    
    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.retained", i);
    mallctl(mib_name, &retained, &len, NULL, 0);
    
    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.pactive", i);
    mallctl(mib_name, &pactive, &len, NULL, 0);
    
    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.pdirty", i);
    mallctl(mib_name, &pdirty, &len, NULL, 0);
    
    snprintf(mib_name, sizeof(mib_name), "stats.arenas.%u.pmuzzy", i);
    mallctl(mib_name, &pmuzzy, &len, NULL, 0);
    
    printf("Arena %u:\n", i);
    printf("  Allocated: %zu bytes (small: %zu, large: %zu)\n",
           small_allocated + large_allocated, small_allocated, large_allocated);
    printf("  Mapped: %zu bytes\n", mapped);
    printf("  Resident: %zu bytes\n", resident);
    printf("  Retained: %zu bytes\n", retained);
    printf("  Pages: active=%zu dirty=%zu muzzy=%zu\n",
           pactive, pdirty, pmuzzy);
}
```

## Using MALLCTL_ARENAS_ALL

To get aggregated statistics for all arenas, use `MALLCTL_ARENAS_ALL` (4096):

```c
size_t total_allocated;
size_t len = sizeof(size_t);

// Get total allocated across all arenas
mallctl("stats.arenas.4096.small.allocated", &total_allocated, &len, NULL, 0);
size_t large_allocated;
mallctl("stats.arenas.4096.large.allocated", &large_allocated, &len, NULL, 0);

printf("Total allocated: %zu bytes\n", total_allocated + large_allocated);
```

## Understanding the Statistics

### Allocated vs Active

- **Allocated**: Memory actually allocated by your application (via `malloc`, `calloc`, etc.)
- **Active**: Pages currently in use by jemalloc (includes internal fragmentation)
  - Active ≥ Allocated (due to page alignment and internal structures)
  - Active is typically slightly larger than allocated

### Resident vs Mapped

- **Resident**: Physical RAM currently in use
- **Mapped**: Virtual address space mapped from OS
  - Mapped ≥ Resident (some mapped pages may be swapped out)
  - Mapped includes both active and retained memory

### Retained Memory

- Memory that jemalloc has kept for potential reuse
- Not returned to OS immediately after deallocation
- Can be purged manually or via decay settings

### Page States

- **Active**: Pages currently in use
- **Dirty**: Pages that were used but are now free (not yet purged)
- **Muzzy**: Pages that have been lazily purged but not yet returned to OS

## Complete Example

See `memory_stats_example.c` for a complete working example that:
1. Refreshes statistics
2. Reads global memory statistics
3. Reads per-arena statistics
4. Demonstrates stats before/after allocation/deallocation

## Thread-Local Statistics

You can also get statistics for the current thread:

```c
uint64_t thread_allocated, thread_deallocated;
size_t len = sizeof(uint64_t);

mallctl("thread.allocated", &thread_allocated, &len, NULL, 0);
mallctl("thread.deallocated", &thread_deallocated, &len, NULL, 0);

printf("Thread allocated: %lu bytes\n", thread_allocated);
printf("Thread deallocated: %lu bytes\n", thread_deallocated);
printf("Thread net: %lu bytes\n", thread_allocated - thread_deallocated);
```

## Performance Considerations

1. **Statistics are cached**: Always call `epoch` before reading stats
2. **Reading stats has overhead**: Don't call too frequently in hot paths
3. **Per-arena iteration**: Iterating through all arenas can be expensive if you have many arenas

## Compilation

Compile your program with jemalloc:

```bash
gcc -o your_program your_program.c -ljemalloc
```

## Quick Reference: Common Statistics

### Most Important Stats for "Currently Used Memory"

```c
// Refresh stats first!
uint64_t epoch = 1;
mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));

// Get currently allocated memory (what your app is using)
size_t allocated;
size_t len = sizeof(size_t);
mallctl("stats.allocated", &allocated, &len, NULL, 0);

// Get physical memory in use
size_t resident;
len = sizeof(size_t);
mallctl("stats.resident", &resident, &len, NULL, 0);

// Get virtual memory mapped
size_t mapped;
len = sizeof(size_t);
mallctl("stats.mapped", &mapped, &len, NULL, 0);
```

### Metadata Breakdown

```c
size_t metadata, metadata_edata, metadata_rtree;
size_t len = sizeof(size_t);

mallctl("stats.metadata", &metadata, &len, NULL, 0);
mallctl("stats.metadata_edata", &metadata_edata, &len, NULL, 0);
mallctl("stats.metadata_rtree", &metadata_rtree, &len, NULL, 0);
```

### Allocation Counts

```c
uint64_t small_nmalloc, small_ndalloc;
uint64_t large_nmalloc, large_ndalloc;
size_t len = sizeof(uint64_t);

mallctl("stats.arenas.0.small.nmalloc", &small_nmalloc, &len, NULL, 0);
mallctl("stats.arenas.0.small.ndalloc", &small_ndalloc, &len, NULL, 0);
mallctl("stats.arenas.0.large.nmalloc", &large_nmalloc, &len, NULL, 0);
mallctl("stats.arenas.0.large.ndalloc", &large_ndalloc, &len, NULL, 0);
```

## See Also

- jemalloc documentation: `doc/jemalloc.xml.in` (search for "stats")
- jemalloc source: `src/ctl.c` (statistics implementation)
- `memory_stats_example.c` - Complete working example
