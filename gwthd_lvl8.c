// Level 8: cooperative yielding.
// Two threads each take 5 steps. After every step each calls gwthd_yield(),
// voluntarily giving up the CPU. The round-robin scheduler interleaves them,
// producing the pattern A B A B A B A B A B instead of A A A A A B B B B B.
#include <stdio.h>
#include <stdlib.h>
#include "gwthd.h"

#define STEPS 5

static int g_order[STEPS * 2];
static int g_idx = 0;

static void *thread_a(void *arg)
{
    (void)arg;
    for (int i = 0; i < STEPS; i++) {
        g_order[g_idx++] = 'A';
        gwthd_yield();
    }
    gwthd_exit();
    return NULL;
}

static void *thread_b(void *arg)
{
    (void)arg;
    for (int i = 0; i < STEPS; i++) {
        g_order[g_idx++] = 'B';
        gwthd_yield();
    }
    gwthd_exit();
    return NULL;
}

int main(void)
{
    gwthd_t a, b;
    if (gwthd_create(&a, thread_a, NULL) == -1 ||
        gwthd_create(&b, thread_b, NULL) == -1) {
        printf("Failed to create threads\n");
        return 1;
    }
    gwthd_join(a);
    gwthd_join(b);

    printf("Execution order: ");
    for (int i = 0; i < STEPS * 2; i++)
        printf("%c", g_order[i]);
    printf("\n");

    // Both threads must have run exactly STEPS times each.
    int a_count = 0, b_count = 0;
    for (int i = 0; i < STEPS * 2; i++) {
        if (g_order[i] == 'A') a_count++;
        if (g_order[i] == 'B') b_count++;
    }

    // The output must not be all A's followed by all B's.
    int interleaved = 0;
    for (int i = 0; i < STEPS * 2 - 1; i++) {
        if (g_order[i] != g_order[i + 1]) { interleaved = 1; break; }
    }

    if (a_count == STEPS && b_count == STEPS && interleaved)
        printf("Test passed!\n");
    else
        printf("Test failed! (a=%d b=%d interleaved=%d)\n",
               a_count, b_count, interleaved);
    return 0;
}
