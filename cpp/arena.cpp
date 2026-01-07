#include <cstdlib>
#include <cstring>

#include "arena.h"

// Helper to align upwards
size_t Arena::align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

// Helper to get the last block in the chain
Arena* Arena::get_last_block(Arena* arena) {
    Arena* cur = arena;
    while (cur->next) {
        cur = cur->next;
    }
    return cur;
}

// Helper to compute current global position (total allocated bytes)
size_t Arena::get_current_position(Arena* arena) {
    size_t pos = 0;
    Arena* cur = arena;
    while (cur->next) {
        pos += (size_t)(cur->end - cur->base);
        cur = cur->next;
    }
    pos += (size_t)(cur->bump - cur->base);
    return pos;
}


Arena::Arena (size_t initial_size) {
    if (initial_size == 0) initial_size = ARENA_DEFAULT_SIZE;

    this->base = (uint8_t*)malloc(initial_size);
    if (!this->base) {
        exit(EXIT_FAILURE);
    }

    this->bump = this->base;
    this->end = this->base + initial_size;
    this->markers = (size_t*)malloc(ARENA_INITIAL_MARKER_CAP * sizeof(size_t));

    if (!this->markers)
        free(this->base);

    this->marker_count = 0;
    this->marker_cap = ARENA_INITIAL_MARKER_CAP;
    this->next = NULL;
}

Arena::~Arena() {
    Arena* cur = this;
    while (cur) {
        Arena* next = cur->next;
        free(cur->base);
        if (cur->markers) free(cur->markers);  // Only root has markers
        cur = next;
    }
}

// // Allocate memory from the arena (grows via chaining if out of space)
void* Arena::a_alloc(size_t bytes) {
    if (bytes == 0) return NULL;
    bytes = align_up(bytes, ARENA_ALIGNMENT);
    Arena* last = get_last_block(this);
    if (last->bump + bytes > last->end) {
        // Grow by chaining a new block
        size_t prev_size = (size_t)(last->end - last->base);
        size_t new_size = prev_size * 2;
        if (new_size < ARENA_DEFAULT_SIZE) new_size = ARENA_DEFAULT_SIZE;
        if (new_size < bytes) new_size = bytes;
        Arena* newarena = (Arena*)malloc(sizeof(Arena));
        if (!newarena) return NULL;
        newarena->base = (uint8_t*)malloc(new_size);
        if (!newarena->base) {
            free(newarena);
            return NULL;
        }
        newarena->bump = newarena->base;
        newarena->end = newarena->base + new_size;
        newarena->markers = NULL;  // Non-root has no markers
        newarena->marker_count = 0;
        newarena->marker_cap = 0;
        newarena->next = NULL;
        last->next = newarena;
        last = newarena;
    }
    void* ptr = last->bump;
    last->bump += bytes;
    return ptr;
}

// Allocate and zero-initialize
void* Arena::a_calloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = a_alloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

// Reallocate memory in the arena (requires old_size; may allocate new space and copy)
void* Arena::a_realloc(void* ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        // Like free, but in arena, we can't free individually; just return NULL
        return NULL;
    }
    if (!ptr) {
        // Like alloc
        return a_alloc(new_size);
    }
    old_size = align_up(old_size, ARENA_ALIGNMENT);
    new_size = align_up(new_size, ARENA_ALIGNMENT);

    // Find the block containing ptr
    Arena* cur = this;
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
    void* new_ptr = a_alloc(new_size);
    if (new_ptr) {
        size_t copy_size = old_size < new_size ? old_size : new_size;
        memcpy(new_ptr, ptr, copy_size);
    }
    return new_ptr;
}

// Push a marker (saves current global position); operates on root
void Arena::push_marker() {
    if (this->marker_count == this->marker_cap) {
        size_t new_cap = this->marker_cap * 2;
        size_t* new_markers = (size_t*)realloc(this->markers, new_cap * sizeof(size_t));
        if (!new_markers) return; // Fail silently; marker not pushed
        this->markers = new_markers;
        this->marker_cap = new_cap;
    }
    size_t pos = get_current_position(this);
    this->markers[this->marker_count++] = pos;
}


// Pop a marker (resets to last saved global position); frees later blocks if needed
void Arena::pop_marker() {
    if (this->marker_count == 0) return;
    size_t g = this->markers[--this->marker_count];
    size_t c = 0;
    Arena* cur = this;
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
void Arena::reset() {
    this->marker_count = 0;
    // Free all chained blocks
    Arena* n = this->next;
    this->next = NULL;
    while (n) {
        Arena* temp = n->next;
        if (n->base) free(n->base);
        if (n->markers) free(n->markers);
        free(n);
        n = temp;
    }
    this->bump = this->base;
}

// Duplicate a string into the arena
char* Arena::strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* dup = (char*)a_alloc(len);
    if (dup) memcpy(dup, str, len);
    return dup;
}
