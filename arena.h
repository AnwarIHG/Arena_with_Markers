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

typedef struct Arena {
  uint8_t *base;       // Start of the memory block
  uint8_t *bump;       // Current allocation pointer
  uint8_t *end;        // End of the memory block
  size_t *markers;     // Dynamic array for markers (offsets from base)
  size_t marker_count; // Number of active markers
  size_t marker_cap;   // Capacity of markers array
  struct Arena *next;  // For chaining if resizable (optional)
} Arena;

// Utility to align upwards
static inline size_t align_up(size_t n, size_t align) {
  return (n + align - 1) & ~(align - 1);
}

// Helper to get the last block in the chain
static Arena* get_last_block(Arena* arena) {
    Arena* cur = arena;
    while (cur->next) {
        cur = cur->next;
    }
    return cur;
}

// Helper to compute current global position (total allocated bytes)
static size_t get_current_position(Arena* arena) {
    size_t pos = 0;
    Arena* cur = arena;
    while (cur->next) {
        pos += (size_t)(cur->end - cur->base);
        cur = cur->next;
    }
    pos += (size_t)(cur->bump - cur->base);
    return pos;
}

Arena *arena_create(size_t initial_size);
void arena_destroy(Arena *arena);

void* arena_alloc(Arena* arena, size_t bytes);

void arena_push_marker(Arena *arena);
void arena_pop_marker(Arena *arena);
void arena_reset(Arena *arena);

char* arena_strdup(Arena* arena, const char* str);


// Create and initialize the arena with a fixed size
Arena* arena_create(size_t initial_size) {
    if (initial_size == 0) initial_size = ARENA_DEFAULT_SIZE;
    Arena* arena = (Arena*)malloc(sizeof(Arena));
    if (!arena) return NULL;
    arena->base = (uint8_t*)malloc(initial_size);
    if (!arena->base) {
        free(arena);
        return NULL;
    }
    arena->bump = arena->base;
    arena->end = arena->base + initial_size;
    arena->markers = (size_t*)malloc(ARENA_INITIAL_MARKER_CAP * sizeof(size_t));
    if (!arena->markers) {
        free(arena->base);
        free(arena);
        return NULL;
    }
    arena->marker_count = 0;
    arena->marker_cap = ARENA_INITIAL_MARKER_CAP;
    arena->next = NULL;
    return arena;
}

// Destroy the arena chain and free resources
void arena_destroy(Arena* arena) {
    if (!arena) return;
    Arena* cur = arena;
    while (cur) {
        Arena* next = cur->next;
        if (cur->base) free(cur->base);
        if (cur->markers) free(cur->markers);  // Only root has markers
        free(cur);
        cur = next;
    }
}

// Allocate memory from the arena (grows via chaining if out of space)
void* arena_alloc(Arena* arena, size_t bytes) {
    if (bytes == 0) return NULL;
    bytes = align_up(bytes, ARENA_ALIGNMENT);
    Arena* last = get_last_block(arena);
    if (last->bump + bytes > last->end) {
        // Grow by chaining a new block
        size_t prev_size = (size_t)(last->end - last->base);
        size_t new_size = prev_size * 2;
        if (new_size < ARENA_DEFAULT_SIZE) new_size = ARENA_DEFAULT_SIZE;
        if (new_size < bytes) new_size = bytes;
        Arena* new_arena = (Arena*)malloc(sizeof(Arena));
        if (!new_arena) return NULL;
        new_arena->base = (uint8_t*)malloc(new_size);
        if (!new_arena->base) {
            free(new_arena);
            return NULL;
        }
        new_arena->bump = new_arena->base;
        new_arena->end = new_arena->base + new_size;
        new_arena->markers = NULL;  // Non-root has no markers
        new_arena->marker_count = 0;
        new_arena->marker_cap = 0;
        new_arena->next = NULL;
        last->next = new_arena;
        last = new_arena;
    }
    void* ptr = last->bump;
    last->bump += bytes;
    return ptr;
}

// Allocate and zero-initialize
void* arena_calloc(Arena* arena, size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = arena_alloc(arena, total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}


// Push a marker (saves current global position); operates on root
void arena_push_marker(Arena* arena) {
    if (arena->marker_count == arena->marker_cap) {
        size_t new_cap = arena->marker_cap * 2;
        size_t* new_markers = (size_t*)realloc(arena->markers, new_cap * sizeof(size_t));
        if (!new_markers) return; // Fail silently; marker not pushed
        arena->markers = new_markers;
        arena->marker_cap = new_cap;
    }
    size_t pos = get_current_position(arena);
    arena->markers[arena->marker_count++] = pos;
}


// Pop a marker (resets to last saved global position); frees later blocks if needed
void arena_pop_marker(Arena* arena) {
    if (arena->marker_count == 0) return;
    size_t g = arena->markers[--arena->marker_count];
    size_t c = 0;
    Arena* cur = arena;
    Arena* prev = NULL;
    while (cur) {
        size_t block_cap = (size_t)(cur->end - cur->base);
        if (g <= c + block_cap) {
            cur->bump = cur->base + (g - c);
            // Free all subsequent blocks
            Arena* n = cur->next;
            cur->next = NULL;
            while (n) {
                Arena* temp = n->next;
                free(n->base);
                if (n->markers) free(n->markers);  // Should be NULL for non-root
                free(n);
                n = temp;
            }
            break;
        } else {
            c += block_cap;
            prev = cur;
            cur = cur->next;
        }
    }
}

// Reset the entire arena chain (clears markers, resets to root base, frees chains)
void arena_reset(Arena* arena) {
    arena->marker_count = 0;
    // Free all chained blocks
    Arena* n = arena->next;
    arena->next = NULL;
    while (n) {
        Arena* temp = n->next;
        free(n->base);
        if (n->markers) free(n->markers);
        free(n);
        n = temp;
    }
    arena->bump = arena->base;
}

// Duplicate a string into the arena
char* arena_strdup(Arena* arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* dup = (char*)arena_alloc(arena, len);
    if (dup) memcpy(dup, str, len);
    return dup;
}

#endif // ARENA_H
