// N:1 cooperative green-thread library using Windows Fibers.
// Same API as the xv6 kernel gwthd implementation; all threads run on one
// OS thread and switch cooperatively via gwthd_exit / gwthd_join.
#pragma once

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_THREADS 64

typedef int gwthd_t;
typedef void *(*gwthd_fn_t)(void *);

typedef enum { GW_RUNNABLE, GW_RUNNING, GW_ZOMBIE, GW_WAITING } gw_state_t;

typedef struct {
    gwthd_t    id;
    gw_state_t state;
    LPVOID     fiber;
    gwthd_fn_t fn;
    void      *arg;
    gwthd_t    waiting_for;
} gw_thread_t;

static gw_thread_t _gw_threads[MAX_THREADS];
static int         _gw_count       = 0;
static int         _gw_current     = 0;
static int         _gw_next_id     = 2; // 1 = main; children increment from 2
static LPVOID      _gw_sched_fiber = NULL;
static int         _gw_initialized = 0;

static void WINAPI _gw_scheduler_fiber(LPVOID param);
static inline void gwthd_exit(void);
static inline void gwthd_yield(void);

static void WINAPI _gw_thread_entry(LPVOID param) {
    (void)param;
    // _gw_current is set by the scheduler before switching to this fiber.
    gw_thread_t *t = &_gw_threads[_gw_current];
    t->fn(t->arg);
    gwthd_exit();
}

static void _gw_init(void) {
    if (_gw_initialized) return;
    _gw_initialized = 1;
    _gw_threads[0].id    = 1;
    _gw_threads[0].state = GW_RUNNING;
    _gw_threads[0].fiber = ConvertThreadToFiber(NULL);
    _gw_count   = 1;
    _gw_current = 0;
    _gw_sched_fiber = CreateFiber(4096 * 4, _gw_scheduler_fiber, NULL);
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

/**
 * Creates a new green thread that executes fn(arg).
 *
 * The thread is added to the run queue immediately but does not start running
 * until the caller yields control (via gwthd_join or gwthd_yield). Only the
 * main context may create threads; calling this from inside a thread returns -1.
 *
 * @param childid  Output — set to the new thread's gwthd_t id on success.
 * @param fn       Function the thread will execute. Must call gwthd_exit()
 *                 rather than returning; returning without calling gwthd_exit()
 *                 is handled gracefully but is not the intended usage.
 * @param arg      Opaque pointer forwarded to fn as its sole argument.
 * @return         0 on success; -1 if called from a thread, if the thread
 *                 table is full (MAX_THREADS), or if fiber creation fails.
 */
static inline int gwthd_create(gwthd_t *childid, gwthd_fn_t fn, void *arg) {
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
    t->id          = _gw_next_id++;
    t->state       = GW_RUNNABLE;
    t->fn          = fn;
    t->arg         = arg;
    t->waiting_for = 0;
    t->fiber       = CreateFiber(4096 * 4, _gw_thread_entry, NULL);
    if (!t->fiber) { t->state = GW_ZOMBIE; return -1; }
    *childid = t->id;
    return 0;
}

/**
 * Terminates the calling thread.
 *
 * Marks the thread as ZOMBIE, wakes any parent blocked in gwthd_join waiting
 * on this thread, then yields to the scheduler. The fiber's resources are
 * freed by the parent's subsequent gwthd_join call — not here — because a
 * fiber cannot delete its own stack while still running on it.
 *
 * If called from the main context (not a thread), logs a diagnostic to stderr
 * and returns harmlessly; the process is not affected.
 */
static inline void gwthd_exit(void) {
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

/**
 * Blocks until the child thread has exited, then frees its resources.
 *
 * If the child has already called gwthd_exit() by the time join is called,
 * this returns immediately without yielding. Otherwise the caller is marked
 * GW_WAITING and the scheduler runs other threads until the child finishes.
 * DeleteFiber is called here (not in gwthd_exit) to respect the invariant
 * that a fiber must not free its own stack.
 *
 * @param child  The gwthd_t id returned by gwthd_create for the target thread.
 * @return       0 on success; -1 if called from a thread or if child is not
 *               a valid thread id.
 */
static inline int gwthd_join(gwthd_t child) {
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
    _gw_threads[0].state       = GW_RUNNING;
    _gw_threads[0].waiting_for = 0;
    return 0;
}

/**
 * Returns the unique id of the calling thread.
 *
 * Safe to call from both the main context and child threads. The main context
 * always returns 1; child threads return the id set by gwthd_create.
 *
 * @return  The gwthd_t id of the currently executing thread.
 */
static inline gwthd_t gwthd_id(void) {
    _gw_init();
    return _gw_threads[_gw_current].id;
}

/**
 * Voluntarily relinquishes the CPU without exiting.
 *
 * Moves the calling thread back to GW_RUNNABLE and switches to the scheduler.
 * The round-robin scheduler will resume this thread only after every other
 * currently runnable thread has had at least one turn, preventing starvation.
 * Safe to call from both the main context and child threads.
 */
static inline void gwthd_yield(void) {
    _gw_init();
    gw_thread_t *t = &_gw_threads[_gw_current];
    t->state = GW_RUNNABLE;          // stay in the queue; give up the CPU
    SwitchToFiber(_gw_sched_fiber);  // resumes here when the scheduler picks us again
    t->state = GW_RUNNING;
}
