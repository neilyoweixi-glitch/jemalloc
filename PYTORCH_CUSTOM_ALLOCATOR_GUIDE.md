# How to Implement a Customized Allocator in PyTorch

This guide explains how to implement a custom allocator for PyTorch, including integration with jemalloc.

## Overview

PyTorch has its own C++ allocator system (`c10::Allocator`) that can be customized. PyTorch can also use jemalloc as its underlying allocator. There are several approaches:

1. **Custom PyTorch Allocator** - Implement PyTorch's `c10::Allocator` interface
2. **jemalloc with Custom Extent Hooks** - Use jemalloc with custom memory management
3. **Hybrid Approach** - Combine PyTorch allocator with jemalloc

## Method 1: Custom PyTorch Allocator (C++)

### Basic Structure

```cpp
#include <c10/core/Allocator.h>
#include <c10/util/Exception.h>

class CustomPyTorchAllocator : public c10::Allocator {
public:
    void* allocate(size_t size) override {
        // Your custom allocation logic
        void* ptr = custom_malloc(size);
        if (!ptr) {
            throw std::bad_alloc();
        }
        return ptr;
    }
    
    void deallocate(void* ptr) override {
        if (ptr) {
            custom_free(ptr);
        }
    }
    
    // Optional: Get actual allocated size
    size_t get_allocated_size(void* ptr) const override {
        return custom_get_size(ptr);
    }
    
    // Optional: Get memory statistics
    c10::AllocatorStats getStats() const override {
        c10::AllocatorStats stats;
        // Fill in your stats
        return stats;
    }
    
    // Optional: Clear cache
    void emptyCache() override {
        // Clear any caches
    }
};
```

### Registering the Allocator

```cpp
#include <c10/core/Allocator.h>
#include <ATen/core/Allocator.h>

// Create your custom allocator
static CustomPyTorchAllocator custom_allocator;

// Register it for CPU
at::SetAllocator(at::DeviceType::CPU, &custom_allocator);

// Or register for CUDA
at::SetAllocator(at::DeviceType::CUDA, &custom_allocator);
```

## Method 2: Using jemalloc with PyTorch

### Step 1: Build PyTorch with jemalloc

```bash
# Set environment variable to use jemalloc
export USE_JEMALLOC=1

# Build PyTorch
python setup.py build
```

### Step 2: Configure jemalloc for PyTorch

Set jemalloc options via `MALLOC_CONF`:

```bash
export MALLOC_CONF="dirty_decay_ms:1000,muzzy_decay_ms:1000"
python your_pytorch_script.py
```

### Step 3: Customize jemalloc via Python

```python
import ctypes
import jemalloc

# Configure jemalloc settings
jemalloc.mallctl(b"arena.0.dirty_decay_ms", None, None, ctypes.c_ssize_t(-1), ctypes.sizeof(ctypes.c_ssize_t))
jemalloc.mallctl(b"arena.0.muzzy_decay_ms", None, None, ctypes.c_ssize_t(-1), ctypes.sizeof(ctypes.c_ssize_t))

# Your PyTorch code
import torch
tensor = torch.randn(1000, 1000)
```

## Method 3: Custom jemalloc Extent Hooks (Advanced)

This allows you to customize how jemalloc manages memory while PyTorch uses jemalloc.

### Implementing Custom Extent Hooks

