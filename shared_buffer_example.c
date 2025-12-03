/*
 * Example: Thread-safe shared buffer using jemalloc with reference counting
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
#include <assert.h>

#define NUM_THREADS 4
#define BUFFER_SIZE (1024 * 1024) // 1 MB

// Shared buffer with reference counting
typedef struct {
    void *data;
    size_t size;
    atomic_int ref_count;
    pthread_mutex_t mutex; // Protect structure access
} shared_buffer_t;

// Create a new shared buffer
shared_buffer_t* shared_buffer_create(size_t size) {
    shared_buffer_t *buf = malloc(sizeof(shared_buffer_t));
    if (!buf) {
        return NULL;
    }
    
    // Allocate using jemalloc (thread-safe)
    buf->data = mallocx(size, MALLOCX_TCACHE_NONE); // Disable tcache for sharing
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    buf->size = size;
    atomic_init(&buf->ref_count, 1); // Start with ref count of 1
    pthread_mutex_init(&buf->mutex, NULL);
    
    printf("[Main] Created shared buffer: %p, size: %zu bytes\n", buf->data, size);
    return buf;
}

// Increment reference count (call before sharing with another thread)
shared_buffer_t* shared_buffer_ref(shared_buffer_t *buf) {
    if (!buf) {
        return NULL;
    }
    
    int old_count = atomic_fetch_add(&buf->ref_count, 1);
    printf("[Thread %lu] Incremented ref count: %d -> %d\n", 
           (unsigned long)pthread_self(), old_count, old_count + 1);
    return buf;
}

// Decrement reference count and free if zero
void shared_buffer_unref(shared_buffer_t *buf) {
    if (!buf) {
        return;
    }
    
    int old_count = atomic_fetch_sub(&buf->ref_count, 1);
    printf("[Thread %lu] Decremented ref count: %d -> %d\n", 
           (unsigned long)pthread_self(), old_count, old_count - 1);
    
    if (old_count == 1) {
        // Last reference - safe to free from any thread
        printf("[Thread %lu] Last reference - freeing buffer %p\n", 
               (unsigned long)pthread_self(), buf->data);
        
        // jemalloc's dallocx is thread-safe - can free from any thread
        dallocx(buf->data, MALLOCX_TCACHE_NONE);
        pthread_mutex_destroy(&buf->mutex);
        free(buf);
    }
}

// Get current reference count (for debugging)
int shared_buffer_get_ref_count(shared_buffer_t *buf) {
    if (!buf) {
        return 0;
    }
    return atomic_load(&buf->ref_count);
}

// Access buffer data (thread-safe read/write with external synchronization)
void* shared_buffer_get_data(shared_buffer_t *buf) {
    if (!buf) {
        return NULL;
    }
    return buf->data;
}

// Thread function that uses the shared buffer
void* thread_worker(void *arg) {
    shared_buffer_t *buf = (shared_buffer_t*)arg;
    pthread_t tid = pthread_self();
    
    printf("[Thread %lu] Started, buffer: %p\n", (unsigned long)tid, buf->data);
    
    // Access the buffer
    void *data = shared_buffer_get_data(buf);
    if (data) {
        // Simulate work with the buffer
        printf("[Thread %lu] Writing to buffer...\n", (unsigned long)tid);
        memset(data, (int)(unsigned long)tid & 0xFF, buf->size);
        
        // Simulate processing time
        usleep(100000); // 100ms
        
        printf("[Thread %lu] Reading from buffer...\n", (unsigned long)tid);
        // Read some data
        volatile char first_byte = ((char*)data)[0];
        (void)first_byte; // Use the value
        
        printf("[Thread %lu] Completed work with buffer\n", (unsigned long)tid);
    }
    
    // Release reference when done
    // This can be called from any thread - jemalloc handles thread safety
    shared_buffer_unref(buf);
    
    return NULL;
}

int main(void) {
    printf("=== Thread-Safe Shared Buffer Example ===\n\n");
    
    // Create a shared buffer
    shared_buffer_t *buf = shared_buffer_create(BUFFER_SIZE);
    if (!buf) {
        fprintf(stderr, "Failed to create shared buffer\n");
        return 1;
    }
    
    printf("Initial ref count: %d\n\n", shared_buffer_get_ref_count(buf));
    
    // Create multiple threads that will share the buffer
    pthread_t threads[NUM_THREADS];
    
    printf("Creating %d threads to share the buffer...\n", NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; i++) {
        // Increment reference count before sharing
        shared_buffer_ref(buf);
        
        if (pthread_create(&threads[i], NULL, thread_worker, buf) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            shared_buffer_unref(buf); // Release ref on failure
            continue;
        }
    }
    
    printf("\nMain thread also using the buffer...\n");
    void *data = shared_buffer_get_data(buf);
    if (data) {
        memset(data, 0x42, 100); // Write some data
        printf("Main thread wrote data to buffer\n");
    }
    
    // Main thread releases its reference
    printf("\nMain thread releasing reference...\n");
    shared_buffer_unref(buf);
    
    // Wait for all worker threads to complete
    printf("\nWaiting for all threads to complete...\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n=== All threads completed ===\n");
    printf("Buffer should be freed automatically when ref count reaches zero\n");
    
    return 0;
}
