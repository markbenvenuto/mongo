#include <stdio.h>

#ifndef _WIN32
__thread int no_stall;
#else
__declspec(thread) int no_stall;
#endif

// This is a C function to avoid C++'s name mangling.
void ftdc_register_no_stall() {
    printf("Thread registered as not stalling");

    no_stall = 1;
}