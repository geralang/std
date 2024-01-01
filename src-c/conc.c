
#include <gera.h>
#include <stdlib.h>
#include <stdio.h>
#include "conc.h"
#include "storage.h"
#include "prc.h"


#ifdef _WIN32
    THREAD thread_create(void* task, void* arg) {
        THREAD r = CreateThread(NULL, 0, task, arg, 0, NULL);
        if(r == NULL) {
            gera___panic("Unable to create a thread!");
        }
        return r;
    }

    void thread_join(THREAD* other) {
        if(WaitForSingleObject(*other, INFINITE) != WAIT_OBJECT_0) {
            gera___panic("Unable to join threads!");
        }
    }

    THREAD_IDENTIFIER thread_get_identifier(THREAD* t) {
        return GetThreadId(*t);
    }

    THREAD_IDENTIFIER thread_current_identifier() {
        return GetCurrentThreadId();
    }
    
    gbool thread_identifiers_equal(THREAD_IDENTIFIER a, THREAD_IDENTIFIER b) {
        return a == b;
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
        mutex_unlock(&c->mutex);
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

    THREAD_IDENTIFIER thread_get_identifier(THREAD* t) {
        return *t;
    }

    THREAD_IDENTIFIER thread_current_identifier() {
        return pthread_self();
    }
    
    gbool thread_identifiers_equal(THREAD_IDENTIFIER a, THREAD_IDENTIFIER b) {
        return pthread_equal(a, b);
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
        int status = pthread_mutex_destroy(m);
        if(status != 0) {
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
        mutex_unlock(&c->mutex);
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

THREAD_RET_T thread_start(ThreadTask* htask) {
    ThreadTask task = *htask;
    free(htask);
    task.call(task.captures);
    gera___rc_decr(task.captures);
    return THREAD_RET_V;
}

typedef GERA_CLOSURE_NOARGS(gint) ThreadHandle;

typedef struct ThreadEntry {
    THREAD thread;
    MUTEX mutex;
    COND_VAR cond_var;
    GeraAllocation* handle_allocation;
} ThreadEntry;

void free_thread_handle(char* data, size_t size) {
    size_t id = *((size_t*) data);
    ThreadEntry* thread_entry = storage_get(&THREAD_STORAGE, id);
    mutex_free(&thread_entry->mutex);
    cond_var_free(&thread_entry->cond_var);
    storage_remove(&THREAD_STORAGE, id);
}

gint get_thread_handle(GeraAllocation* captures) {
    return *((gint*) captures->data);
}

static inline void init_mutex_storage();

static inline void init_thread_storage() {
    if(!HAS_THREAD_STORAGE) {
        // this can only happen if there was no thread yet, no need for a mutex
        HAS_THREAD_STORAGE = 1;
        THREAD_STORAGE = storage_create(sizeof(ThreadEntry));
    }
    init_mutex_storage();
    init_process_storage();
}

ThreadHandle gera_std_conc_spawn(ThreadTask task) {
    gera___rc_incr(task.captures);
    init_thread_storage();
    ThreadEntry temp_thread_entry = (ThreadEntry) {
        .mutex = mutex_create(),
        .cond_var = cond_var_create()
    };
    size_t id = storage_insert(&THREAD_STORAGE, &temp_thread_entry);
    ThreadTask* htask = (ThreadTask*) malloc(sizeof(ThreadTask));
    *htask = task;
    ThreadEntry* thread_entry = storage_get(&THREAD_STORAGE, id);
    mutex_lock(&thread_entry->mutex); // prevent joining until fully initialized
    thread_entry->thread = thread_create((void*)(&thread_start), htask);
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
    init_thread_storage();
    THREAD_IDENTIFIER this_thread = thread_current_identifier();
    ThreadEntry* this_thread_entry;
    char found_entry = 0;
    storage_lock(&THREAD_STORAGE);
    for(size_t id = 0; id < THREAD_STORAGE.data_length; id += 1) {
        ThreadEntry* searched_thread = storage_get(&THREAD_STORAGE, id);
        if(!thread_identifiers_equal(
            thread_get_identifier(&searched_thread->thread), this_thread
        )) {
            continue;
        }
        this_thread_entry = searched_thread;
        found_entry = 1;
        break;
    }
    gera___rc_incr(this_thread_entry->handle_allocation);
    storage_unlock(&THREAD_STORAGE);
    cond_var_wait(&this_thread_entry->cond_var);
    gera___rc_decr(this_thread_entry->handle_allocation);
}

void gera_std_conc_notify(ThreadHandle other) {
    init_thread_storage();
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
    init_thread_storage();
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

typedef struct MutexEntry {
    MUTEX mutex;
    MUTEX data_mutex;
    THREAD_IDENTIFIER owner;
    char has_owner;
    GeraAllocation* handle_allocation;
} MutexEntry;

void free_mutex_handle(char* data, size_t size) {
    size_t id = *((size_t*) data);
    MutexEntry* mutex_entry = storage_get(&MUTEX_STORAGE, id);
    mutex_free(&mutex_entry->data_mutex);
    mutex_free(&mutex_entry->mutex);
    storage_remove(&MUTEX_STORAGE, id);
}

gint get_mutex_handle(GeraAllocation* captures) {
    return *((gint*) captures->data);
}

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
    init_mutex_storage();
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
    if(mutex_entry->has_owner
        && thread_identifiers_equal(
            mutex_entry->owner, thread_current_identifier()
        )
    ) {
        gera___panic("The mutex is already owned by this thread!");
    }
    mutex_unlock(&mutex_entry->data_mutex);
    gbool entered = mutex_try_lock(&mutex_entry->mutex);
    if(entered) {
        mutex_lock(&mutex_entry->data_mutex);
        mutex_entry->has_owner = 1;
        mutex_entry->owner = thread_current_identifier();
        mutex_unlock(&mutex_entry->data_mutex);
    }
    return entered;
}

void gera_std_conc_lock(MutexHandle mutex_handle) {
    init_mutex_storage();
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
    if(mutex_entry->has_owner
        && thread_identifiers_equal(
            mutex_entry->owner, thread_current_identifier()
        )
    ) {
        gera___panic("The mutex is already owned by this thread!");
    }
    mutex_unlock(&mutex_entry->data_mutex);
    mutex_lock(&mutex_entry->mutex);
    mutex_lock(&mutex_entry->data_mutex);
    mutex_entry->has_owner = 1;
    mutex_entry->owner = thread_current_identifier();
    mutex_unlock(&mutex_entry->data_mutex);
}

gbool gera_std_conc_is_locked(MutexHandle mutex_handle) {
    init_mutex_storage();
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
    init_mutex_storage();
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
    if(!mutex_entry->has_owner
        || !thread_identifiers_equal(
            mutex_entry->owner, thread_current_identifier()
        )
    ) {
        gera___panic("The mutex is not currently owned by this thread!");
    }
    mutex_unlock(&mutex_entry->mutex);
    mutex_entry->has_owner = 0;
    mutex_unlock(&mutex_entry->data_mutex);
}