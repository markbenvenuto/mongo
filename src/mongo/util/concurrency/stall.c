#include <stdio.h>

#ifndef _WIN32
__thread int no_stall;
__thread int idles;
__thread int end_idles;
#else
__declspec(thread) int no_stall;
__declspec(thread) int idles;
__declspec(thread) int end_idles;
#endif

// This is a C function to avoid C++'s name mangling.
void ftdc_register_no_stall() {
    printf("Thread registered as not stalling");

    no_stall++;
}

void stall_mark_start_idle() {
    idles++;
}

void stall_mark_end_idle() {
    end_idles++;
}

// This function exists so that the compiler cannot optimize out the other code in this file.
int stall_get_idle_counter() {
    return no_stall + idles + end_idles;
}
