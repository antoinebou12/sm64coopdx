#ifndef THREADING_H
#define THREADING_H

#if defined(__3DS__)
#include <3ds.h>
#else
#include <pthread.h>
#endif

#include "cliopts.h"
#include "types.h"

// Macros
#define MUTEX_LOCK(handle) if (handle.state == RUNNING) { lock_mutex(&handle); }
#define MUTEX_UNLOCK(handle) if (handle.state == RUNNING) { unlock_mutex(&handle); }

// Types
enum ThreadState {
    INVALID = 0,
    STOPPED = 1,
    RUNNING = 2
};

struct ThreadHandle {
#if defined(__3DS__)
    Thread thread;
    LightLock mutex;
    void *(*entry)(void *);
    void *arg;
    bool detached;
#else
    pthread_t thread;
    pthread_mutex_t mutex;
#endif
    enum ThreadState state;
};

// Functions
//// Thread Handle
int init_thread_handle(struct ThreadHandle *handle, void *(*entry)(void *), void *arg, void *sp, size_t sp_size);
void cleanup_thread_handle(struct ThreadHandle *handle);

//// Thread
int init_thread(struct ThreadHandle *handle, void *(*entry)(void *), void *arg, void *sp, size_t sp_size);
int join_thread(struct ThreadHandle *handle);
int detach_thread(struct ThreadHandle *handle);
void exit_thread();
int stop_thread(struct ThreadHandle *handle);

//// Mutex
int init_mutex(struct ThreadHandle *handle);
int destroy_mutex(struct ThreadHandle *handle);
int lock_mutex(struct ThreadHandle *handle);
int trylock_mutex(struct ThreadHandle *handle);
int unlock_mutex(struct ThreadHandle *handle);

#endif // THREADING_H
