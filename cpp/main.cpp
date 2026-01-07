#include "arena.h"

#include <iostream>


int main(void){
    Arena arena(1024);

    char* hi = arena.strdup("hello World");
    arena.push_marker();

    std::cout << hi << "\n"; 

    return EXIT_SUCCESS;
}
