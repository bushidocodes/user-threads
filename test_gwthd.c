/* test_gwthd.c — comprehensive unit tests for gwthd.h using Unity */
#include "unity.h"
#include "gwthd.h"

/* helper_tu.c — separate TU used by test_multi_tu_shared_scheduler_state */
extern int helper_tu_create_thread(gwthd_t *tid, volatile int *flag);

/* ─────────────────────────────────────────────────────────────────────
   Thread helper functions and shared globals.
   All globals are reset in setUp() before every test.
   ───────────────────────────────────────────────────────────────────── */

/* exits immediately */
static void *thread_exit_fn(void *arg)      { (void)arg; gwthd_exit(); return NULL; }

/* sets g_ran = 1 then exits */
static volatile int g_ran;
static void *thread_set_ran(void *arg)      { (void)arg; g_ran = 1; gwthd_exit(); return NULL; }

/* captures gwthd_id() into g_captured_id */
static gwthd_t g_captured_id;
static void *thread_capture_id(void *arg)   { (void)arg; g_captured_id = gwthd_id(); gwthd_exit(); return NULL; }

/* multi-thread id capture: g_ids[*(int*)arg] = gwthd_id() */
#define MULTI_N 5
static gwthd_t g_ids[MULTI_N];
static int     g_idx_arg[MULTI_N];
static void *thread_capture_id_indexed(void *arg) {
    g_ids[*(int *)arg] = gwthd_id();
    gwthd_exit();
    return NULL;
}

/* increments g_counter */
static volatile int g_counter;
static void *thread_increment(void *arg)    { (void)arg; g_counter++; gwthd_exit(); return NULL; }

/* spins 100 iterations then sets g_spin_done */
static volatile int g_spin_done;
static void *thread_spin(void *arg) {
    (void)arg;
    for (int i = 0; i < 100; i++) { /* burn cycles */ }
    g_spin_done = 1;
    gwthd_exit();
    return NULL;
}

/* argument passing — int */
static int g_int_result;
static void *thread_recv_int(void *arg)     { g_int_result = *(int *)arg; gwthd_exit(); return NULL; }

/* argument passing — struct pointer */
typedef struct { int x; int y; } pt_t;
static pt_t g_pt;
static int  g_pt_x, g_pt_y;
static void *thread_recv_pt(void *arg) {
    pt_t *p = (pt_t *)arg;
    g_pt_x = p->x;
    g_pt_y = p->y;
    gwthd_exit();
    return NULL;
}

/* argument passing — null */
static void *g_null_check;
static void *thread_recv_null(void *arg)    { g_null_check = arg; gwthd_exit(); return NULL; }

/* shared heap: g_heap_ptrs[*(int*)arg] = malloc(...) */
#define HEAP_N 4
static int *g_heap_ptrs[HEAP_N];
static int  g_heap_idx[HEAP_N];
static void *thread_heap_alloc(void *arg) {
    int i = *(int *)arg;
    g_heap_ptrs[i] = malloc((i + 1) * sizeof(int));
    gwthd_exit();
    return NULL;
}

/* error handling — tries gwthd_create from inside a thread */
static int g_create_from_thread_rc;
static void *thread_try_create(void *arg) {
    (void)arg;
    gwthd_t dummy;
    g_create_from_thread_rc = gwthd_create(&dummy, thread_exit_fn, NULL);
    gwthd_exit();
    return NULL;
}

/* error handling — tries gwthd_join from inside a thread */
static int g_join_from_thread_rc;
static void *thread_try_join(void *arg) {
    (void)arg;
    g_join_from_thread_rc = gwthd_join(9999);
    gwthd_exit();
    return NULL;
}

/* yield: thread A writes 'A' then yields, YIELD_STEPS times */
#define YIELD_STEPS 5
static int g_order[YIELD_STEPS * 2];
static int g_order_idx;
static void *thread_yield_a(void *arg) {
    (void)arg;
    for (int i = 0; i < YIELD_STEPS; i++) { g_order[g_order_idx++] = 'A'; gwthd_yield(); }
    gwthd_exit();
    return NULL;
}
static void *thread_yield_b(void *arg) {
    (void)arg;
    for (int i = 0; i < YIELD_STEPS; i++) { g_order[g_order_idx++] = 'B'; gwthd_yield(); }
    gwthd_exit();
    return NULL;
}
static void *thread_just_yield(void *arg)   { (void)arg; gwthd_yield(); gwthd_exit(); return NULL; }

