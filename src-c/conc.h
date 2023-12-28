
#ifndef GERASTD_CONC_H
#define GERASTD_CONC_H


#include <gera.h>

typedef GERA_CLOSURE_NOARGS(void) ThreadTask;

#ifdef _WIN32
    #include <windows.h>
    #define THREAD HANDLE
    #define THREAD_RET_T DWORD
    #define THREAD_RET_V 0
    #define MUTEX CRITICAL_SECTION
    #ifdef _MSC_VER
        #define THREAD_LOCAL __declspec(thread)
    #else
        #define THREAD_LOCAL __thread
    #endif
    typedef struct {
        MUTEX mutex;
        CONDITION_VARIABLE cond;
    } COND_VAR;
#else
    #include <pthread.h>
    #define THREAD pthread_t
    #define THREAD_RET_T void*
    #define THREAD_RET_V NULL
    #define MUTEX pthread_mutex_t
    #define THREAD_LOCAL _Thread_local
    typedef struct {
        MUTEX mutex;
        pthread_cond_t cond;
    } COND_VAR;
#endif

THREAD thread_create(void* task, void* arg);
THREAD thread_current();
void thread_wait();
void thread_notify(THREAD* other);
void thread_join(THREAD* other);

MUTEX mutex_create();
gbool mutex_try_lock(MUTEX* m);
void mutex_lock(MUTEX* m);
void mutex_unlock(MUTEX* m);
void mutex_free(MUTEX* m);

COND_VAR cond_var_create();
void cond_var_wait(COND_VAR* c);
void cond_var_notify(COND_VAR* c);
void cond_var_free(COND_VAR* c);

#endif