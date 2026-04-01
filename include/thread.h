#ifndef THREAD_H
#define THREAD_H

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_thread.h>

typedef struct {
    SDL_AtomicInt running;
    SDL_Thread *thread;
    SDL_ThreadFunction func;
    void *data;
} thread_t;

/* Creates and starts a new thread. Returns NULL on failure. */
thread_t *thread_create(SDL_ThreadFunction func, const char *name, void *data);

/* Signals the thread to stop, waits for it to finish, then frees the struct. */
void thread_stop(thread_t *t);

/* Thread-safe read of the running flag. */
static inline int thread_is_running(thread_t *t) { return SDL_GetAtomicInt(&t->running); }

#endif /* THREAD_H */
