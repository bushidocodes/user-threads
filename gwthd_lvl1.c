#include <stdio.h>
#include <stdlib.h>
#include "gwthd.h"

static void gwthd_id_returns_thread_id(void)
{
    printf("gwthd_id_returns_thread_id\t");
    gwthd_t tid = gwthd_id();
    if (tid > 0)
        printf("PASS\n");
    else
        printf("FAIL\n");
    printf("gwthd_id returned %d\n", tid);
}

int main(void)
{
    printf("======================Level 1======================\n");
    gwthd_id_returns_thread_id();
    printf("===================================================\n");
    return 0;
}