/* ─────────────────────────────────────────────────────────────────────
   setUp / tearDown
   ───────────────────────────────────────────────────────────────────── */

void setUp(void) {
    g_ran             = 0;
    g_captured_id     = 0;
    g_int_result      = 0;
    g_pt_x            = g_pt_y = 0;
    g_null_check      = (void *)1;   /* non-NULL sentinel; thread must clear it */
    g_counter         = 0;
    g_spin_done       = 0;
    g_create_from_thread_rc = 0;
    g_join_from_thread_rc   = 0;
    g_order_idx       = 0;
    for (int i = 0; i < MULTI_N; i++) { g_ids[i] = 0; g_idx_arg[i] = i; }
    for (int i = 0; i < HEAP_N;  i++) { g_heap_ptrs[i] = NULL; g_heap_idx[i] = i; }
}

void tearDown(void) {}

/* ═══════════════════════════════════════════════════════════════════════
   gwthd_id
   ═══════════════════════════════════════════════════════════════════════ */

void test_id_main_is_positive(void) {
    TEST_ASSERT_GREATER_THAN(0, gwthd_id());
}

void test_id_main_is_one(void) {
    TEST_ASSERT_EQUAL(1, gwthd_id());
}

void test_id_main_is_stable_across_calls(void) {
    TEST_ASSERT_EQUAL(gwthd_id(), gwthd_id());
}

void test_id_child_is_positive(void) {
    gwthd_t t;
    gwthd_create(&t, thread_capture_id, NULL);
    gwthd_join(t);
    TEST_ASSERT_GREATER_THAN(0, g_captured_id);
}

void test_id_child_differs_from_main(void) {
    gwthd_t t;
    gwthd_create(&t, thread_capture_id, NULL);
    gwthd_join(t);
    TEST_ASSERT_NOT_EQUAL(gwthd_id(), g_captured_id);
}

