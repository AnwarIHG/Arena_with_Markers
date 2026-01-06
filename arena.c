#include <stdio.h>
#include <stdlib.h>

#include "arena.h"

int main(void) {
    Arena *arena = arena_create(1024);
    arena_push_marker(arena);

    char *str = arena_strdup(arena, "Hello World\n");
    arena_push_marker(arena);

    char *str2 = arena_strdup(arena, "are you all right\n");

    printf("%s",str);

    arena_destroy(arena);
    return EXIT_SUCCESS;
}
