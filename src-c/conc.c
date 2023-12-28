
#include <gera.h>
#include <stdlib.h>
#include <stdio.h>
#include "conc.h"
#include "storage.h"

#ifdef _WIN32
    THREAD thread_create(void* task, void* arg) {
        THREAD r = CreateThread(NULL, 0, task, arg, 0, NULL);
        if(r == NULL) {
            gera___panic("Unable to create a thread!");
        }
        return r;
    }

    THREAD thread_current() {
        return GetCurrentThread();
    }

    void thread_join(THREAD* other) {
        if(WaitForSingleObject(*other, INFINITE) != WAIT_OBJECT_0) {
            gera___panic("Unable to join threads!");
        }
    }

    MUTEX mutex_create() {
        MUTEX m;
        InitializeCriticalSection(&m);
        return m;
    }

    gbool mutex_try_lock(MUTEX* m) {
        return TryEnterCriticalSection(m);
    }

    void mutex_lock(MUTEX* m) {
        EnterCriticalSection(m);
    }

    void mutex_unlock(MUTEX* m) {
        LeaveCriticalSection(m);
    }

    void mutex_free(MUTEX* m) {
        DeleteCriticalSection(m);
    }

    COND_VAR cond_var_create() {
        COND_VAR r;
        r.mutex = mutex_create();
        InitializeConditionVariable(&r.cond);
        return r;
    }

    void cond_var_wait(COND_VAR* c) {
        mutex_lock(&c->mutex);
        if(SleepConditionVariableCS(&c->cond, &c->mutex, INFINITE) == 0) {
            gera___panic("Unable to wait for condition variable!");
        }
        mutex_free(&c->mutex);
    }

    void cond_var_notify(COND_VAR* c) {
        mutex_lock(&c->mutex);
        WakeAllConditionVariable(&c->cond);
        mutex_unlock(&c->mutex);
    }

    void cond_var_free(COND_VAR* c) {
        mutex_free(&c->mutex);
    }
#else
    #include <errno.h>

    THREAD thread_create(void* task, void* arg) {
        THREAD r;
        if(pthread_create(&r, NULL, task, arg) != 0) {
            gera___panic("Unable to create a thread!");
        }
        return r;
    }

    THREAD thread_current() {
        return pthread_self();
    }

    void thread_join(THREAD* other) {
        if(pthread_join(*other, NULL) != 0) {
            gera___panic("Unable to join threads!");
        }
    }

    MUTEX mutex_create() {
        MUTEX m;
        if(pthread_mutex_init(&m, NULL) != 0) { 
            gera___panic("Unable to create a mutex!");
        } 
        return m;
    }

    gbool mutex_try_lock(MUTEX* m) {
        int code = pthread_mutex_trylock(m);
        if(code == 0) { return 1; }
        if(code == EBUSY) { return 0; }
        gera___panic("Unable to lock a mutex!");
    }

    void mutex_lock(MUTEX* m) {
        if(pthread_mutex_lock(m) != 0) {
            gera___panic("Unable to lock a mutex!");
        }
    }
    
    void mutex_unlock(MUTEX* m) {
        if(pthread_mutex_unlock(m) != 0) {
            gera___panic("Unable to unlock a mutex!");
        }
    }

    void mutex_free(MUTEX* m) {
        if(pthread_mutex_destroy(m) != 0) {
            gera___panic("Unable to destroy a mutex!");
        }
    }

    COND_VAR cond_var_create() {
        COND_VAR r;
        r.mutex = mutex_create();
        if(pthread_cond_init(&r.cond, NULL) != 0) {
            gera___panic("Unable to create a condition variable!");
        }
        return r;
    }

    void cond_var_wait(COND_VAR* c) {
        mutex_lock(&c->mutex);
        if(pthread_cond_wait(&c->cond, &c->mutex) != 0) {
            gera___panic("Unable to wait for condition variable!");
        }
    }

    void cond_var_notify(COND_VAR* c) {
        mutex_lock(&c->mutex);
        if(pthread_cond_broadcast(&c->cond) != 0) {
            gera___panic("Unable to broadcast on condition variable!");
        }
        mutex_unlock(&c->mutex);
    }

    void cond_var_free(COND_VAR* c) {
        mutex_free(&c->mutex);
        if(pthread_cond_destroy(&c->cond) != 0) {
            gera___panic("Unable to destroy a condition variable!");
        }
    }
#endif


static gbool HAS_THREAD_STORAGE = 0;
static Storage THREAD_STORAGE;

typedef struct ThreadData {
    gint internal_id;
    ThreadTask task;
} ThreadData;

THREAD_LOCAL gbool HAS_THREAD_ID = 0;
THREAD_LOCAL size_t THREAD_ID;