void test_id_multiple_children_all_unique(void) {
    gwthd_t pids[MULTI_N];
    for (int i = 0; i < MULTI_N; i++)
        TEST_ASSERT_EQUAL(0, gwthd_create(&pids[i], thread_capture_id_indexed, &g_idx_arg[i]));
    for (int i = 0; i < MULTI_N; i++)
        TEST_ASSERT_EQUAL(0, gwthd_join(pids[i]));
    for (int i = 0; i < MULTI_N; i++) {
        TEST_ASSERT_GREATER_THAN(0, g_ids[i]);
        for (int j = i + 1; j < MULTI_N; j++)
            TEST_ASSERT_NOT_EQUAL(g_ids[i], g_ids[j]);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   gwthd_create
   ═══════════════════════════════════════════════════════════════════════ */

void test_create_returns_zero_on_success(void) {
    gwthd_t t;
    TEST_ASSERT_EQUAL(0, gwthd_create(&t, thread_exit_fn, NULL));
    gwthd_join(t);
}

void test_create_sets_positive_childid(void) {
    gwthd_t t = -1;
    gwthd_create(&t, thread_exit_fn, NULL);
    TEST_ASSERT_GREATER_THAN(0, t);
    gwthd_join(t);
}

void test_create_with_null_arg_does_not_crash(void) {
    gwthd_t t;
    TEST_ASSERT_EQUAL(0, gwthd_create(&t, thread_exit_fn, NULL));
    TEST_ASSERT_EQUAL(0, gwthd_join(t));
}

void test_create_multiple_sequential(void) {
    /* slot reuse must keep this from exhausting MAX_THREADS */
    for (int i = 0; i < 10; i++) {
        gwthd_t t;
        TEST_ASSERT_EQUAL(0, gwthd_create(&t, thread_exit_fn, NULL));
        TEST_ASSERT_EQUAL(0, gwthd_join(t));
    }
}

void test_create_multiple_parallel_all_run(void) {
    gwthd_t pids[MULTI_N];
    for (int i = 0; i < MULTI_N; i++)
        TEST_ASSERT_EQUAL(0, gwthd_create(&pids[i], thread_increment, NULL));
    for (int i = 0; i < MULTI_N; i++)
        TEST_ASSERT_EQUAL(0, gwthd_join(pids[i]));
    TEST_ASSERT_EQUAL(MULTI_N, g_counter);
}

void test_create_from_thread_returns_minus1(void) {
    gwthd_t t;
    TEST_ASSERT_EQUAL(0, gwthd_create(&t, thread_try_create, NULL));
    TEST_ASSERT_EQUAL(0, gwthd_join(t));
    TEST_ASSERT_EQUAL(-1, g_create_from_thread_rc);
}

/* ═══════════════════════════════════════════════════════════════════════
   gwthd_exit / gwthd_join
   ═══════════════════════════════════════════════════════════════════════ */

void test_join_returns_zero_on_success(void) {
    gwthd_t t;
    gwthd_create(&t, thread_exit_fn, NULL);
    TEST_ASSERT_EQUAL(0, gwthd_join(t));
}

void test_join_thread_actually_ran(void) {
    gwthd_t t;
    gwthd_create(&t, thread_set_ran, NULL);
    gwthd_join(t);
    TEST_ASSERT_EQUAL(1, g_ran);
}

void test_join_invalid_id_returns_minus1(void) {
    TEST_ASSERT_EQUAL(-1, gwthd_join(9999));
}

void test_join_from_thread_returns_minus1(void) {
    gwthd_t t;
    TEST_ASSERT_EQUAL(0, gwthd_create(&t, thread_try_join, NULL));
    TEST_ASSERT_EQUAL(0, gwthd_join(t));
    TEST_ASSERT_EQUAL(-1, g_join_from_thread_rc);
}

void test_join_blocks_until_thread_completes(void) {
    /* cooperative scheduler runs child to completion before returning to main */
    gwthd_t t;
    gwthd_create(&t, thread_spin, NULL);
    TEST_ASSERT_EQUAL(0, gwthd_join(t));
    TEST_ASSERT_EQUAL(1, g_spin_done);
}

void test_join_after_thread_already_exited(void) {
    /* yield lets the thread run and become ZOMBIE; join should not block */
    gwthd_t t;
    gwthd_create(&t, thread_set_ran, NULL);
    gwthd_yield();
    TEST_ASSERT_EQUAL(1, g_ran);       /* thread finished during yield */
    TEST_ASSERT_EQUAL(0, gwthd_join(t)); /* returns immediately */
}

/* ═══════════════════════════════════════════════════════════════════════
   argument passing
   ═══════════════════════════════════════════════════════════════════════ */

void test_arg_int_passed_correctly(void) {
    gwthd_t t;
    int val = 42;
    gwthd_create(&t, thread_recv_int, &val);
    gwthd_join(t);
    TEST_ASSERT_EQUAL(42, g_int_result);
}

void test_arg_struct_pointer_passed_correctly(void) {
    gwthd_t t;
    g_pt.x = 100; g_pt.y = 200;
    gwthd_create(&t, thread_recv_pt, &g_pt);
    gwthd_join(t);
    TEST_ASSERT_EQUAL(100, g_pt_x);
    TEST_ASSERT_EQUAL(200, g_pt_y);
}

void test_arg_null_arrives_as_null_in_thread(void) {
    gwthd_t t;
    gwthd_create(&t, thread_recv_null, NULL);
    gwthd_join(t);
    TEST_ASSERT_NULL(g_null_check);
}

/* ═══════════════════════════════════════════════════════════════════════
   shared heap
   ═══════════════════════════════════════════════════════════════════════ */

void test_heap_alloc_in_thread_visible_to_parent(void) {
    gwthd_t t;
    gwthd_create(&t, thread_heap_alloc, &g_heap_idx[0]);
    gwthd_join(t);
    TEST_ASSERT_NOT_NULL(g_heap_ptrs[0]);
    free(g_heap_ptrs[0]);
}

void test_heap_multiple_threads_produce_unique_allocations(void) {
    gwthd_t pids[HEAP_N];
    for (int i = 0; i < HEAP_N; i++)
        TEST_ASSERT_EQUAL(0, gwthd_create(&pids[i], thread_heap_alloc, &g_heap_idx[i]));
    for (int i = 0; i < HEAP_N; i++)
        TEST_ASSERT_EQUAL(0, gwthd_join(pids[i]));
    for (int i = 0; i < HEAP_N; i++) {
        TEST_ASSERT_NOT_NULL(g_heap_ptrs[i]);
        for (int j = i + 1; j < HEAP_N; j++)
            TEST_ASSERT_NOT_EQUAL(g_heap_ptrs[i], g_heap_ptrs[j]);
        free(g_heap_ptrs[i]);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   synchronization / multiple threads
   ═══════════════════════════════════════════════════════════════════════ */

void test_all_threads_run_before_join_returns(void) {
    gwthd_t pids[MULTI_N];
    for (int i = 0; i < MULTI_N; i++)
        TEST_ASSERT_EQUAL(0, gwthd_create(&pids[i], thread_increment, NULL));
    for (int i = 0; i < MULTI_N; i++)
        TEST_ASSERT_EQUAL(0, gwthd_join(pids[i]));
    TEST_ASSERT_EQUAL(MULTI_N, g_counter);
}

void test_join_in_reverse_creation_order(void) {
    gwthd_t pids[3];
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_EQUAL(0, gwthd_create(&pids[i], thread_increment, NULL));
    for (int i = 2; i >= 0; i--)
        TEST_ASSERT_EQUAL(0, gwthd_join(pids[i]));
    TEST_ASSERT_EQUAL(3, g_counter);
}

/* ═══════════════════════════════════════════════════════════════════════
   error handling
   ═══════════════════════════════════════════════════════════════════════ */

void test_exit_from_main_is_harmless(void) {
    /* prints a diagnostic to stderr but must not crash or terminate */
    gwthd_exit();
    TEST_PASS();
}

/* ═══════════════════════════════════════════════════════════════════════
   gwthd_yield
   ═══════════════════════════════════════════════════════════════════════ */

void test_yield_from_main_does_not_crash(void) {
    gwthd_yield();
    TEST_PASS();
}

void test_yield_from_thread_does_not_crash(void) {
    gwthd_t t;
    TEST_ASSERT_EQUAL(0, gwthd_create(&t, thread_just_yield, NULL));
    TEST_ASSERT_EQUAL(0, gwthd_join(t));
}

void test_yield_interleaves_two_threads(void) {
    gwthd_t a, b;
    TEST_ASSERT_EQUAL(0, gwthd_create(&a, thread_yield_a, NULL));
    TEST_ASSERT_EQUAL(0, gwthd_create(&b, thread_yield_b, NULL));
    TEST_ASSERT_EQUAL(0, gwthd_join(a));
    TEST_ASSERT_EQUAL(0, gwthd_join(b));

    int cnt_a = 0, cnt_b = 0;
    for (int i = 0; i < YIELD_STEPS * 2; i++) {
        if (g_order[i] == 'A') cnt_a++;
        if (g_order[i] == 'B') cnt_b++;
    }
    TEST_ASSERT_EQUAL(YIELD_STEPS, cnt_a);
    TEST_ASSERT_EQUAL(YIELD_STEPS, cnt_b);

    /* must not be AAAA…BBBB…; at least one adjacent pair must differ */
    int saw_interleave = 0;
    for (int i = 0; i < YIELD_STEPS * 2 - 1; i++) {
        if (g_order[i] != g_order[i + 1]) { saw_interleave = 1; break; }
    }
    TEST_ASSERT_TRUE(saw_interleave);
}

void test_yield_both_threads_complete_all_steps(void) {
    gwthd_t a, b;
    TEST_ASSERT_EQUAL(0, gwthd_create(&a, thread_yield_a, NULL));
    TEST_ASSERT_EQUAL(0, gwthd_create(&b, thread_yield_b, NULL));
    gwthd_join(a);
    gwthd_join(b);
    TEST_ASSERT_EQUAL(YIELD_STEPS * 2, g_order_idx);
}

/* ═══════════════════════════════════════════════════════════════════════
   multi-TU shared state
   Regression guard for the static-globals bug: if scheduler state were
   declared static in the header, each TU would get its own private copy
   and a cross-TU join would return -1 (thread not found).
   ═══════════════════════════════════════════════════════════════════════ */

void test_multi_tu_thread_created_in_helper_tu_joinable_here(void) {
    volatile int flag = 0;
    gwthd_t tid;
    TEST_ASSERT_EQUAL_MESSAGE(0, helper_tu_create_thread(&tid, &flag),
        "helper_tu_create_thread failed");
    /* Cross-TU join: succeeds only if both TUs share the same _gw_threads */
    TEST_ASSERT_EQUAL_MESSAGE(0, gwthd_join(tid),
        "gwthd_join returned -1: scheduler state is not shared across TUs");
    TEST_ASSERT_EQUAL(1, flag);
}

/* ═══════════════════════════════════════════════════════════════════════
   resource cleanup
   ═══════════════════════════════════════════════════════════════════════ */

void test_200_sequential_create_join_cycles(void) {
    /* if DeleteFiber is never called, Windows hits its fiber/handle limit
       long before 200 iterations; the test also verifies slot reuse. */
    for (int i = 0; i < 200; i++) {
        gwthd_t t;
        TEST_ASSERT_EQUAL_MESSAGE(0, gwthd_create(&t, thread_increment, NULL),
                                  "gwthd_create failed — handle or slot exhaustion?");
        TEST_ASSERT_EQUAL(0, gwthd_join(t));
    }
    TEST_ASSERT_EQUAL(200, g_counter);
}

/* ═══════════════════════════════════════════════════════════════════════
   main
   ═══════════════════════════════════════════════════════════════════════ */

int main(void) {
    UNITY_BEGIN();

    /* gwthd_id */
    RUN_TEST(test_id_main_is_positive);
    RUN_TEST(test_id_main_is_one);
    RUN_TEST(test_id_main_is_stable_across_calls);
    RUN_TEST(test_id_child_is_positive);
    RUN_TEST(test_id_child_differs_from_main);
    RUN_TEST(test_id_multiple_children_all_unique);

    /* gwthd_create */
    RUN_TEST(test_create_returns_zero_on_success);
    RUN_TEST(test_create_sets_positive_childid);
    RUN_TEST(test_create_with_null_arg_does_not_crash);
    RUN_TEST(test_create_multiple_sequential);
    RUN_TEST(test_create_multiple_parallel_all_run);
    RUN_TEST(test_create_from_thread_returns_minus1);

    /* gwthd_exit / gwthd_join */
    RUN_TEST(test_join_returns_zero_on_success);
    RUN_TEST(test_join_thread_actually_ran);
    RUN_TEST(test_join_invalid_id_returns_minus1);
    RUN_TEST(test_join_from_thread_returns_minus1);
    RUN_TEST(test_join_blocks_until_thread_completes);
    RUN_TEST(test_join_after_thread_already_exited);

    /* argument passing */
    RUN_TEST(test_arg_int_passed_correctly);
    RUN_TEST(test_arg_struct_pointer_passed_correctly);
    RUN_TEST(test_arg_null_arrives_as_null_in_thread);

    /* shared heap */
    RUN_TEST(test_heap_alloc_in_thread_visible_to_parent);
    RUN_TEST(test_heap_multiple_threads_produce_unique_allocations);

    /* synchronization / multiple threads */
    RUN_TEST(test_all_threads_run_before_join_returns);
    RUN_TEST(test_join_in_reverse_creation_order);

    /* error handling */
    RUN_TEST(test_exit_from_main_is_harmless);

    /* gwthd_yield */
    RUN_TEST(test_yield_from_main_does_not_crash);
    RUN_TEST(test_yield_from_thread_does_not_crash);
    RUN_TEST(test_yield_interleaves_two_threads);
    RUN_TEST(test_yield_both_threads_complete_all_steps);

    /* multi-TU shared state */
    RUN_TEST(test_multi_tu_thread_created_in_helper_tu_joinable_here);

    /* resource cleanup */
    RUN_TEST(test_200_sequential_create_join_cycles);

    return UNITY_END();
}
