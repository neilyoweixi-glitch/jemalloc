# Understanding "Resident" Memory and Physical Memory Utilization in jemalloc

## What Does "Resident" Mean?

**Resident memory** (`stats.resident`) represents the total amount of physical RAM (resident set size) that jemalloc is currently using. It includes:

1. **Active pages** - Pages currently allocated and in use by your application
2. **Dirty pages** - Pages that were freed but haven't been purged yet (still consuming physical RAM)
3. **Metadata pages** - Base metadata overhead for jemalloc's internal structures

### How Resident is Calculated

From the source code (`src/pa_extra.c:116-119`):

```c
size_t resident_pgs = 0;
resident_pgs += pa_shard_nactive(shard);  // Active pages
resident_pgs += pa_shard_ndirty(shard);   // Dirty pages
*resident += (resident_pgs << LG_PAGE);   // Convert to bytes
```

Plus metadata overhead from base structures (`src/base.c`).

## Comparison of Memory Statistics

| Statistic | What It Includes | Best For |
|-----------|------------------|----------|
| **`stats.allocated`** | Memory actually allocated by application (via malloc/calloc) | Understanding application memory usage |
| **`stats.active`** | Pages currently in use (includes internal fragmentation) | Understanding jemalloc's page-level usage |
| **`stats.resident`** | Active pages + Dirty pages + Metadata | **Best estimate of physical RAM usage** |
| **`stats.mapped`** | Total virtual memory mapped from OS | Understanding virtual address space usage |
| **`stats.retained`** | Virtual memory retained for reuse (not returned to OS) | Understanding memory retention behavior |

## Which Statistic is Best for Physical Memory Utilization?

### **`stats.resident` is the best estimate for physical memory utilization**

**Why?**

1. **Includes dirty pages**: Dirty pages are still consuming physical RAM until they're purged via `MADV_DONTNEED` or `MADV_FREE`. These pages count toward your process's RSS (Resident Set Size).

2. **Includes metadata**: The base metadata overhead is also backed by physical memory.

3. **Matches OS RSS**: `stats.resident` should closely match what you see in tools like `top`, `htop`, or `/proc/<pid>/status` (RSS field).

### Important Notes

1. **Dirty pages inflate resident**: If you have many dirty pages (freed but not purged), `resident` will be higher than `active`. This is expected behavior - those pages are still consuming RAM.

2. **Muzzy pages are NOT included**: Muzzy pages (lazily purged) are not included in resident because they've been marked as unused to the kernel. However, the kernel may not have immediately freed them, so there can be a discrepancy.

3. **Mapped vs Resident**: `mapped` can be much larger than `resident` because:
   - Some mapped pages may be swapped out
   - Retained pages are mapped but may not be resident
   - The kernel may not have allocated physical pages for all mapped regions

## Example: Understanding the Difference

```c
// Allocate 10 MB
void *ptr = malloc(10 * 1024 * 1024);

// Refresh stats
uint64_t epoch = 1;
mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));

size_t allocated, active, resident, mapped;
size_t len = sizeof(size_t);

mallctl("stats.allocated", &allocated, &len, NULL, 0);
mallctl("stats.active", &active, &len, NULL, 0);
mallctl("stats.resident", &resident, &len, NULL, 0);
mallctl("stats.mapped", &mapped, &len, NULL, 0);

// allocated ≈ 10 MB (what you asked for)
// active ≈ 10 MB + small overhead (pages in use)
// resident ≈ active + metadata (physical RAM used)
// mapped ≥ resident (virtual memory mapped)

// Now free it
free(ptr);

// Refresh stats again
epoch = 1;
mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));

mallctl("stats.allocated", &allocated, &len, NULL, 0);
mallctl("stats.active", &active, &len, NULL, 0);
mallctl("stats.resident", &resident, &len, NULL, 0);

// allocated ≈ 0 MB (nothing allocated)
// active ≈ 0 MB (no active pages)
// resident ≈ dirty pages + metadata (still consuming RAM!)
//   - Dirty pages haven't been purged yet
//   - They're still backed by physical memory
```

## Relationship Between Statistics

```
Mapped (Virtual Memory)
  ├── Retained (not returned to OS)
  │   └── May or may not be resident
  └── Active (in use)
      ├── Allocated (application memory)
      └── Internal fragmentation overhead
  └── Dirty (freed but not purged)
      └── Still resident (consuming RAM)
  └── Muzzy (lazily purged)
      └── May or may not be resident
  └── Metadata
      └── Resident (always)

Resident = Active + Dirty + Metadata
```

## Recommendations

### For Monitoring Physical Memory Usage

**Use `stats.resident`** - This is the best estimate of physical RAM consumption.

```c
size_t resident;
size_t len = sizeof(size_t);
mallctl("stats.resident", &resident, &len, NULL, 0);
printf("Physical RAM used: %zu bytes\n", resident);
```

### For Understanding Application Memory Usage

**Use `stats.allocated`** - This shows what your application has actually allocated.

```c
size_t allocated;
size_t len = sizeof(size_t);
mallctl("stats.allocated", &allocated, &len, NULL, 0);
printf("Application memory: %zu bytes\n", allocated);
```

### For Understanding Memory Efficiency

**Compare `allocated` vs `active` vs `resident`**:

- **`allocated / active`**: Internal fragmentation ratio
- **`active / resident`**: How much of resident is actually in use
- **`resident / mapped`**: How much of mapped memory is actually resident

## Verifying Against OS Tools

You can verify `stats.resident` against OS tools:

```bash
# Check RSS from /proc
cat /proc/<pid>/status | grep VmRSS

# Or use top/htop
top -p <pid>
# Look at RES column

# Compare with jemalloc stats
# stats.resident should be close to RSS
```

**Note**: `stats.resident` is typically **smaller** than OS RSS because RSS includes:
- Thread stacks
- Shared library code segments
- Memory allocated by other libraries
- Memory-mapped files
- Other memory mappings

**See `RSS_VS_RESIDENT_DIFFERENCE.md` for detailed explanation of why `stats.resident < RSS` and how to investigate the difference.**

**See `diagnose_rss_difference.c` for a diagnostic tool to analyze the difference.**

## Summary

- **`stats.resident`** = Best estimate of physical RAM usage
  - Includes: Active pages + Dirty pages + Metadata
  - Closely matches OS RSS
  - Use this for monitoring physical memory consumption

- **`stats.allocated`** = Application memory usage
  - What your application has actually allocated
  - Use this for understanding application-level memory

- **`stats.active`** = Pages currently in use
  - Includes internal fragmentation
  - Use this for understanding jemalloc's page-level behavior

- **`stats.mapped`** = Virtual address space
  - Total virtual memory mapped
  - Can be much larger than resident

**For physical memory utilization, use `stats.resident`.**
