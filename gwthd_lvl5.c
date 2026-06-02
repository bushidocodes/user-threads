#include <stdio.h>
#include <stdlib.h>
#include "gwthd.h"

static void *hello_world(void *arg)
{
    printf("Hello world!\n");
    int *cast_arg = (int *)arg;
    printf("Arg is %p => %d\n", arg, *cast_arg);
    for (int i = 0; i < 500; i++)
        printf("Thread is spinning to ensure join works right!\n");
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
    if (gwthd_join(pid) == 0)
        printf("Successfully joined gwthd. Test Fails if any Thread Spinning messages follow this line.\n");
    else
        printf("Failed to join thread!\n");
    return 0;
}
