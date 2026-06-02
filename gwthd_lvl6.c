// Level 6: error handling.
// In the kernel version, threads calling fork/exit/wait were intercepted by
// the kernel and returned -1. In this userspace library, only the gwthd_*
// functions can be guarded; OS primitives (fork, exit, wait) are not
// interceptable without LD_PRELOAD tricks.
#include <stdio.h>
#include <stdlib.h>
#include "gwthd.h"

static void *hello_world(void *arg)
{
    (void)arg;

    printf("Thread calling gwthd_create should return -1\n");
    gwthd_t i;
    if (gwthd_create(&i, hello_world, NULL) == -1)
        printf("Test Passed!\n");
    else
        printf("Test Failed!\n");

    printf("Thread calling gwthd_join should return -1\n");
    if (gwthd_join(42) == -1)
        printf("Test Passed!\n");
    else
        printf("Test Failed!\n");

    gwthd_exit();
    printf("Err: Shouldn't have gotten here!\n");
    return NULL;
}

int main(void)
{
    gwthd_t pid = -1;
    if (gwthd_create(&pid, hello_world, NULL) == -1) {
        printf("Failed to allocate gwthd\n");
        return 1;
    }
    if (gwthd_join(pid) == 0)
        printf("Successfully joined gwthd\n");
    else
        printf("Failed to join thread!\n");

    printf("Process calling gwthd_exit should log an error to stderr and return harmlessly\n");
    gwthd_exit();
    printf("A preceding error on stderr constitutes a passed test\n");
    return 0;
}