```c
#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Custom allocator statistics
typedef struct {
    size_t total_allocated;
    size_t total_freed;
    size_t current_allocated;
} custom_allocator_stats_t;

static custom_allocator_stats_t stats = {0};

// Custom extent allocation hook
static void *
custom_extent_alloc(extent_hooks_t *extent_hooks, void *new_addr,
    size_t size, size_t alignment, bool *zero, bool *commit,
    unsigned arena_ind) {
    
    // Log allocation
    printf("Custom alloc: size=%zu, alignment=%zu\n", size, alignment);
    
    // Use mmap or your custom allocation
    void *ptr = mmap(new_addr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    
    // Zero memory if requested
    if (*zero) {
        memset(ptr, 0, size);
    }
    
    stats.total_allocated += size;
    stats.current_allocated += size;
    
    return ptr;
}

// Custom extent deallocation hook
static bool
custom_extent_dalloc(extent_hooks_t *extent_hooks, void *addr,
    size_t size, bool committed, unsigned arena_ind) {
    
    printf("Custom dalloc: addr=%p, size=%zu\n", addr, size);
    
    // Don't actually free here - let destroy handle it
    // This allows jemalloc to manage the memory
    return false; // Return false to indicate we didn't handle it
}

// Custom extent destroy hook
static void
custom_extent_destroy(extent_hooks_t *extent_hooks, void *addr,
    size_t size, bool committed, unsigned arena_ind) {
    
    printf("Custom destroy: addr=%p, size=%zu\n", addr, size);
    
    munmap(addr, size);
    stats.total_freed += size;
    stats.current_allocated -= size;
}

// Custom extent commit hook (optional)
static bool
custom_extent_commit(extent_hooks_t *extent_hooks, void *addr,
    size_t size, size_t offset, size_t length, unsigned arena_ind) {
    
    // Memory is already committed via mmap
    return false; // Use default behavior
}

// Custom extent decommit hook (optional)
static bool
custom_extent_decommit(extent_hooks_t *extent_hooks, void *addr,
    size_t size, size_t offset, size_t length, unsigned arena_ind) {
    
    // Use madvise to decommit
    if (madvise((char*)addr + offset, length, MADV_DONTNEED) == 0) {
        return false; // Success
    }
    return true; // Failure
}

// Custom extent purge hook (optional)
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

// Custom extent split hook (optional)
static bool
custom_extent_split(extent_hooks_t *extent_hooks, void *addr,
    size_t size, size_t size_a, size_t size_b, bool committed,
    unsigned arena_ind) {
    
    // Allow splitting
    return false;
}

// Custom extent merge hook (optional)
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

// Function to install custom hooks
void install_custom_allocator(void) {
    size_t hooks_size = sizeof(extent_hooks_t);
    extent_hooks_t *old_hooks;
    
    // Set custom hooks for arena 0
    int ret = mallctl("arena.0.extent_hooks", &old_hooks, &hooks_size,
                      &custom_extent_hooks, hooks_size);
    
    if (ret == 0) {
        printf("Custom allocator hooks installed successfully\n");
    } else {
        fprintf(stderr, "Failed to install custom hooks: %d\n", ret);
    }
}

// Function to get statistics
void print_allocator_stats(void) {
    printf("=== Custom Allocator Statistics ===\n");
    printf("Total allocated: %zu bytes\n", stats.total_allocated);
    printf("Total freed:     %zu bytes\n", stats.total_freed);
    printf("Current:         %zu bytes\n", stats.current_allocated);
}
```

### Using Custom Hooks with PyTorch

```python
# custom_allocator.py
import ctypes
import ctypes.util

# Load jemalloc
jemalloc_lib = ctypes.CDLL(ctypes.util.find_library('jemalloc'))

# Load your custom allocator library
custom_lib = ctypes.CDLL('./libcustom_allocator.so')

# Install custom hooks
custom_lib.install_custom_allocator()

# Now import PyTorch - it will use jemalloc with your custom hooks
import torch

# Your PyTorch code
tensor = torch.randn(1000, 1000)

# Print statistics
custom_lib.print_allocator_stats()
```

## Method 4: Python Wrapper for Custom Allocator

### Python C Extension

```python
# custom_allocator_python.py
import ctypes
import torch

class CustomAllocator:
    def __init__(self):
        self.lib = ctypes.CDLL('./libcustom_allocator.so')
        self.lib.install_custom_allocator()
    
    def get_stats(self):
        self.lib.print_allocator_stats()
    
    def configure(self, **kwargs):
        # Configure allocator settings
        pass

# Use it
allocator = CustomAllocator()
import torch
tensor = torch.randn(1000, 1000)
allocator.get_stats()
```

## Complete Example: Custom Allocator with Statistics

```c
// custom_pytorch_allocator.c
#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef struct {
    size_t pytorch_allocations;
    size_t pytorch_bytes;
    size_t current_bytes;
} pytorch_stats_t;

static pytorch_stats_t stats = {0};

static void *
pytorch_extent_alloc(extent_hooks_t *extent_hooks, void *new_addr,
    size_t size, size_t alignment, bool *zero, bool *commit,
    unsigned arena_ind) {
    
    void *ptr = mmap(new_addr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    
    if (*zero) {
        memset(ptr, 0, size);
    }
    
    stats.pytorch_allocations++;
    stats.pytorch_bytes += size;
    stats.current_bytes += size;
    
    return ptr;
}

static void
pytorch_extent_destroy(extent_hooks_t *extent_hooks, void *addr,
    size_t size, bool committed, unsigned arena_ind) {
    
    munmap(addr, size);
    stats.current_bytes -= size;
}

static extent_hooks_t pytorch_extent_hooks = {
    pytorch_extent_alloc,
    NULL,  // dalloc - use default
    pytorch_extent_destroy,
    NULL,  // commit - use default
    NULL,  // decommit - use default
    NULL,  // purge_lazy - use default
    NULL,  // purge_forced - use default
    NULL,  // split - use default
    NULL   // merge - use default
};

void pytorch_install_custom_allocator(void) {
    size_t hooks_size = sizeof(extent_hooks_t);
    mallctl("arena.0.extent_hooks", NULL, NULL,
            &pytorch_extent_hooks, hooks_size);
}

void pytorch_print_stats(void) {
    printf("PyTorch Allocator Stats:\n");
    printf("  Allocations: %zu\n", stats.pytorch_allocations);
    printf("  Total bytes: %zu\n", stats.pytorch_bytes);
    printf("  Current bytes: %zu\n", stats.current_bytes);
}
```

