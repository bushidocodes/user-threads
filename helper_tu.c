// Second translation unit used by test_multi_tu_shared_scheduler_state.
// Calls gwthd_create() on behalf of the test so that thread creation and
// thread joining happen in different .c files.  If the static-globals bug
// were re-introduced, each TU would have its own _gw_threads and the
// cross-TU join in test_gwthd.c would return -1.
#include "gwthd.h"

static void *helper_worker(void *arg) {
    *(volatile int *)arg = 1;
    gwthd_exit();
    return NULL;
}

int helper_tu_create_thread(gwthd_t *tid, volatile int *flag) {
    return gwthd_create(tid, helper_worker, (void *)flag);
}
