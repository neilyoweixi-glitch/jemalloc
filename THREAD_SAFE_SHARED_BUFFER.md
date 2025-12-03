# Sharing jemalloc Buffers Between Threads Safely

This guide explains how to safely share jemalloc-allocated buffers between threads and ensure thread-safe deallocation.

## Overview

jemalloc's `malloc()` and `free()` functions are **thread-safe** - you can allocate memory in one thread and free it in another thread. However, you still need to implement proper synchronization to avoid:
- **Double-free**: Freeing the same pointer twice
- **Use-after-free**: Using memory after it's been freed
- **Race conditions**: Concurrent access without synchronization

## Key Points

### 1. jemalloc's Thread Safety

jemalloc guarantees:
- ✅ `malloc()` / `mallocx()` are thread-safe
- ✅ `free()` / `dallocx()` are thread-safe
- ✅ You can allocate in thread A and free in thread B
- ✅ Multiple threads can allocate/free concurrently

**However**, you must ensure:
- ❌ Only one thread frees a given pointer
- ❌ No thread uses memory after it's freed
- ❌ Proper synchronization for reference counting

## Method 1: Reference Counting (Recommended)

Use atomic reference counting to track how many threads are using the buffer:

```c
#include <jemalloc/jemalloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct {
    void *ptr;
    size_t size;
    atomic_int ref_count;
} shared_buffer_t;

// Allocate a shared buffer
shared_buffer_t* shared_buffer_alloc(size_t size) {
    shared_buffer_t *buf = malloc(sizeof(shared_buffer_t));
    if (!buf) {
        return NULL;
    }
    
    buf->ptr = mallocx(size, 0);
    if (!buf->ptr) {
        free(buf);
        return NULL;
    }
    
    buf->size = size;
    atomic_init(&buf->ref_count, 1); // Start with ref count of 1
    return buf;
}

// Increment reference count (call before sharing)
shared_buffer_t* shared_buffer_ref(shared_buffer_t *buf) {
    if (buf) {
        atomic_fetch_add(&buf->ref_count, 1);
    }
    return buf;
}

// Decrement reference count and free if zero
void shared_buffer_unref(shared_buffer_t *buf) {
    if (!buf) {
        return;
    }
    
    int old_count = atomic_fetch_sub(&buf->ref_count, 1);
    
    if (old_count == 1) {
        // Last reference - safe to free
        dallocx(buf->ptr, 0);
        free(buf);
    }
    // Otherwise, other threads still have references
}
```

### Usage Example

```c
// Thread 1: Allocate and share
shared_buffer_t *buf = shared_buffer_alloc(1024 * 1024);
shared_buffer_ref(buf); // Increment before sharing

// Pass to thread 2
pthread_create(&thread2, NULL, thread2_func, shared_buffer_ref(buf));

// Thread 1 can also use it
use_buffer(buf);

// Thread 1 done - decrement ref count
shared_buffer_unref(buf);

// Thread 2: Use and free
void* thread2_func(void *arg) {
    shared_buffer_t *buf = (shared_buffer_t*)arg;
    
    use_buffer(buf);
    
    // Decrement ref count when done
    shared_buffer_unref(buf);
    
    return NULL;
}
```

## Method 2: Ownership Transfer

Transfer ownership explicitly - only one thread owns the buffer at a time:

```c
#include <jemalloc/jemalloc.h>
#include <stdatomic.h>
#include <pthread.h>

typedef struct {
    void *ptr;
    size_t size;
    atomic_bool owned;
    pthread_mutex_t mutex;
} owned_buffer_t;

owned_buffer_t* owned_buffer_alloc(size_t size) {
    owned_buffer_t *buf = malloc(sizeof(owned_buffer_t));
    if (!buf) {
        return NULL;
    }
    
    buf->ptr = mallocx(size, 0);
    if (!buf->ptr) {
        free(buf);
        return NULL;
    }
    
    buf->size = size;
    atomic_init(&buf->owned, true);
    pthread_mutex_init(&buf->mutex, NULL);
    return buf;
}

// Transfer ownership from current thread to another
bool owned_buffer_transfer(owned_buffer_t *buf) {
    pthread_mutex_lock(&buf->mutex);
    bool was_owned = atomic_exchange(&buf->owned, true);
    pthread_mutex_unlock(&buf->mutex);
    return was_owned;
}

// Release ownership and free
void owned_buffer_release(owned_buffer_t *buf) {
    pthread_mutex_lock(&buf->mutex);
    bool owned = atomic_exchange(&buf->owned, false);
    pthread_mutex_unlock(&buf->mutex);
    
    if (owned) {
        dallocx(buf->ptr, 0);
        pthread_mutex_destroy(&buf->mutex);
        free(buf);
    }
}
```

