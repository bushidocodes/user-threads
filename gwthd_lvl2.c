#include <stdio.h>
#include <stdlib.h>
#include "gwthd.h"

static void *hello_world(void *arg)
{
    (void)arg;
    printf("Hello world from thread!\n");
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
    gwthd_join(pid);
    if (pid > -1)
        printf("Test Passed\n");
    return 0;
}