THREAD_RET_T thread_start(ThreadData* hdata) {
    ThreadData data = *hdata;
    HAS_THREAD_ID = 1;
    THREAD_ID = *((size_t*) &data.internal_id);
    free(hdata);
    data.task.call(data.task.captures);
    gera___rc_decr(data.task.captures);
    return THREAD_RET_V;
}

typedef GERA_CLOSURE_NOARGS(gint) ThreadHandle;

void free_thread_handle(char* data, size_t size) {
    storage_remove(&THREAD_STORAGE, *((size_t*) data));
}

gint get_thread_handle(GeraAllocation* captures) {
    return *((gint*) captures->data);
}

typedef struct ThreadEntry {
    THREAD thread;
    MUTEX mutex;
    COND_VAR cond_var;
    GeraAllocation* handle_allocation;
} ThreadEntry;

static inline void init_mutex_storage();

ThreadHandle gera_std_conc_spawn(ThreadTask task) {
    gera___rc_incr(task.captures);
    if(!HAS_THREAD_STORAGE) {
        // this can only happen if there was no thread yet, no need for a mutex
        HAS_THREAD_STORAGE = 1;
        THREAD_STORAGE = storage_create(sizeof(ThreadEntry));
    }
    init_mutex_storage();
    ThreadEntry temp_thread_entry = (ThreadEntry) {
        .mutex = mutex_create(),
        .cond_var = cond_var_create()
    };
    size_t id = storage_insert(&THREAD_STORAGE, &temp_thread_entry);
    ThreadData* hdata = (ThreadData*) malloc(sizeof(ThreadData));
    hdata->task = task;
    hdata->internal_id = id;
    ThreadEntry* thread_entry = storage_get(&THREAD_STORAGE, id);
    mutex_lock(&thread_entry->mutex); // prevent joining until fully initialized
    thread_entry->thread = thread_create((void*)(&thread_start), hdata);
    GeraAllocation* allocation = gera___rc_alloc(
        sizeof(gint), &free_thread_handle
    );
    *((size_t*) allocation->data) = id;
    thread_entry->handle_allocation = allocation;
    mutex_unlock(&thread_entry->mutex);
    return (ThreadHandle) {
        .captures = allocation,
        .call = &get_thread_handle
    };
}

void gera_std_conc_wait() {
    if(!HAS_THREAD_ID) {
        gera___panic(
            "This thread has not been created with 'std::conc::spawn', \
and therefore cannot await notifications!"
        );
    }
    ThreadEntry* this_thread = storage_get(&THREAD_STORAGE, THREAD_ID);
    cond_var_wait(&this_thread->cond_var);
}

void gera_std_conc_notify(ThreadHandle other) {
    gint sid = other.call(other.captures);
    size_t id = *((size_t*) &sid);
    if(id >= THREAD_STORAGE.data_length) {
        gera___panic("The provided thread handle is invalid!");
    }
    ThreadEntry* other_thread = storage_get(&THREAD_STORAGE, id);
    if(other_thread->handle_allocation != other.captures) {
        gera___panic("The provided thread handle is invalid!");
    }
    cond_var_notify(&other_thread->cond_var);
}

void gera_std_conc_join(ThreadHandle other) {
    gint sid = other.call(other.captures);
    size_t id = *((size_t*) &sid);
    if(id >= THREAD_STORAGE.data_length) {
        gera___panic("The provided thread handle is invalid!");
    }
    ThreadEntry* other_thread = storage_get(&THREAD_STORAGE, id);
    if(other_thread->handle_allocation != other.captures) {
        gera___panic("The provided thread handle is invalid!");
    }
    mutex_lock(&other_thread->mutex);
    thread_join(&other_thread->thread);
    mutex_unlock(&other_thread->mutex);
}

#ifdef _WIN32
    void gera_std_conc_sleep(gint millis) {
        if(millis <= 0) { return; }
        Sleep(millis);
    }
#else
    #include <sys/select.h>
    #include <time.h>

    void gera_std_conc_sleep(gint millis) {
        if(millis <= 0) { return; }
        struct timespec ts;
        ts.tv_sec = millis / 1000;
        ts.tv_nsec = (millis % 1000) * 1000000;
        pselect(0, NULL, NULL, NULL, &ts, NULL);
    }
#endif


static gbool HAS_MUTEX_STORAGE = 0;
static Storage MUTEX_STORAGE;

typedef GERA_CLOSURE_NOARGS(gint) MutexHandle;

void free_mutex_handle(char* data, size_t size) {
    storage_remove(&MUTEX_STORAGE, *((size_t*) data));
}

gint get_mutex_handle(GeraAllocation* captures) {
    return *((gint*) captures->data);
}

typedef struct MutexEntry {
    MUTEX mutex;
    MUTEX data_mutex;
    THREAD owner;
    char has_owner;
    GeraAllocation* handle_allocation;
} MutexEntry;