## Method 3: Using jemalloc's Thread-Local Storage

Use jemalloc's thread-specific data to track ownership:

```c
#include <jemalloc/jemalloc.h>
#include <pthread.h>

// Thread-local storage for tracking allocations
static pthread_key_t alloc_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

void init_key(void) {
    pthread_key_create(&alloc_key, NULL);
}

typedef struct {
    void *ptr;
    size_t size;
    pthread_t owner_thread;
} tracked_buffer_t;

tracked_buffer_t* tracked_buffer_alloc(size_t size) {
    pthread_once(&key_once, init_key);
    
    tracked_buffer_t *buf = malloc(sizeof(tracked_buffer_t));
    if (!buf) {
        return NULL;
    }
    
    buf->ptr = mallocx(size, 0);
    if (!buf->ptr) {
        free(buf);
        return NULL;
    }
    
    buf->size = size;
    buf->owner_thread = pthread_self();
    return buf;
}

// Transfer ownership to another thread
void tracked_buffer_transfer(tracked_buffer_t *buf, pthread_t new_owner) {
    buf->owner_thread = new_owner;
}

// Free only if called from owner thread
bool tracked_buffer_free(tracked_buffer_t *buf) {
    if (pthread_equal(buf->owner_thread, pthread_self())) {
        dallocx(buf->ptr, 0);
        free(buf);
        return true;
    }
    return false; // Not the owner
}
```

## Method 4: Using jemalloc's Arena Assignment

Assign buffers to a specific arena and use arena-level synchronization:

```c
#include <jemalloc/jemalloc.h>
#include <stdatomic.h>

// Create a dedicated arena for shared buffers
unsigned create_shared_arena(void) {
    unsigned arena_ind;
    size_t len = sizeof(unsigned);
    
    // Create new arena
    mallctl("arenas.create", &arena_ind, &len, NULL, 0);
    
    return arena_ind;
}

// Allocate from specific arena
void* arena_alloc_shared(unsigned arena_ind, size_t size) {
    return mallocx(size, MALLOCX_ARENA(arena_ind));
}

// Free from any thread (jemalloc handles thread safety)
void arena_free_shared(void *ptr) {
    dallocx(ptr, 0); // jemalloc is thread-safe
}
```

## Complete Example: Reference-Counted Shared Buffer

