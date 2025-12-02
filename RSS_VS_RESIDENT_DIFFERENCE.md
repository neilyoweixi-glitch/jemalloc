# Why stats.resident is Smaller Than RSS

If you find that `stats.resident` is significantly smaller than the RSS (Resident Set Size) reported by the OS, this is **normal and expected**. Here's why:

## What `stats.resident` Includes

`stats.resident` only tracks memory managed by jemalloc:

1. **Active pages** - Pages currently allocated and in use
2. **Dirty pages** - Freed pages not yet purged
3. **Base metadata** - jemalloc's internal metadata structures

From the source code:
```c
// src/pa_extra.c
resident = (nactive + ndirty) * PAGE_SIZE + base_metadata
```

## What `stats.resident` Does NOT Include

The OS RSS includes **all** memory used by your process, including:

### 1. **Thread Stacks**
Each thread has its own stack (typically 2-8 MB per thread by default on Linux).

```c
// Example: If you have 10 threads with 8MB stacks each
// That's 80 MB not counted in stats.resident
```

**How to check:**
```bash
# Check thread count
ps -eLf | grep <pid> | wc -l

# Check stack size (default is usually 8MB on Linux)
ulimit -s  # Shows stack size in KB
```

### 2. **Memory Allocated by Other Libraries**
If other libraries allocate memory without going through jemalloc:
- Direct `mmap()` calls
- System `malloc()` (if jemalloc isn't the default allocator)
- Third-party libraries using their own allocators
- Memory-mapped files

**Common culprits:**
- OpenSSL
- Image processing libraries
- Database libraries
- Network libraries

### 3. **Shared Libraries (Code Segments)**
The RSS includes code segments from shared libraries (.so files):
- jemalloc library itself
- libc, libpthread, etc.
- All dynamically loaded libraries

**How to check:**
```bash
# See memory mappings
cat /proc/<pid>/maps | grep '\.so'

# Or use pmap
pmap -x <pid>
```

### 4. **Memory Mapped Files**
Any memory-mapped files (`mmap()` with file backing):
- Memory-mapped I/O
- Shared memory segments
- Database memory maps

### 5. **Memory Allocated Before jemalloc Initialization**
If memory was allocated before jemalloc was initialized or if jemalloc isn't linked as the default allocator.

### 6. **jemalloc Internal Structures Not Fully Tracked**
Some internal structures might not be fully accounted for:
- Thread-local caches (tcache) - partially tracked
- Some metadata structures
- Arena internal structures

### 7. **Memory Allocated via Non-jemalloc Paths**
- Direct system calls (`mmap`, `brk`, `sbrk`)
- Memory allocated by the kernel
- Anonymous mappings created outside jemalloc

## How to Investigate the Difference

### Step 1: Get RSS from OS

```bash
# Method 1: From /proc
cat /proc/<pid>/status | grep VmRSS
# Output: VmRSS:    123456 kB

# Method 2: From ps
ps -o pid,rss,comm -p <pid>
# Output:   PID   RSS COMMAND
#          1234 123456 your_program

# Method 3: From top/htop
top -p <pid>
# Look at RES column
```

### Step 2: Get jemalloc resident

```c
uint64_t epoch = 1;
mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));

size_t resident;
size_t len = sizeof(size_t);
mallctl("stats.resident", &resident, &len, NULL, 0);

printf("jemalloc resident: %zu bytes (%.2f MB)\n", 
       resident, resident / (1024.0 * 1024.0));
```

### Step 3: Analyze the Difference

```bash
# Check memory mappings
pmap -x <pid>

# Or more detailed
cat /proc/<pid>/maps | awk '{print $1, $6}' | \
  awk -F- '{print $1, $2}' | \
  awk '{sum += ($2 - $1)} END {print sum " bytes"}'

# Check thread count and stack usage
ps -eLf | grep <pid> | wc -l
```

### Step 4: Check What's Using Memory

```bash
# Detailed memory breakdown
cat /proc/<pid>/smaps | grep -E "^Size:|^Rss:" | \
  awk '{if ($1 == "Size:") size=$2; if ($1 == "Rss:") {rss=$2; print size, rss}}'

# Or use smem (if available)
smem -p -P <pid>
```

## Expected Differences

### Typical Breakdown

For a typical application using jemalloc:

```
Total RSS (from OS) = 
  + jemalloc resident (stats.resident)
  + Thread stacks (~2-8 MB per thread)
  + Shared libraries code (~10-50 MB)
  + Other libraries' allocations
  + Memory-mapped files
  + Other memory mappings
```

### Example Calculation

```
OS RSS:              500 MB
jemalloc resident:   350 MB
Difference:          150 MB

Breakdown of 150 MB:
  - Thread stacks (10 threads × 8 MB):     80 MB
  - Shared libraries:                       40 MB
  - Other allocations:                      30 MB
```

## Common Scenarios

### Scenario 1: Multi-threaded Application

**Symptom**: Large difference, especially with many threads

**Cause**: Thread stacks not included in `stats.resident`

**Solution**: This is expected. Count threads and multiply by stack size.

### Scenario 2: Using Third-party Libraries

**Symptom**: Difference grows over time or with specific operations

**Cause**: Libraries allocating memory outside jemalloc

**Solution**: Check which libraries are being used and if they use jemalloc.

### Scenario 3: Memory-mapped Files

**Symptom**: Difference correlates with file I/O operations

**Cause**: Memory-mapped files counted in RSS but not jemalloc

**Solution**: Check for `mmap()` calls with file backing.

### Scenario 4: jemalloc Not Default Allocator

**Symptom**: Very large difference, especially if jemalloc is loaded via `LD_PRELOAD`

**Cause**: Some allocations go through system malloc

**Solution**: Ensure jemalloc is properly linked as the default allocator.

## How to Get More Accurate Comparison

### Option 1: Compare Against Heap Only

```bash
# Get heap size from /proc
cat /proc/<pid>/status | grep VmData
# VmData is closer to jemalloc's view (heap memory)

# Compare:
# stats.resident ≈ VmData (but still may differ)
```

### Option 2: Use jemalloc's Mapped Statistic

```c
size_t mapped;
size_t len = sizeof(size_t);
mallctl("stats.mapped", &mapped, &len, NULL, 0);

// mapped is closer to total virtual memory jemalloc uses
// But still doesn't include non-jemalloc memory
```

### Option 3: Add Thread Stack Estimation

```c
// Estimate thread stack usage
unsigned nthreads;
size_t len = sizeof(unsigned);
mallctl("stats.arenas.0.nthreads", &nthreads, &len, NULL, 0);

// Default stack size is usually 8MB on Linux
size_t estimated_stacks = nthreads * 8 * 1024 * 1024;

size_t resident;
len = sizeof(size_t);
mallctl("stats.resident", &resident, &len, NULL, 0);

size_t estimated_total = resident + estimated_stacks;
printf("jemalloc resident: %zu MB\n", resident / (1024*1024));
printf("+ estimated stacks: %zu MB\n", estimated_stacks / (1024*1024));
printf("= estimated total: %zu MB\n", estimated_total / (1024*1024));
printf("OS RSS: (check separately)\n");
```

## Is This a Problem?

**Generally, NO** - this is expected behavior:

1. ✅ `stats.resident` accurately reflects jemalloc-managed memory
2. ✅ OS RSS includes all process memory (as it should)
3. ✅ The difference is normal and expected

**However, investigate if:**

- The difference is unexpectedly large (>50% of RSS)
- The difference grows unexpectedly over time
- You suspect memory leaks outside jemalloc
- You need to account for all memory usage

## Summary

| Memory Type | Included in stats.resident? | Included in OS RSS? |
|-------------|----------------------------|---------------------|
| jemalloc active pages | ✅ Yes | ✅ Yes |
| jemalloc dirty pages | ✅ Yes | ✅ Yes |
| jemalloc metadata | ✅ Yes | ✅ Yes |
| Thread stacks | ❌ No | ✅ Yes |
| Shared library code | ❌ No | ✅ Yes |
| Other libraries' allocations | ❌ No | ✅ Yes |
| Memory-mapped files | ❌ No | ✅ Yes |
| Other memory mappings | ❌ No | ✅ Yes |

**Bottom line**: `stats.resident` is accurate for jemalloc-managed memory, but OS RSS includes everything. The difference is normal and expected.
