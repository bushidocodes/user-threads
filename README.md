# user-threads

A userspace port of the `gwthd` threading library from [hw4-19-threading](https://github.com/bushidocodes/hw4-19-threading-bushidocodes), which implemented 1:1 POSIX-style threads inside the xv6 teaching kernel.

This repo keeps the same API and test suite but replaces the kernel syscalls with **Windows Fibers**, giving an N:1 cooperative green-thread scheduler that runs entirely in userspace.

## API

```c
typedef int     gwthd_t;
typedef void *(*gwthd_fn_t)(void *);

int     gwthd_create(gwthd_t *childid, gwthd_fn_t fn, void *arg);
void    gwthd_exit(void);
int     gwthd_join(gwthd_t child);
gwthd_t gwthd_id(void);
void    gwthd_yield(void);   // cooperative yield — give up the CPU, stay runnable
```

`gwthd_create/exit/join/id` are identical to the xv6 version (a simplified subset of pthreads). `gwthd_yield` is new: it returns the current thread to the run queue and lets the scheduler pick the next one. Without it, a running thread holds the CPU until it calls `gwthd_exit`.

## Building

Requires MinGW GCC on Windows.

```
make        # builds gwthd_lvl0 … gwthd_lvl7
make run    # builds and runs all levels
make clean
```

## Tests

Each level tests an additional capability, mirroring the original assignment:

| Level | Tests |
|-------|-------|
| 0 | Compiles |
| 1 | `gwthd_id` returns a positive id |
| 2 | `gwthd_create` + `gwthd_join` round-trip |
| 3 | Argument passing through `gwthd_create` |
| 4 | Threads share the heap (`malloc` in a thread is visible to the parent) |
| 5 | `gwthd_join` blocks until the child fully completes |
| 6 | Error handling: threads can't call `gwthd_create`/`gwthd_join`; the main process calling `gwthd_exit` is a no-op |
| 7 | Stack cleanup: 200 sequential create/join cycles complete without handle exhaustion, proving `DeleteFiber` is called on every join |
| 8 | Cooperative yield: two threads each take 5 steps calling `gwthd_yield` between each, producing interleaved output `ABABABABAB` |

## Architecture

### xv6 kernel version (1:1 threads)

```
gwthd_create  →  mkthrd syscall  →  allocproc() + share pgdir
gwthd_exit    →  exitthrd syscall  →  sleep/wakeup1
gwthd_join    →  jointhrd syscall  →  wait loop + kfree(kstack)
```

Each thread was a real kernel `proc` scheduled by the xv6 round-robin scheduler. The parent passed a `malloc`'d stack into the kernel; the kernel stored it and the parent `free`'d it after `jointhrd` returned the pointer.

### This version (N:1 green threads)

```
gwthd_create  →  CreateFiber
gwthd_exit    →  mark ZOMBIE, wake waiter, SwitchToFiber(scheduler)
gwthd_join    →  SwitchToFiber(scheduler) if needed, then DeleteFiber
```

All threads run on a single OS thread. A dedicated **scheduler fiber** (`_gw_scheduler_fiber`) acts as the dispatch loop: after any thread yields, the scheduler restarts its scan of the thread table and switches to the next `GW_RUNNABLE` entry. The same join-frees-the-stack ownership rule applies — `DeleteFiber` is called from `gwthd_join`, never from `gwthd_exit`, because you can't free the stack you're currently running on.

### Key differences

| Property | xv6 kernel | user-threads |
|----------|-----------|--------------|
| Threading model | 1:1 (kernel-scheduled) | N:1 (cooperative) |
| Context switch cost | Syscall + trap | `SwitchToFiber` (userspace) |
| Parallelism | Yes (on SMP xv6) | No (single OS thread) |
| Stack management | `malloc` in user, `kfree` in kernel | `CreateFiber` / `DeleteFiber` |
| Preemption | Timer interrupt | None — threads must call `gwthd_exit` |
| `fork`/`exit` from thread | Kernel returns −1 | Not interceptable without LD_PRELOAD |