```c
/*
 * Example: Thread-safe shared buffer with reference counting
 *
 * Compile with:
 *   gcc -o shared_buffer_example shared_buffer_example.c -ljemalloc -lpthread
 */

#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

typedef struct {
    void *data;
    size_t size;
    atomic_int ref_count;
    pthread_mutex_t mutex; // Protect the structure itself
} shared_buffer_t;

shared_buffer_t* shared_buffer_create(size_t size) {
    shared_buffer_t *buf = malloc(sizeof(shared_buffer_t));
    if (!buf) {
        return NULL;
    }
    
    // Allocate using jemalloc
    buf->data = mallocx(size, 0);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    buf->size = size;
    atomic_init(&buf->ref_count, 1);
    pthread_mutex_init(&buf->mutex, NULL);
    
    printf("Created shared buffer: %p, size: %zu\n", buf->data, size);
    return buf;
}

shared_buffer_t* shared_buffer_ref(shared_buffer_t *buf) {
    if (!buf) {
        return NULL;
    }
    
    int old_count = atomic_fetch_add(&buf->ref_count, 1);
    printf("Thread %lu: Incremented ref count to %d\n", 
           pthread_self(), old_count + 1);
    return buf;
}

void shared_buffer_unref(shared_buffer_t *buf) {
    if (!buf) {
        return;
    }
    
    int old_count = atomic_fetch_sub(&buf->ref_count, 1);
    printf("Thread %lu: Decremented ref count to %d\n", 
           pthread_self(), old_count - 1);
    
    if (old_count == 1) {
        // Last reference - safe to free
        printf("Thread %lu: Freeing buffer %p\n", pthread_self(), buf->data);
        dallocx(buf->data, 0);
        pthread_mutex_destroy(&buf->mutex);
        free(buf);
    }
}

void* use_buffer(shared_buffer_t *buf) {
    if (!buf) {
        return NULL;
    }
    
    // Access the buffer (read/write)
    return buf->data;
}

// Thread function
void* thread_func(void *arg) {
    shared_buffer_t *buf = (shared_buffer_t*)arg;
    
    printf("Thread %lu: Using buffer %p\n", pthread_self(), buf->data);
    
    // Use the buffer
    void *data = use_buffer(buf);
    if (data) {
        // Simulate work
        sleep(1);
        memset(data, 0xAA, buf->size);
    }
    
    // Release reference when done
    shared_buffer_unref(buf);
    
    return NULL;
}

int main(void) {
    const size_t buffer_size = 1024 * 1024; // 1 MB
    
    // Create shared buffer
    shared_buffer_t *buf = shared_buffer_create(buffer_size);
    if (!buf) {
        fprintf(stderr, "Failed to create buffer\n");
        return 1;
    }
    
    // Create multiple threads sharing the buffer
    pthread_t threads[3];
    
    for (int i = 0; i < 3; i++) {
        // Increment ref count before sharing
        shared_buffer_ref(buf);
        
        if (pthread_create(&threads[i], NULL, thread_func, buf) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            shared_buffer_unref(buf); // Release ref on failure
            continue;
        }
    }
    
    // Main thread also uses the buffer
    printf("Main thread: Using buffer\n");
    use_buffer(buf);
    sleep(1);
    
    // Main thread releases its reference
    shared_buffer_unref(buf);
    
    // Wait for all threads
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("All threads completed\n");
    return 0;
}
```

## Method 5: Using jemalloc's Internal APIs (Advanced)

For advanced use cases, you can use jemalloc's internal thread-safe deallocation:

```c
#include <jemalloc/jemalloc.h>
#include <stdatomic.h>

// Use jemalloc's thread-safe free directly
// jemalloc handles all thread safety internally

typedef struct {
    void *ptr;
    size_t size;
    atomic_int ref_count;
} safe_buffer_t;

safe_buffer_t* safe_buffer_alloc(size_t size) {
    safe_buffer_t *buf = malloc(sizeof(safe_buffer_t));
    if (!buf) {
        return NULL;
    }
    
    // Allocate using jemalloc (thread-safe)
    buf->ptr = mallocx(size, 0);
    if (!buf->ptr) {
        free(buf);
        return NULL;
    }
    
    buf->size = size;
    atomic_init(&buf->ref_count, 1);
    return buf;
}

void safe_buffer_unref(safe_buffer_t *buf) {
    if (!buf) {
        return;
    }
    
    int old_count = atomic_fetch_sub(&buf->ref_count, 1);
    
    if (old_count == 1) {
        // Safe to free from any thread - jemalloc is thread-safe
        dallocx(buf->ptr, 0);
        free(buf);
    }
}
```

## Best Practices

### 1. Always Use Reference Counting for Shared Buffers

```c
// Good: Reference counting
shared_buffer_ref(buf);  // Before sharing
shared_buffer_unref(buf); // When done

// Bad: Direct sharing without ref counting
// free(buf); // Dangerous - other threads might still use it
```

### 2. Use Atomic Operations

Always use atomic operations for reference counting:

```c
// Good: Atomic operations
atomic_fetch_add(&ref_count, 1);
atomic_fetch_sub(&ref_count, 1);

// Bad: Non-atomic operations
ref_count++; // Race condition!
```

### 3. Free from Any Thread (jemalloc is Thread-Safe)

jemalloc's `free()` and `dallocx()` are thread-safe:

```c
// Thread 1 allocates
void *ptr = mallocx(size, 0);

// Thread 2 can safely free it
dallocx(ptr, 0); // Safe!
```

### 4. Prevent Double-Free

Use reference counting or ownership tracking:

```c
// Good: Reference counting prevents double-free
if (atomic_fetch_sub(&ref_count, 1) == 1) {
    free(ptr); // Only free when ref count reaches zero
}

// Bad: No protection
free(ptr); // Could be freed multiple times!
```

### 5. Use MALLOCX_TCACHE_NONE for Shared Buffers

