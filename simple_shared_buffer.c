/*
 * Simple Example: Sharing jemalloc buffer between two threads
 *
 * Key points:
 * 1. jemalloc's free()/dallocx() are thread-safe
 * 2. Use reference counting to prevent double-free
 * 3. Use atomic operations for reference counting
 *
 * Compile:
 *   gcc -o simple_shared_buffer simple_shared_buffer.c -ljemalloc -lpthread
 */

#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <string.h>

// Simple shared buffer with reference counting
typedef struct {
    void *data;
    size_t size;
    atomic_int ref_count;
} shared_buf_t;

// Allocate buffer
shared_buf_t* buf_alloc(size_t size) {
    shared_buf_t *b = malloc(sizeof(shared_buf_t));
    b->data = mallocx(size, MALLOCX_TCACHE_NONE); // Disable tcache
    b->size = size;
    atomic_init(&b->ref_count, 1);
    return b;
}

// Increment reference (call before sharing)
void buf_ref(shared_buf_t *b) {
    if (b) atomic_fetch_add(&b->ref_count, 1);
}

// Decrement reference and free if zero
void buf_unref(shared_buf_t *b) {
    if (!b) return;
    
    // Atomic decrement
    int count = atomic_fetch_sub(&b->ref_count, 1);
    
    // If this was the last reference, free it
    // Safe to free from any thread - jemalloc is thread-safe
    if (count == 1) {
        dallocx(b->data, MALLOCX_TCACHE_NONE);
        free(b);
    }
}

// Thread 1: Allocates and shares
void* thread1(void *arg) {
    shared_buf_t *buf = buf_alloc(1024);
    
    // Increment before sharing
    buf_ref(buf);
    
    // Pass to thread 2 (via shared variable, queue, etc.)
    *(shared_buf_t**)arg = buf;
    
    // Thread 1 uses buffer
    memset(buf->data, 0x11, buf->size);
    
    // Thread 1 done - decrement
    buf_unref(buf);
    
    return NULL;
}

// Thread 2: Receives and uses shared buffer
void* thread2(void *arg) {
    shared_buf_t *buf = *(shared_buf_t**)arg;
    
    // Thread 2 uses buffer
    memset(buf->data, 0x22, buf->size);
    
    // Thread 2 done - decrement (this will free if last reference)
    buf_unref(buf);
    
    return NULL;
}

int main(void) {
    shared_buf_t *shared = NULL;
    pthread_t t1, t2;
    
    // Thread 1 allocates and shares
    pthread_create(&t1, NULL, thread1, &shared);
    
    // Wait for thread 1 to set shared
    while (shared == NULL) {
        usleep(1000);
    }
    
    // Thread 2 uses shared buffer
    pthread_create(&t2, NULL, thread2, &shared);
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    printf("Both threads completed - buffer safely freed\n");
    return 0;
}
