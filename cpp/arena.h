#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Default initial memory size (adjust for your project, e.g., 1MB for compilers)
#define ARENA_DEFAULT_SIZE (1024 * 1024)

// Default initial capacity for marker stack
#define ARENA_INITIAL_MARKER_CAP 16

// Alignment boundary (e.g., 8 bytes for 64-bit)
#define ARENA_ALIGNMENT 8

#ifndef ARENA_H
#define ARENA_H

class Arena {
    private:
        uint8_t *base;       // Start of the memory block
        uint8_t *bump;       // Current allocation pointer
        uint8_t *end;        // End of the memory block
        size_t *markers;     // Dynamic array for markers (offsets from base)
        size_t marker_count; // Number of active markers
        size_t marker_cap;   // Capacity of markers array
        Arena *next;         // For chaining if resizable (optional)

        // Helper to align upwards
        static size_t align_up(size_t n, size_t align);

        // Helper to get the last block in the chain
        static Arena* get_last_block(Arena* arena);

        // Helper to compute current global position (total allocated bytes)
        static size_t get_current_position(Arena* arena);

    public:
        Arena (size_t initial_size);

        // Allocate memory from the arena (grows via chaining if out of space)
        void* a_alloc(size_t bytes);

        // Allocate and zero-initialize
        void* a_calloc(size_t num, size_t size);

        // Reallocate memory in the arena (requires old_size; may allocate new space and copy)
        void* a_realloc(void* ptr, size_t old_size, size_t new_size);

        // Push a marker (saves current global position); operates on root
        void push_marker();

        // Pop a marker (resets to last saved global position); frees later blocks if needed
        void pop_marker();

        // Reset the entire arena chain (clears markers, resets to root base, frees chains)
        void reset();

        // Duplicate a string into the arena
        char* strdup(const char* str);

        ~Arena();
};

#endif // ARENA_H
