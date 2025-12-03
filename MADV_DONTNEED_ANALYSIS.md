# When is MADV_DONTNEED called in jemalloc 5.3.0?

## Overview

`MADV_DONTNEED` is used by jemalloc to tell the Linux kernel that memory pages are no longer needed and can be freed. This is part of jemalloc's page purging mechanism to reduce memory usage.

## Main Call Sites

### 1. `pages_purge_lazy()` - Lazy Page Purging
**Location:** `src/pages.c:456-491`

Called when jemalloc wants to lazily purge pages (mark them as unused without immediately zeroing them). This happens when:
- `MADV_FREE` is not available (e.g., on x86 Linux)
- The system is configured to use `MADV_DONTNEED` for lazy purging

**Code path:**
```c
pages_purge_lazy()
  → madvise(addr, size, MADV_DONTNEED)  // Line 484
```

**Condition:** Only called when `JEMALLOC_PURGE_MADVISE_DONTNEED` is defined AND `JEMALLOC_PURGE_MADVISE_DONTNEED_ZEROS` is NOT defined.

### 2. `pages_purge_forced()` - Forced Page Purging
**Location:** `src/pages.c:494-516`

Called when jemalloc needs to forcefully purge pages (ensuring they are zeroed on next access). This is used when:
- Pages need to be zeroed before reuse
- During extent zeroing operations

**Code path:**
```c
pages_purge_forced()
  → madvise(addr, size, MADV_DONTNEED)  // Line 505
```

**Condition:** Only called when both `JEMALLOC_PURGE_MADVISE_DONTNEED` and `JEMALLOC_PURGE_MADVISE_DONTNEED_ZEROS` are defined.

### 3. `pages_purge_process_madvise()` - Batch Purging
**Location:** `src/pages.c:650-656`

Uses the `process_madvise()` system call (introduced in Linux 5.4) to batch purge multiple memory regions at once. This is more efficient than calling `madvise()` multiple times.

**Code path:**
```c
pages_purge_process_madvise_impl()
  → syscall(SYS_process_madvise, pidfd, vec, vec_len, MADV_DONTNEED, 0)  // Line 652-653
```

**Condition:** Only available when `JEMALLOC_HAVE_PROCESS_MADVISE` is defined and `opt_process_madvise_max_batch > 0`.

## When Purge Operations Are Triggered

### Decay-Based Purging

The main mechanism for triggering purges is through the **decay system**:

1. **Background Thread Purging** (`pac_maybe_decay_purge()`)
   - Called periodically by background threads
   - Checks if decay epochs have advanced
   - Purges dirty/muzzy extents when thresholds are exceeded
   - **Location:** `src/pac.c:624-660`

2. **Decay Flow:**
   ```
   pac_maybe_decay_purge()
     → pac_decay_try_purge()
       → pac_decay_to_limit()
         → pac_decay_stashed()
           → extent_purge_lazy_wrapper()  // For dirty→muzzy transition
           → extent_dalloc_wrapper()      // For muzzy→retained transition
   ```

3. **Extent State Transitions:**
   - **Dirty → Muzzy:** Uses `extent_purge_lazy_wrapper()` → `pages_purge_lazy()` → `MADV_DONTNEED`
   - **Muzzy → Retained:** Uses `extent_dalloc_wrapper()` → may call `pages_purge_forced()` → `MADV_DONTNEED`

### Other Trigger Points

1. **Arena Reset/Destroy** (`pac_decay_all()`)
   - **Location:** `src/pac.c:605-610`
   - Purges all extents in dirty/muzzy caches

2. **Extent Zeroing** (`ehooks_default_zero_impl()`)
   - **Location:** `src/ehooks.c:231-245`
   - Uses `pages_purge_forced()` to zero memory when not using hugepages

3. **Base Metadata Purging** (`base.c`)
   - **Location:** `src/base.c:86-89`
   - Purges base metadata pages when possible

## Configuration

The use of `MADV_DONTNEED` depends on compile-time configuration:

- **`JEMALLOC_PURGE_MADVISE_DONTNEED`**: Defined when `madvise(MADV_DONTNEED)` is available
- **`JEMALLOC_PURGE_MADVISE_DONTNEED_ZEROS`**: Defined when `MADV_DONTNEED` zeros pages (runtime check performed)
- **`JEMALLOC_PURGE_MADVISE_FREE`**: If available, `MADV_FREE` is preferred over `MADV_DONTNEED` for lazy purging

## Runtime Detection

jemalloc performs a runtime check to verify `MADV_DONTNEED` behavior:
- **Location:** `src/pages.c:69-105` (`madvise_MADV_DONTNEED_zeroes_pages()`)
- Checks if `MADV_DONTNEED` actually zeros pages (required for forced purging)
- Important for QEMU compatibility (QEMU doesn't support zeroing via `MADV_DONTNEED`)
- Controlled by `opt_trust_madvise` option

## Summary

`MADV_DONTNEED` is called in jemalloc 5.3.0:

1. **During decay-based purging** - Periodically by background threads when extents transition from dirty→muzzy→retained states
2. **During forced purging** - When pages need to be zeroed (e.g., for zeroed allocations)
3. **During arena operations** - When arenas are reset or destroyed
4. **During extent zeroing** - When zeroing extents for reuse

The exact behavior depends on:
- System capabilities (`MADV_FREE` vs `MADV_DONTNEED`)
- Compile-time configuration
- Runtime detection of `MADV_DONTNEED` zeroing behavior
- Decay time settings (`dirty_decay_ms`, `muzzy_decay_ms`)
