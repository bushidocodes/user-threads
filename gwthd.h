// N:1 cooperative green-thread library using Windows Fibers.
// Same API as the xv6 kernel gwthd implementation; all threads run on one
// OS thread and switch cooperatively via gwthd_exit / gwthd_join.
//
// Include this header in any translation unit that uses the API.
// Compile and link gwthd.c exactly once per binary.
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
int gwthd_create(gwthd_t *childid, gwthd_fn_t fn, void *arg);

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
void gwthd_exit(void);

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
int gwthd_join(gwthd_t child);

/**
 * Returns the unique id of the calling thread.
 *
 * Safe to call from both the main context and child threads. The main context
 * always returns 1; child threads return the id set by gwthd_create.
 *
 * @return  The gwthd_t id of the currently executing thread.
 */
gwthd_t gwthd_id(void);

/**
 * Voluntarily relinquishes the CPU without exiting.
 *
 * Moves the calling thread back to GW_RUNNABLE and switches to the scheduler.
 * The round-robin scheduler will resume this thread only after every other
 * currently runnable thread has had at least one turn, preventing starvation.
 * Safe to call from both the main context and child threads.
 */
void gwthd_yield(void);
