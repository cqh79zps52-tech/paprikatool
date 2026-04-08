/*
 * Tiny portable thread + mutex shim. Just enough for the GUI's worker
 * pattern: spawn a detached thread that pushes lines into a mutex-guarded
 * ring buffer the main thread polls each frame.
 */
#include "paprika.h"

#include <stdlib.h>

#if defined(PAPRIKA_WINDOWS)
#  include <windows.h>
#  include <process.h>
#else
#  include <pthread.h>
#endif

struct paprika_mutex {
#if defined(PAPRIKA_WINDOWS)
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t  m;
#endif
};

paprika_mutex *paprika_mutex_new(void)
{
    paprika_mutex *m = (paprika_mutex *)calloc(1, sizeof(*m));
    if (!m) return NULL;
#if defined(PAPRIKA_WINDOWS)
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutex_init(&m->m, NULL);
#endif
    return m;
}

void paprika_mutex_free(paprika_mutex *m)
{
    if (!m) return;
#if defined(PAPRIKA_WINDOWS)
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->m);
#endif
    free(m);
}

void paprika_mutex_lock(paprika_mutex *m)
{
#if defined(PAPRIKA_WINDOWS)
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->m);
#endif
}

void paprika_mutex_unlock(paprika_mutex *m)
{
#if defined(PAPRIKA_WINDOWS)
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->m);
#endif
}

/* ── thread spawn ────────────────────────────────────────────────────────── */

typedef struct {
    void (*entry)(void *);
    void  *arg;
} thunk_t;

#if defined(PAPRIKA_WINDOWS)
static unsigned __stdcall win_thunk(void *p)
{
    thunk_t *t = (thunk_t *)p;
    t->entry(t->arg);
    free(t);
    return 0;
}
#else
static void *posix_thunk(void *p)
{
    thunk_t *t = (thunk_t *)p;
    t->entry(t->arg);
    free(t);
    return NULL;
}
#endif

bool paprika_thread_spawn(void (*entry)(void *), void *arg)
{
    thunk_t *t = (thunk_t *)malloc(sizeof(*t));
    if (!t) return false;
    t->entry = entry;
    t->arg   = arg;

#if defined(PAPRIKA_WINDOWS)
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, win_thunk, t, 0, NULL);
    if (!h) { free(t); return false; }
    CloseHandle(h);
    return true;
#else
    pthread_t th;
    if (pthread_create(&th, NULL, posix_thunk, t) != 0) { free(t); return false; }
    pthread_detach(th);
    return true;
#endif
}
