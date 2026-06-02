#include <stdio.h>
#include <stdlib.h>
#include "gwthd.h"

static void *hello_world(void *arg)
{
    int *cast_arg = (int *)arg;
    if (*cast_arg == 42)
        printf("Test Passed\n");
    else
        printf("Test Failed\n");
    gwthd_exit();
    printf("Err: Shouldn't have gotten here!\n");
    return NULL;
}

int main(void)
{
    gwthd_t pid = -1;
    int arg = 42;
    if (gwthd_create(&pid, hello_world, &arg) == -1) {
        printf("Failed to allocate gwthd\n");
        return 1;
    }
    gwthd_join(pid);
    return 0;
}
