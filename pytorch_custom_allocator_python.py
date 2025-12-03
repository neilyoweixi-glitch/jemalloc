#!/usr/bin/env python3
"""
Example: Using Custom Allocator with PyTorch

Usage:
    python pytorch_custom_allocator_python.py
"""

import ctypes
import ctypes.util
import sys

# Load jemalloc library
jemalloc_path = ctypes.util.find_library('jemalloc')
if not jemalloc_path:
    print("Error: jemalloc library not found")
    print("Make sure jemalloc is installed and in LD_LIBRARY_PATH")
    sys.exit(1)

jemalloc_lib = ctypes.CDLL(jemalloc_path)

# Load custom allocator library
try:
    custom_lib = ctypes.CDLL('./libpytorch_custom_allocator.so')
except OSError as e:
    print(f"Error loading custom allocator: {e}")
    print("Make sure to compile custom_pytorch_allocator_example.c first:")
    print("  gcc -shared -fPIC -o libpytorch_custom_allocator.so \\")
    print("      custom_pytorch_allocator_example.c -ljemalloc")
    sys.exit(1)

# Define function signatures
custom_lib.install_custom_allocator.argtypes = []
custom_lib.install_custom_allocator.restype = None

custom_lib.print_allocator_stats.argtypes = []
custom_lib.print_allocator_stats.restype = None

custom_lib.get_current_bytes.argtypes = []
custom_lib.get_current_bytes.restype = ctypes.c_size_t

custom_lib.reset_stats.argtypes = []
custom_lib.reset_stats.restype = None

def configure_jemalloc():
    """Configure jemalloc settings for PyTorch"""
    # Disable decay for manual control
    ssize_t = ctypes.c_ssize_t
    size_t = ctypes.c_size_t
    
    # Disable dirty decay
    disable_decay = ssize_t(-1)
    jemalloc_lib.mallctl(
        b"arena.0.dirty_decay_ms",
        None, None,
        ctypes.byref(disable_decay),
        ctypes.sizeof(ssize_t)
    )
    
    # Disable muzzy decay
    jemalloc_lib.mallctl(
        b"arena.0.muzzy_decay_ms",
        None, None,
        ctypes.byref(disable_decay),
        ctypes.sizeof(ssize_t)
    )
    
    print("Configured jemalloc settings")

def main():
    print("=== PyTorch Custom Allocator Example ===\n")
    
    # Step 1: Install custom allocator hooks
    print("Step 1: Installing custom allocator hooks...")
    custom_lib.install_custom_allocator()
    
    # Step 2: Configure jemalloc
    print("Step 2: Configuring jemalloc...")
    configure_jemalloc()
    
    # Step 3: Import PyTorch (must be after allocator setup)
    print("Step 3: Importing PyTorch...")
    try:
        import torch
        print(f"PyTorch version: {torch.__version__}")
    except ImportError:
        print("Error: PyTorch not installed")
        sys.exit(1)
    
    # Step 4: Allocate some tensors
    print("\nStep 4: Allocating PyTorch tensors...")
    
    # Get initial stats
    initial_bytes = custom_lib.get_current_bytes()
    print(f"Initial allocated bytes: {initial_bytes}")
    
    # Create some tensors
    tensors = []
    for i in range(5):
        tensor = torch.randn(1000, 1000)
        tensors.append(tensor)
        current_bytes = custom_lib.get_current_bytes()
        print(f"  After tensor {i+1}: {current_bytes} bytes "
              f"({(current_bytes - initial_bytes) / (1024*1024):.2f} MB)")
    
    # Step 5: Print statistics
    print("\nStep 5: Allocator statistics:")
    custom_lib.print_allocator_stats()
    
    # Step 6: Free some tensors
    print("\nStep 6: Freeing some tensors...")
    del tensors[0:3]
    import gc
    gc.collect()
    
    # Step 7: Print updated statistics
    print("\nStep 7: Updated statistics:")
    custom_lib.print_allocator_stats()
    
    # Step 8: Manual purge (if needed)
    print("\nStep 8: Manual purge...")
    jemalloc_lib.mallctl(b"arena.0.purge", None, None, None, 0)
    
    print("\nStep 9: Final statistics:")
    custom_lib.print_allocator_stats()
    
    print("\n=== Example Complete ===")

if __name__ == "__main__":
    main()
