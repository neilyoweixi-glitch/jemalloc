# Guide: Disable Decay and Manually Trigger Global Purge

This guide explains how to disable `dirty_decay` and `muzzy_decay` functionalities and manually trigger a global purge operation that calls `MADV_DONTNEED` for all unused pages in jemalloc 5.3.0.

## Overview

By default, jemalloc uses a decay-based purging system that automatically purges unused pages over time. To disable this automatic behavior and manually control when purging occurs, you need to:

1. **Disable decay** by setting `dirty_decay_ms` and `muzzy_decay_ms` to `-1`
2. **Manually trigger purges** using the `arena.<i>.purge` mallctl interface

## Method 1: Disable Decay for All Arenas (Recommended)

Use `MALLCTL_ARENAS_ALL` (4096) to disable decay for all arenas at once:

```c
#include <jemalloc/jemalloc.h>

ssize_t disable_decay = -1;
size_t old_len = sizeof(ssize_t);
ssize_t old_value;

// Disable dirty decay for all arenas
mallctl("arena.4096.dirty_decay_ms", &old_value, &old_len,
        &disable_decay, sizeof(ssize_t));

// Disable muzzy decay for all arenas
old_len = sizeof(ssize_t);
mallctl("arena.4096.muzzy_decay_ms", &old_value, &old_len,
        &disable_decay, sizeof(ssize_t));
```

## Method 2: Disable Decay for Individual Arenas

If you need more fine-grained control, disable decay for specific arenas:

```c
unsigned arena_index = 0;  // or any specific arena index
ssize_t disable_decay = -1;
char mib_name[64];

snprintf(mib_name, sizeof(mib_name), "arena.%u.dirty_decay_ms", arena_index);
mallctl(mib_name, NULL, NULL, &disable_decay, sizeof(ssize_t));

snprintf(mib_name, sizeof(mib_name), "arena.%u.muzzy_decay_ms", arena_index);
mallctl(mib_name, NULL, NULL, &disable_decay, sizeof(ssize_t));
```

## Method 3: Manually Trigger Global Purge

After disabling decay, trigger a manual purge to call `MADV_DONTNEED` for all unused pages:

```c
// Purge all arenas at once
mallctl("arena.4096.purge", NULL, NULL, NULL, 0);

// Or purge a specific arena
unsigned arena_index = 0;
char mib_name[64];
snprintf(mib_name, sizeof(mib_name), "arena.%u.purge", arena_index);
mallctl(mib_name, NULL, NULL, NULL, 0);
```

## Complete Example

See `manual_purge_example.c` for a complete working example that:
1. Disables decay for all arenas
2. Allocates and frees memory to create dirty/muzzy pages
3. Triggers a manual global purge
4. Displays purge statistics

## Understanding the Mallctl Interfaces

### Decay Control
- **`arena.<i>.dirty_decay_ms`**: Controls dirty page decay time
  - `-1`: Disable decay (never purge automatically)
  - `0`: Purge immediately
  - `> 0`: Purge after N milliseconds
  
- **`arena.<i>.muzzy_decay_ms`**: Controls muzzy page decay time
  - Same values as `dirty_decay_ms`

- **`arena.4096.*`**: Special index (`MALLCTL_ARENAS_ALL`) that applies to all arenas

### Purge Control
- **`arena.<i>.purge`**: Manually trigger purge for arena `<i>`
  - Purges all unused dirty and muzzy pages
  - Calls `MADV_DONTNEED` (or `MADV_FREE` if available) for the pages
  - Synchronous operation (blocks until complete)

- **`arena.<i>.decay`**: Trigger decay-based purge (respects decay settings)
  - Only purges if decay time has elapsed
  - Less aggressive than `purge`

## What Happens During Purge

When you call `arena.<i>.purge`, jemalloc:

1. **Locks the arena** to prevent concurrent modifications
2. **Identifies unused extents** in dirty and muzzy caches
3. **Calls purge functions**:
   - For dirty→muzzy transition: `extent_purge_lazy_wrapper()` → `pages_purge_lazy()` → `madvise(MADV_DONTNEED)`
   - For muzzy→retained transition: `extent_dalloc_wrapper()` → `pages_purge_forced()` → `madvise(MADV_DONTNEED)`
4. **Updates statistics** (nmadvise, npurge, purged counters)

## Checking Purge Statistics

You can verify that purging occurred by checking statistics:

```c
uint64_t dirty_npurge, dirty_purged, muzzy_npurge, muzzy_purged;
size_t len = sizeof(uint64_t);

mallctl("stats.arenas.0.dirty_npurge", &dirty_npurge, &len, NULL, 0);
mallctl("stats.arenas.0.dirty_purged", &dirty_purged, &len, NULL, 0);
mallctl("stats.arenas.0.muzzy_npurge", &muzzy_npurge, &len, NULL, 0);
mallctl("stats.arenas.0.muzzy_purged", &muzzy_purged, &len, NULL, 0);

printf("Dirty: %lu purge operations, %lu pages purged\n", 
       dirty_npurge, dirty_purged);
printf("Muzzy: %lu purge operations, %lu pages purged\n",
       muzzy_npurge, muzzy_purged);
```

## Re-enabling Decay

To re-enable automatic decay-based purging:

```c
ssize_t decay_ms = 10000;  // 10 seconds
mallctl("arena.4096.dirty_decay_ms", NULL, NULL, &decay_ms, sizeof(ssize_t));
mallctl("arena.4096.muzzy_decay_ms", NULL, NULL, &decay_ms, sizeof(ssize_t));
```

## Important Notes

1. **Decay disabled means no automatic purging**: With decay disabled (`-1`), jemalloc will NOT automatically purge unused pages. You must manually call `purge` when needed.

2. **Purge is synchronous**: The `arena.<i>.purge` operation blocks until all purging is complete. For large applications, this may take some time.

3. **Memory may not be immediately returned to OS**: `MADV_DONTNEED` tells the kernel the pages are not needed, but the kernel may not immediately free them. The pages will be freed when memory pressure occurs.

4. **Background threads**: If background threads are enabled, they may still attempt to purge even with decay disabled. To disable background threads for full manual control:
   ```c
   bool enable = false;
   mallctl("background_thread", NULL, NULL, &enable, sizeof(bool));
   ```
   Note: This must be done before any allocations occur, or you may need to set it via `MALLOC_CONF` environment variable at program startup.

5. **Arena index 4096**: The special value `4096` (`MALLCTL_ARENAS_ALL`) applies operations to all arenas. Use this for global operations.

## Compilation

Compile your program with jemalloc:

```bash
gcc -o your_program your_program.c -ljemalloc
```

Or link statically if jemalloc was built as a static library.

## See Also

- `MADV_DONTNEED_ANALYSIS.md` - Detailed analysis of when `MADV_DONTNEED` is called
- jemalloc documentation: `doc/jemalloc.xml.in` (search for "decay" and "purge")
- jemalloc source: `src/ctl.c` (mallctl implementations)
- jemalloc source: `src/pac.c` (purge implementation)