static inline void init_mutex_storage() {
    if(!HAS_MUTEX_STORAGE) {
        HAS_MUTEX_STORAGE = 1;
        MUTEX_STORAGE = storage_create(sizeof(MutexEntry));
    }
}

MutexHandle gera_std_conc_mutex() {
    init_mutex_storage();
    GeraAllocation* allocation = gera___rc_alloc(
        sizeof(gint), &free_mutex_handle
    );
    MutexEntry mutex_entry = (MutexEntry) {
        .mutex = mutex_create(),
        .data_mutex = mutex_create(),
        .handle_allocation = allocation,
        .has_owner = 0
    };
    size_t id = storage_insert(&MUTEX_STORAGE, &mutex_entry);
    *((size_t*) allocation->data) = id;
    return (MutexHandle) {
        .captures = allocation,
        .call = &get_mutex_handle
    };
}

gbool gera_std_conc_try_lock(MutexHandle mutex_handle) {
    gint sid = mutex_handle.call(mutex_handle.captures);
    size_t id = *((size_t*) &sid);
    if(id >= MUTEX_STORAGE.data_length) {
        gera___panic("The provided mutex handle is invalid!");
    }
    MutexEntry* mutex_entry = storage_get(&MUTEX_STORAGE, id);
    if(mutex_entry->handle_allocation != mutex_handle.captures) {
        gera___panic("The provided mutex handle is invalid!");
    }
    mutex_lock(&mutex_entry->data_mutex);
    if(mutex_entry->has_owner && mutex_entry->owner == thread_current()) {
        gera___panic("The mutex is already owned by this thread!");
    }
    mutex_unlock(&mutex_entry->data_mutex);
    gbool entered = mutex_try_lock(&mutex_entry->mutex);
    if(entered) {
        mutex_lock(&mutex_entry->data_mutex);
        mutex_entry->has_owner = 1;
        mutex_entry->owner = thread_current();
        mutex_unlock(&mutex_entry->data_mutex);
    }
    return entered;
}

void gera_std_conc_lock(MutexHandle mutex_handle) {
    gint sid = mutex_handle.call(mutex_handle.captures);
    size_t id = *((size_t*) &sid);
    if(id >= MUTEX_STORAGE.data_length) {
        gera___panic("The provided mutex handle is invalid!");
    }
    MutexEntry* mutex_entry = storage_get(&MUTEX_STORAGE, id);
    if(mutex_entry->handle_allocation != mutex_handle.captures) {
        gera___panic("The provided mutex handle is invalid!");
    }
    mutex_lock(&mutex_entry->data_mutex);
    if(mutex_entry->has_owner && mutex_entry->owner == thread_current()) {
        gera___panic("The mutex is already owned by this thread!");
    }
    mutex_unlock(&mutex_entry->data_mutex);
    mutex_lock(&mutex_entry->mutex);
    mutex_lock(&mutex_entry->data_mutex);
    mutex_entry->has_owner = 1;
    mutex_entry->owner = thread_current();
    mutex_unlock(&mutex_entry->data_mutex);
}

gbool gera_std_conc_is_locked(MutexHandle mutex_handle) {
    gint sid = mutex_handle.call(mutex_handle.captures);
    size_t id = *((size_t*) &sid);
    if(id >= MUTEX_STORAGE.data_length) {
        gera___panic("The provided mutex handle is invalid!");
    }
    MutexEntry* mutex_entry = storage_get(&MUTEX_STORAGE, id);
    if(mutex_entry->handle_allocation != mutex_handle.captures) {
        gera___panic("The provided mutex handle is invalid!");
    }
    mutex_lock(&mutex_entry->data_mutex);
    gbool is_locked = mutex_entry->has_owner;
    mutex_unlock(&mutex_entry->data_mutex);
    return is_locked;
}

void gera_std_conc_unlock(MutexHandle mutex_handle) {
    gint sid = mutex_handle.call(mutex_handle.captures);
    size_t id = *((size_t*) &sid);
    if(id >= MUTEX_STORAGE.data_length) {
        gera___panic("The provided mutex handle is invalid!");
    }
    MutexEntry* mutex_entry = storage_get(&MUTEX_STORAGE, id);
    if(mutex_entry->handle_allocation != mutex_handle.captures) {
        gera___panic("The provided mutex handle is invalid!");
    }
    mutex_lock(&mutex_entry->data_mutex);
    if(!mutex_entry->has_owner || mutex_entry->owner != thread_current()) {
        gera___panic("The mutex is not currently owned by this thread!");
    }
    mutex_unlock(&mutex_entry->mutex);
    mutex_entry->has_owner = 0;
    mutex_unlock(&mutex_entry->data_mutex);
}