// N:1 cooperative green-thread library — implementation.
// Single definition point for all scheduler state and function bodies.
#include "gwthd.h"

gw_thread_t  _gw_threads[MAX_THREADS];
int          _gw_count       = 0;
int          _gw_current     = 0;
unsigned int _gw_next_id     = 2; // 1 = main; children increment from 2
LPVOID       _gw_sched_fiber = NULL;
int          _gw_initialized = 0;

static void WINAPI _gw_scheduler_fiber(LPVOID param);

static void WINAPI _gw_thread_entry(LPVOID param) {
    (void)param;
    // _gw_current is set by the scheduler before switching to this fiber.
    gw_thread_t *t = &_gw_threads[_gw_current];
    t->fn(t->arg);
    gwthd_exit();
}

static void _gw_init(void) {
    if (_gw_initialized) return;
    // Initialize before touching any state so a failed init can be retried.
    _gw_threads[0].fiber = ConvertThreadToFiber(NULL);
    if (!_gw_threads[0].fiber) {
        fprintf(stderr, "gwthd: ConvertThreadToFiber failed (%lu)\n", GetLastError());
        abort();
    }
    _gw_sched_fiber = CreateFiber(4096 * 4, _gw_scheduler_fiber, NULL);
    if (!_gw_sched_fiber) {
        fprintf(stderr, "gwthd: CreateFiber failed (%lu)\n", GetLastError());
        abort();
    }
    _gw_threads[0].id    = 1;
    _gw_threads[0].state = GW_RUNNING;
    _gw_count   = 1;
    _gw_current = 0;
    _gw_initialized = 1; // set last so a failed init leaves the library retryable
}

static int _gw_find_idx(gwthd_t id) {
    for (int i = 0; i < _gw_count; i++)
        if (_gw_threads[i].id == id) return i;
    return -1;
}

// Round-robin scheduler: after each context switch, resumes scanning from
// the slot *after* the thread that just ran so every runnable thread gets a
// turn before the same one is selected twice.
static void WINAPI _gw_scheduler_fiber(LPVOID param) {
    (void)param;
    for (;;) {
        int found = 0;
        for (int j = 1; j <= _gw_count; j++) {
            int i = (_gw_current + j) % _gw_count;
            if (_gw_threads[i].state == GW_RUNNABLE) {
                _gw_current = i;
                _gw_threads[i].state = GW_RUNNING;
                SwitchToFiber(_gw_threads[i].fiber);
                // Resumes here after any thread yields back to the scheduler.
                found = 1;
                break; // restart scan from new _gw_current
            }
        }
        if (!found) {
            // Nothing runnable — all children done, main already RUNNABLE.
            break;
        }
    }
}

int gwthd_create(gwthd_t *childid, gwthd_fn_t fn, void *arg) {
    _gw_init();
    if (_gw_current != 0) return -1; // threads cannot create threads
    // Reuse a ZOMBIE slot whose fiber was already deleted by gwthd_join.
    int idx = -1;
    for (int i = 1; i < _gw_count; i++) {
        if (_gw_threads[i].state == GW_ZOMBIE && _gw_threads[i].fiber == NULL) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        if (_gw_count >= MAX_THREADS) return -1;
        idx = _gw_count++;
    }
    gw_thread_t *t = &_gw_threads[idx];
    t->id          = (gwthd_t)_gw_next_id;
    if (++_gw_next_id < 2) _gw_next_id = 2; // skip ids 0 and 1 after unsigned wrap
    t->state       = GW_RUNNABLE;
    t->fn          = fn;
    t->arg         = arg;
    t->waiting_for = 0;
    t->fiber       = CreateFiber(4096 * 4, _gw_thread_entry, NULL);
    if (!t->fiber) { t->state = GW_ZOMBIE; return -1; }
    *childid = t->id;
    return 0;
}

void gwthd_exit(void) {
    _gw_init();
    if (_gw_current == 0) {
        // The main process called gwthd_exit — harmless, just log and return.
        fprintf(stderr, "Err: Process called gwthd_exit\n");
        return;
    }
    gw_thread_t *t = &_gw_threads[_gw_current];
    t->state = GW_ZOMBIE;
    // Wake any parent blocked in gwthd_join on this thread.
    for (int i = 0; i < _gw_count; i++) {
        if (_gw_threads[i].state == GW_WAITING &&
            _gw_threads[i].waiting_for == t->id)
            _gw_threads[i].state = GW_RUNNABLE;
    }
    SwitchToFiber(_gw_sched_fiber);
}

int gwthd_join(gwthd_t child) {
    _gw_init();
    if (_gw_current != 0) return -1; // threads cannot join
    int cidx = _gw_find_idx(child);
    if (cidx <= 0) return -1; // 0 = main; joining main would deadlock
    if (_gw_threads[cidx].state != GW_ZOMBIE) {
        _gw_threads[0].state       = GW_WAITING;
        _gw_threads[0].waiting_for = child;
        SwitchToFiber(_gw_sched_fiber);
        // Resumes here once the child calls gwthd_exit and marks us RUNNABLE.
    }
    if (_gw_threads[cidx].fiber) {
        DeleteFiber(_gw_threads[cidx].fiber);
        _gw_threads[cidx].fiber = NULL;
    }
    _gw_threads[cidx].id    = 0;
    _gw_threads[cidx].state = GW_ZOMBIE;
    _gw_threads[0].state       = GW_RUNNING;
    _gw_threads[0].waiting_for = 0;
    return 0;
}

gwthd_t gwthd_id(void) {
    _gw_init();
    return _gw_threads[_gw_current].id;
}

void gwthd_yield(void) {
    _gw_init();
    gw_thread_t *t = &_gw_threads[_gw_current];
    t->state = GW_RUNNABLE;          // stay in the queue; give up the CPU
    SwitchToFiber(_gw_sched_fiber);  // resumes here when the scheduler picks us again
    t->state = GW_RUNNING;
}
