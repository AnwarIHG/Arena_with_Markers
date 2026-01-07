#include "arena.h"

Arena_t *arena = {0};

// Create and initialize the arena with a fixed size
Arena_t* arena_create(size_t initial_size) {
    if (initial_size == 0) initial_size = ARENA_DEFAULT_SIZE;
    Arena_t* _arena = (Arena_t*)malloc(sizeof(Arena_t));
    if (!_arena) return NULL;
    _arena->base = (uint8_t*)malloc(initial_size);
    if (!_arena->base) {
        free(_arena);
        return NULL;
    }
    _arena->bump = _arena->base;
    _arena->end = _arena->base + initial_size;
    _arena->markers = (size_t*)malloc(ARENA_INITIAL_MARKER_CAP * sizeof(size_t));
    if (!_arena->markers) {
        free(_arena->base);
        free(_arena);
        return NULL;
    }
    _arena->marker_count = 0;
    _arena->marker_cap = ARENA_INITIAL_MARKER_CAP;
    _arena->next = NULL;
    return _arena;
}

// Destroy the arena chain and free resources
void arena_destroy(Arena_t* _arena) {
    if (!_arena) return;
    Arena_t* cur = _arena;
    while (cur) {
        Arena_t* next = cur->next;
        free(cur->base);
        if (cur->markers) free(cur->markers);  // Only root has markers
        free(cur);
        cur = next;
    }
}

// Allocate memory from the arena (grows via chaining if out of space)
void* arena_alloc(Arena_t* _arena, size_t bytes) {
    if (bytes == 0) return NULL;
    bytes = align_up(bytes, ARENA_ALIGNMENT);
    Arena_t* last = get_last_block(_arena);
    if (last->bump + bytes > last->end) {
        // Grow by chaining a new block
        size_t prev_size = (size_t)(last->end - last->base);
        size_t new_size = prev_size * 2;
        if (new_size < ARENA_DEFAULT_SIZE) new_size = ARENA_DEFAULT_SIZE;
        if (new_size < bytes) new_size = bytes;
        Arena_t* new_arena = (Arena_t*)malloc(sizeof(Arena_t));
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
void* arena_calloc(Arena_t* _arena, size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = arena_alloc(_arena, total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

// Reallocate memory in the arena (requires old_size; may allocate new space and copy)
void* arena_realloc(Arena_t* _arena, void* ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        // Like free, but in arena, we can't free individually; just return NULL
        return NULL;
    }
    if (!ptr) {
        // Like alloc
        return arena_alloc(_arena, new_size);
    }
    old_size = align_up(old_size, ARENA_ALIGNMENT);
    new_size = align_up(new_size, ARENA_ALIGNMENT);

    // Find the block containing ptr
    Arena_t* cur = _arena;
    while (cur) {
        if ((uint8_t*)ptr >= cur->base && (uint8_t*)ptr < cur->end) {
            // Check if ptr is the last allocation in this block
            if ((uint8_t*)ptr + old_size == cur->bump) {
                // It's the last one; try to resize in place
                size_t extra_needed = new_size > old_size ? new_size - old_size : 0;
                if (cur->bump + extra_needed <= cur->end) {
                    // Enough space: adjust bump
                    cur->bump = (uint8_t*)ptr + new_size;
                    return ptr;
                } else if (new_size < old_size) {
                    // Shrinking: adjust bump back
                    cur->bump = (uint8_t*)ptr + new_size;
                    return ptr;
                }
            }
            break;  // Not last or can't resize: fall to copy
        }
        cur = cur->next;
    }

    // Can't resize in place: allocate new and copy
    void* new_ptr = arena_alloc(_arena, new_size);
    if (new_ptr) {
        size_t copy_size = old_size < new_size ? old_size : new_size;
        memcpy(new_ptr, ptr, copy_size);
    }
    return new_ptr;
}


// Push a marker (saves current global position); operates on root
void arena_push_marker(Arena_t* _arena) {
    if (_arena->marker_count == _arena->marker_cap) {
        size_t new_cap = _arena->marker_cap * 2;
        size_t* new_markers = (size_t*)realloc(_arena->markers, new_cap * sizeof(size_t));
        if (!new_markers) return; // Fail silently; marker not pushed
        _arena->markers = new_markers;
        _arena->marker_cap = new_cap;
    }
    size_t pos = get_current_position(_arena);
    _arena->markers[_arena->marker_count++] = pos;
}


// Pop a marker (resets to last saved global position); frees later blocks if needed
void arena_pop_marker(Arena_t* _arena) {
    if (_arena->marker_count == 0) return;
    size_t g = _arena->markers[--_arena->marker_count];
    size_t c = 0;
    Arena_t* cur = _arena;
    Arena_t* prev = NULL;
    while (cur) {
        size_t block_cap = (size_t)(cur->end - cur->base);
        if (g <= c + block_cap) {
            cur->bump = cur->base + (g - c);
            // Free all subsequent blocks
            Arena_t* n = cur->next;
            cur->next = NULL;
            while (n) {
                Arena_t* temp = n->next;
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
void arena_reset(Arena_t* _arena) {
    _arena->marker_count = 0;
    // Free all chained blocks
    Arena_t* n = _arena->next;
    _arena->next = NULL;
    while (n) {
        Arena_t* temp = n->next;
        if (n->base) free(n->base);
        if (n->markers) free(n->markers);
        free(n);
        n = temp;
    }
    _arena->bump = _arena->base;
}

// Duplicate a string into the arena
char* arena_strdup(Arena_t* _arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* dup = (char*)arena_alloc(_arena, len);
    if (dup) memcpy(dup, str, len);
    return dup;
}
