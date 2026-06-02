#include <stdio.h>
#include <stdlib.h>
#include "gwthd.h"

#define NUM_THREADS 3

static int *array_of_heap_pointers[NUM_THREADS];

static void *hello_world(void *arg)
{
    int *cast_arg = (int *)arg;
    int int_count = (*cast_arg + 1) * 42;
    int *nums = malloc(int_count * sizeof(int));
    array_of_heap_pointers[*cast_arg] = nums;
    gwthd_exit();
    printf("Err: Shouldn't have gotten here!\n");
    return NULL;
}

int main(void)
{
    gwthd_t pid[NUM_THREADS];
    int counters[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        counters[i] = i;
        if (gwthd_create(&pid[i], hello_world, &counters[i]) == -1) {
            printf("Failed to allocate gwthd\n");
            return 1;
        }
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        if (gwthd_join(pid[i]) != 0)
            printf("Failed to join thread!\n");
    }

    int had_dupes = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        for (int j = i + 1; j < NUM_THREADS; j++) {
            if (array_of_heap_pointers[i] == array_of_heap_pointers[j]) {
                had_dupes = 1;
                break;
            }
        }
    }
    if (!had_dupes)
        printf("Test passed!\n");

    for (int i = 0; i < NUM_THREADS; i++)
        free(array_of_heap_pointers[i]);

    return 0;
}