Disable tcache for shared buffers to avoid thread-local caching:

```c
// Allocate without tcache
void *ptr = mallocx(size, MALLOCX_TCACHE_NONE);

// Free without tcache
dallocx(ptr, MALLOCX_TCACHE_NONE);
```

## Common Patterns

### Pattern 1: Producer-Consumer

```c
// Producer thread
shared_buffer_t *buf = shared_buffer_create(size);
fill_buffer(buf);
shared_buffer_ref(buf); // Increment before passing
queue_push(buf);

// Consumer thread
shared_buffer_t *buf = queue_pop();
process_buffer(buf);
shared_buffer_unref(buf); // Decrement when done
```

### Pattern 2: Work Stealing

```c
// Main thread
shared_buffer_t *buf = shared_buffer_create(size);
for (int i = 0; i < num_workers; i++) {
    shared_buffer_ref(buf);
    worker_queue[i].push(buf);
}

// Worker threads
shared_buffer_t *buf = worker_queue[i].pop();
process_buffer(buf);
shared_buffer_unref(buf);
```

### Pattern 3: Broadcast

```c
// Broadcast buffer to multiple threads
shared_buffer_t *buf = shared_buffer_create(size);

for (int i = 0; i < num_threads; i++) {
    shared_buffer_ref(buf); // Each thread gets a reference
    pthread_create(&threads[i], NULL, worker, buf);
}

// Each worker thread
void* worker(void *arg) {
    shared_buffer_t *buf = (shared_buffer_t*)arg;
    use_buffer(buf);
    shared_buffer_unref(buf); // Release when done
    return NULL;
}
```

## Thread Safety Guarantees

### jemalloc Provides:

✅ **Thread-safe allocation**: `malloc()`, `mallocx()`  
✅ **Thread-safe deallocation**: `free()`, `dallocx()`  
✅ **Concurrent operations**: Multiple threads can allocate/free simultaneously  
✅ **Arena-level locking**: Internal synchronization prevents corruption

### You Must Provide:

❌ **Reference counting**: Track how many threads are using the buffer  
❌ **Synchronization**: Prevent use-after-free  
❌ **Ownership tracking**: Ensure only one thread frees (or use ref counting)

## Example: Complete Thread-Safe Buffer Manager

```c
#include <jemalloc/jemalloc.h>
#include <stdatomic.h>
#include <pthread.h>

typedef struct buffer_manager {
    shared_buffer_t *buffers[MAX_BUFFERS];
    atomic_int count;
    pthread_mutex_t mutex;
} buffer_manager_t;

buffer_manager_t* buffer_manager_create(void) {
    buffer_manager_t *mgr = malloc(sizeof(buffer_manager_t));
    atomic_init(&mgr->count, 0);
    pthread_mutex_init(&mgr->mutex, NULL);
    return mgr;
}

void buffer_manager_add(buffer_manager_t *mgr, shared_buffer_t *buf) {
    pthread_mutex_lock(&mgr->mutex);
    int idx = atomic_fetch_add(&mgr->count, 1);
    mgr->buffers[idx] = shared_buffer_ref(buf);
    pthread_mutex_unlock(&mgr->mutex);
}

void buffer_manager_remove(buffer_manager_t *mgr, shared_buffer_t *buf) {
    pthread_mutex_lock(&mgr->mutex);
    // Find and remove buffer
    for (int i = 0; i < atomic_load(&mgr->count); i++) {
        if (mgr->buffers[i] == buf) {
            shared_buffer_unref(buf);
            mgr->buffers[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&mgr->mutex);
}
```

## Summary

### Safe Practices:

1. ✅ **Use reference counting** - Track how many threads reference the buffer
2. ✅ **Use atomic operations** - For reference counting
3. ✅ **Free from any thread** - jemalloc's free is thread-safe
4. ✅ **Use MALLOCX_TCACHE_NONE** - For shared buffers to avoid tcache issues

### Unsafe Practices:

1. ❌ **Double-free** - Free the same pointer twice
2. ❌ **Use-after-free** - Access memory after freeing
3. ❌ **No synchronization** - Sharing without reference counting
4. ❌ **Non-atomic ref counting** - Race conditions in reference counting

**Key Takeaway**: jemalloc's `free()`/`dallocx()` are thread-safe, but you must implement proper synchronization (reference counting) to prevent double-free and use-after-free bugs.
