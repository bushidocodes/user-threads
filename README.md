# user-threads

A userspace port of the `gwthd` threading library from [hw4-19-threading](https://github.com/bushidocodes/hw4-19-threading-bushidocodes), which implemented 1:1 POSIX-style threads inside the xv6 teaching kernel.

This repo keeps the same API and test suite but replaces the kernel syscalls with **Windows Fibers**, giving an N:1 cooperative green-thread scheduler that runs entirely in userspace.

## API

```c
typedef int     gwthd_t;
typedef void *(*gwthd_fn_t)(void *);
```

---

### `gwthd_create`

```c
int gwthd_create(gwthd_t *childid, gwthd_fn_t fn, void *arg);
```

Creates a new green thread that executes `fn(arg)`. The thread is added to the
run queue immediately but does not start running until the caller yields control
(via `gwthd_join` or `gwthd_yield`). Only the main context may create threads;
calling this from inside a thread returns `-1`.

| Parameter | Description |
|-----------|-------------|
| `childid` | Output — set to the new thread's `gwthd_t` id on success |
| `fn` | Function the thread will execute. Should call `gwthd_exit()` rather than returning |
| `arg` | Opaque pointer forwarded to `fn` as its sole argument |

**Returns** `0` on success; `-1` if called from a thread, if the thread table is
full (`MAX_THREADS = 64`), or if fiber creation fails.

---

### `gwthd_exit`

```c
void gwthd_exit(void);
```

Terminates the calling thread. Marks the thread as ZOMBIE, wakes any parent
blocked in `gwthd_join` waiting on this thread, then yields to the scheduler.
The fiber's resources are freed by the parent's subsequent `gwthd_join` — not
here — because a fiber cannot delete its own stack while still running on it.

If called from the main context (not a thread), logs a diagnostic to `stderr`
and returns harmlessly; the process is not affected.

---

### `gwthd_join`

```c
int gwthd_join(gwthd_t child);
```

Blocks until the child thread has exited, then frees its resources. If the
child has already called `gwthd_exit()` by the time `join` is called, this
returns immediately without yielding. Otherwise the caller is suspended and the
scheduler runs other threads until the child finishes.

| Parameter | Description |
|-----------|-------------|
| `child` | The `gwthd_t` id returned by `gwthd_create` for the target thread |

**Returns** `0` on success; `-1` if called from a thread or if `child` is not a
valid thread id.

---

### `gwthd_id`

```c
gwthd_t gwthd_id(void);
```

Returns the unique id of the calling thread. Safe to call from both the main
context and child threads. The main context always returns `1`; child threads
return the id assigned by `gwthd_create`.

**Returns** The `gwthd_t` id of the currently executing thread.

---

### `gwthd_yield`

```c
void gwthd_yield(void);
```

Voluntarily relinquishes the CPU without exiting. Moves the calling thread back
to runnable state and switches to the scheduler. The round-robin scheduler
resumes this thread only after every other currently runnable thread has had at
least one turn, preventing starvation. Safe to call from both the main context
and child threads.

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
