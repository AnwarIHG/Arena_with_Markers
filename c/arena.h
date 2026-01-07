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

typedef struct Arena_t {
  uint8_t *base;       // Start of the memory block
  uint8_t *bump;       // Current allocation pointer
  uint8_t *end;        // End of the memory block
  size_t *markers;     // Dynamic array for markers (offsets from base)
  size_t marker_count; // Number of active markers
  size_t marker_cap;   // Capacity of markers array
  struct Arena_t *next;  // For chaining if resizable (optional)
} Arena_t;


extern Arena_t *arena;

// Utility to align upwards
static inline size_t align_up(size_t n, size_t align) {
  return (n + align - 1) & ~(align - 1);
}

// Helper to get the last block in the chain
static Arena_t* get_last_block(Arena_t* arena) {
    Arena_t* cur = arena;
    while (cur->next) {
        cur = cur->next;
    }
    return cur;
}

// Helper to compute current global position (total allocated bytes)
static size_t get_current_position(Arena_t* arena) {
    size_t pos = 0;
    Arena_t* cur = arena;
    while (cur->next) {
        pos += (size_t)(cur->end - cur->base);
        cur = cur->next;
    }
    pos += (size_t)(cur->bump - cur->base);
    return pos;
}

Arena_t *arena_create(size_t initial_size);
void arena_destroy(Arena_t *arena);

void* arena_alloc(Arena_t* arena, size_t bytes);
void* arena_calloc(Arena_t* arena, size_t num, size_t size);
void* arena_realloc(Arena_t* arena, void* ptr, size_t old_size, size_t new_size);

void arena_push_marker(Arena_t *arena);
void arena_pop_marker(Arena_t *arena);
void arena_reset(Arena_t *arena);

char* arena_strdup(Arena_t* arena, const char* str);

#endif // ARENA_H