Compile:
```bash
gcc -shared -fPIC -o libpytorch_allocator.so \
    custom_pytorch_allocator.c -ljemalloc
```

Use in Python:
```python
import ctypes
lib = ctypes.CDLL('./libpytorch_allocator.so')
lib.pytorch_install_custom_allocator()

import torch
tensor = torch.randn(1000, 1000)
lib.pytorch_print_stats()
```

## PyTorch-Specific Considerations

### 1. CUDA Allocator

PyTorch has separate allocators for CPU and CUDA:

```cpp
// CPU allocator
at::SetAllocator(at::DeviceType::CPU, &cpu_allocator);

// CUDA allocator  
at::SetAllocator(at::DeviceType::CUDA, &cuda_allocator);
```

### 2. Memory Pinning

For CUDA, you may need to handle pinned memory:

```cpp
class CustomCUDAAllocator : public c10::Allocator {
    void* allocate(size_t size) override {
        void* ptr;
        cudaMallocHost(&ptr, size); // Pinned memory
        return ptr;
    }
    
    void deallocate(void* ptr) override {
        cudaFreeHost(ptr);
    }
};
```

### 3. Memory Statistics

PyTorch provides memory statistics APIs:

```python
import torch

# Get memory stats
print(torch.cuda.memory_stats())
print(torch.cuda.memory_summary())

# For CPU (if using jemalloc)
import ctypes
jemalloc = ctypes.CDLL('libjemalloc.so')
size_t = ctypes.c_size_t
resident = size_t()
len = ctypes.c_size_t(ctypes.sizeof(size_t))
jemalloc.mallctl(b"stats.resident", ctypes.byref(resident), 
                 ctypes.byref(len), None, 0)
print(f"Resident memory: {resident.value} bytes")
```

## Best Practices

### 1. Thread Safety

Ensure your custom allocator is thread-safe:

```c
#include <pthread.h>

static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

static void update_stats(size_t size) {
    pthread_mutex_lock(&stats_mutex);
    stats.current_bytes += size;
    pthread_mutex_unlock(&stats_mutex);
}
```

### 2. Error Handling

Always check for allocation failures:

```c
static void *custom_alloc(...) {
    void *ptr = mmap(...);
    if (ptr == MAP_FAILED) {
        // Log error
        return NULL;
    }
    return ptr;
}
```

### 3. Alignment

Respect alignment requirements:

```c
static void *aligned_alloc(size_t size, size_t alignment) {
    size_t total_size = size + alignment - 1;
    void *ptr = malloc(total_size);
    if (ptr) {
        void *aligned = (void*)(((uintptr_t)ptr + alignment - 1) & ~(alignment - 1));
        return aligned;
    }
    return NULL;
}
```

### 4. Memory Tracking

Track allocations for debugging:

```c
typedef struct allocation_info {
    void *ptr;
    size_t size;
    void *backtrace[10];
} allocation_info_t;

static allocation_info_t allocations[MAX_ALLOCATIONS];
```

## Integration with PyTorch Build System

### Modify PyTorch's CMakeLists.txt

```cmake
# In PyTorch's CMakeLists.txt
if(USE_JEMALLOC)
    find_package(jemalloc REQUIRED)
    target_link_libraries(torch PRIVATE jemalloc::jemalloc)
    
    # Add your custom allocator
    add_library(custom_allocator SHARED custom_allocator.c)
    target_link_libraries(custom_allocator PRIVATE jemalloc::jemalloc)
endif()
```

## Summary

| Method | Use Case | Complexity |
|--------|----------|------------|
| **Custom PyTorch Allocator** | Full control over PyTorch allocations | Medium |
| **jemalloc Configuration** | Tune jemalloc for PyTorch | Low |
| **Custom Extent Hooks** | Customize jemalloc memory management | High |
| **Hybrid Approach** | Combine PyTorch allocator with jemalloc | High |

**Recommended Approach:**
1. Start with jemalloc configuration (`MALLOC_CONF`)
2. If more control needed, implement custom PyTorch allocator
3. For advanced use cases, use custom extent hooks

## See Also

- PyTorch Allocator Documentation: `c10/core/Allocator.h`
- jemalloc Extent Hooks: `include/jemalloc/jemalloc_typedefs.h.in`
- PyTorch Memory Management: PyTorch documentation on memory allocators
