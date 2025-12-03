/*
 * Diagnostic tool to investigate why stats.resident < OS RSS
 *
 * Compile with:
 *   gcc -o diagnose_rss_difference diagnose_rss_difference.c -ljemalloc
 *
 * Usage:
 *   ./diagnose_rss_difference <pid>
 *   Or run without arguments to analyze current process
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <jemalloc/jemalloc.h>

static const char* format_bytes(size_t bytes) {
    static char buf[64];
    if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

static size_t read_proc_status(const char *key, pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, strlen(key)) == 0) {
            fclose(f);
            size_t value;
            char unit[8];
            if (sscanf(line + strlen(key), ": %zu %s", &value, unit) == 2) {
                if (strcmp(unit, "kB") == 0) {
                    return value * 1024;
                }
            }
            return value;
        }
    }
    fclose(f);
    return 0;
}

static unsigned count_threads(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    
    FILE *f = popen("find " path " -maxdepth 1 -type d 2>/dev/null | wc -l", "r");
    if (!f) {
        return 0;
    }
    
    unsigned count = 0;
    if (fscanf(f, "%u", &count) == 1) {
        pclose(f);
        return count - 1; // Subtract 1 for the task directory itself
    }
    pclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    pid_t pid = getpid();
    
    if (argc > 1) {
        pid = atoi(argv[1]);
    }
    
    printf("=== RSS vs jemalloc resident diagnostic ===\n");
    printf("Analyzing process PID: %d\n\n", pid);
    
    // Refresh jemalloc stats
    uint64_t epoch = 1;
    mallctl("epoch", NULL, NULL, &epoch, sizeof(uint64_t));
    
    // Get jemalloc statistics
    size_t allocated, active, resident, mapped, retained, metadata;
    size_t len = sizeof(size_t);
    
    mallctl("stats.allocated", &allocated, &len, NULL, 0);
    mallctl("stats.active", &active, &len, NULL, 0);
    mallctl("stats.resident", &resident, &len, NULL, 0);
    mallctl("stats.mapped", &mapped, &len, NULL, 0);
    mallctl("stats.retained", &retained, &len, NULL, 0);
    mallctl("stats.metadata", &metadata, &len, NULL, 0);
    
    printf("=== jemalloc Statistics ===\n");
    printf("Allocated:  %s (%zu bytes)\n", format_bytes(allocated), allocated);
    printf("Active:     %s (%zu bytes)\n", format_bytes(active), active);
    printf("Resident:   %s (%zu bytes)\n", format_bytes(resident), resident);
    printf("Mapped:     %s (%zu bytes)\n", format_bytes(mapped), mapped);
    printf("Retained:   %s (%zu bytes)\n", format_bytes(retained), retained);
    printf("Metadata:   %s (%zu bytes)\n", format_bytes(metadata), metadata);
    printf("\n");
    
    // Get OS RSS
    size_t rss_kb = read_proc_status("VmRSS", pid);
    size_t rss_bytes = rss_kb * 1024;
    
    size_t vmsize_kb = read_proc_status("VmSize", pid);
    size_t vmsize_bytes = vmsize_kb * 1024;
    
    size_t vmdata_kb = read_proc_status("VmData", pid);
    size_t vmdata_bytes = vmdata_kb * 1024;
    
    size_t vmstk_kb = read_proc_status("VmStk", pid);
    size_t vmstk_bytes = vmstk_kb * 1024;
    
    printf("=== OS Memory Statistics (from /proc/%d/status) ===\n", pid);
    printf("RSS (Resident Set Size):  %s (%zu bytes)\n", format_bytes(rss_bytes), rss_bytes);
    printf("VmSize (Virtual Size):    %s (%zu bytes)\n", format_bytes(vmsize_bytes), vmsize_bytes);
    printf("VmData (Heap):            %s (%zu bytes)\n", format_bytes(vmdata_bytes), vmdata_bytes);
    printf("VmStk (Stack):            %s (%zu bytes)\n", format_bytes(vmstk_bytes), vmstk_bytes);
    printf("\n");
    
    // Calculate difference
    size_t difference = 0;
    if (rss_bytes > resident) {
        difference = rss_bytes - resident;
        printf("=== Difference Analysis ===\n");
        printf("OS RSS:                  %s\n", format_bytes(rss_bytes));
        printf("jemalloc resident:       %s\n", format_bytes(resident));
        printf("Difference:              %s (%.1f%% of RSS)\n", 
               format_bytes(difference), 
               (difference * 100.0) / rss_bytes);
        printf("\n");
        
        // Estimate thread stack usage
        unsigned nthreads = count_threads(pid);
        printf("Thread count:            %u\n", nthreads);
        
        // Default stack size is usually 8MB on Linux
        size_t default_stack_size = 8 * 1024 * 1024;
        size_t estimated_stack_usage = nthreads * default_stack_size;
        
        printf("Estimated stack usage:   %s (%u threads × 8 MB)\n", 
               format_bytes(estimated_stack_usage), nthreads);
        
        // Calculate other memory
        size_t other_memory = difference;
        if (estimated_stack_usage < difference) {
            other_memory = difference - estimated_stack_usage;
        }
        
        printf("Other memory (estimated): %s\n", format_bytes(other_memory));
        printf("\n");
        
        printf("=== Breakdown of Difference ===\n");
        printf("jemalloc resident:       %s (%.1f%%)\n", 
               format_bytes(resident), (resident * 100.0) / rss_bytes);
        printf("+ Estimated stacks:      %s (%.1f%%)\n", 
               format_bytes(estimated_stack_usage), 
               (estimated_stack_usage * 100.0) / rss_bytes);
        printf("+ Other memory:          %s (%.1f%%)\n", 
               format_bytes(other_memory), 
               (other_memory * 100.0) / rss_bytes);
        printf("= Total RSS:             %s (100.0%%)\n", format_bytes(rss_bytes));
        printf("\n");
        
        printf("=== What 'Other Memory' Likely Includes ===\n");
        printf("  - Shared library code segments (.so files)\n");
        printf("  - Memory allocated by other libraries (not jemalloc)\n");
        printf("  - Memory-mapped files\n");
        printf("  - Other memory mappings\n");
        printf("  - jemalloc library code itself\n");
        printf("  - Additional thread overhead\n");
        printf("\n");
    } else {
        printf("=== Note ===\n");
        printf("jemalloc resident (%s) >= OS RSS (%s)\n", 
               format_bytes(resident), format_bytes(rss_bytes));
        printf("This is unusual - jemalloc resident should be <= RSS\n");
        printf("Possible reasons:\n");
        printf("  - Timing difference between measurements\n");
        printf("  - Memory was purged between measurements\n");
        printf("  - Process RSS was measured at different time\n");
        printf("\n");
    }
    
    // Compare with VmData (heap)
    printf("=== Comparison with VmData (Heap) ===\n");
    printf("VmData (heap):            %s\n", format_bytes(vmdata_bytes));
    printf("jemalloc resident:        %s\n", format_bytes(resident));
    if (vmdata_bytes > resident) {
        size_t heap_diff = vmdata_bytes - resident;
        printf("Difference:               %s\n", format_bytes(heap_diff));
        printf("(VmData includes some non-jemalloc heap allocations)\n");
    } else {
        printf("(VmData is close to jemalloc resident - good match!)\n");
    }
    printf("\n");
    
    printf("=== Recommendations ===\n");
    if (difference > rss_bytes * 0.3) {
        printf("⚠️  Large difference detected (>30%% of RSS)\n");
        printf("   This may indicate:\n");
        printf("   - Many threads (check thread count)\n");
        printf("   - Large shared libraries\n");
        printf("   - Memory allocated outside jemalloc\n");
        printf("   - Memory-mapped files\n");
        printf("\n");
        printf("   To investigate further:\n");
        printf("   - Check: pmap -x %d\n", pid);
        printf("   - Check: cat /proc/%d/smaps\n", pid);
        printf("   - Check: cat /proc/%d/maps\n", pid);
    } else {
        printf("✓ Difference is within expected range\n");
        printf("  The difference is likely due to:\n");
        printf("  - Thread stacks\n");
        printf("  - Shared library code\n");
        printf("  - Normal overhead\n");
    }
    
    return 0;
}
