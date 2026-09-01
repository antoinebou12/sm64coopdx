#ifdef __3DS__

#include "pc/thread.h"

#include <string.h>

#define NEW3DS_THREAD_DEFAULT_STACK (128 * 1024)
#define NEW3DS_THREAD_PRIORITY 0x30

static void new3ds_thread_entry(void *arg) {
    struct ThreadHandle *handle = (struct ThreadHandle *)arg;
    if (handle != NULL && handle->entry != NULL) {
        handle->entry(handle->arg);
        handle->state = STOPPED;
    }
    threadExit(0);
}

int init_thread_handle(
    struct ThreadHandle *handle,
    void *(*entry)(void *),
    void *arg,
    void *sp,
    size_t sp_size) {
    if (handle == NULL || entry == NULL) return -1;
    memset(handle, 0, sizeof(*handle));

    int mutex_result = init_mutex(handle);
    int thread_result = init_thread(handle, entry, arg, sp, sp_size);
    return mutex_result != 0 || thread_result != 0;
}

void cleanup_thread_handle(struct ThreadHandle *handle) {
    if (handle == NULL) return;

    if (handle->thread != NULL && !handle->detached) {
        (void)threadJoin(handle->thread, U64_MAX);
        threadFree(handle->thread);
    }

    memset(handle, 0, sizeof(*handle));
    handle->state = INVALID;
}

int init_thread(
    struct ThreadHandle *handle,
    void *(*entry)(void *),
    void *arg,
    void *sp,
    size_t sp_size) {
    (void)sp;
    if (handle == NULL || entry == NULL) return -1;

    handle->entry = entry;
    handle->arg = arg;
    handle->detached = false;

    const size_t stack_size = sp_size > 0 ? sp_size : NEW3DS_THREAD_DEFAULT_STACK;
    handle->thread = threadCreate(
        new3ds_thread_entry,
        handle,
        stack_size,
        NEW3DS_THREAD_PRIORITY,
        -2,
        false);
    if (handle->thread == NULL) {
        handle->state = INVALID;
        return -1;
    }

    handle->state = RUNNING;
    return 0;
}

int join_thread(struct ThreadHandle *handle) {
    if (handle == NULL || handle->thread == NULL || handle->detached) return -1;

    Result rc = threadJoin(handle->thread, U64_MAX);
    if (R_FAILED(rc)) return -1;

    threadFree(handle->thread);
    handle->thread = NULL;
    handle->state = STOPPED;
    return 0;
}

int detach_thread(struct ThreadHandle *handle) {
    if (handle == NULL || handle->thread == NULL || handle->detached) return -1;

    threadDetach(handle->thread);
    handle->detached = true;
    handle->state = STOPPED;
    return 0;
}

void exit_thread(void) {
    threadExit(0);
}

int stop_thread(struct ThreadHandle *handle) {
    /*
     * libctru intentionally has no pthread_cancel equivalent. CoopDX worker
     * threads should terminate through their normal state/exit conditions and
     * then be joined. Refuse unsafe forced termination here.
     */
    if (handle == NULL) return -1;
    return -1;
}

int init_mutex(struct ThreadHandle *handle) {
    if (handle == NULL) return -1;
    LightLock_Init(&handle->mutex);
    return 0;
}

int destroy_mutex(struct ThreadHandle *handle) {
    if (handle == NULL) return -1;
    /* LightLock has no destruction requirement. */
    return 0;
}

int lock_mutex(struct ThreadHandle *handle) {
    if (handle == NULL) return -1;
    LightLock_Lock(&handle->mutex);
    return 0;
}

int trylock_mutex(struct ThreadHandle *handle) {
    if (handle == NULL) return -1;
    return LightLock_TryLock(&handle->mutex);
}

int unlock_mutex(struct ThreadHandle *handle) {
    if (handle == NULL) return -1;
    LightLock_Unlock(&handle->mutex);
    return 0;
}

#endif /* __3DS__ */
