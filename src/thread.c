#include "thread.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

thread_t *thread_create(SDL_ThreadFunction func, const char *name, void *data)
{
    if (!func) return NULL;

    thread_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;

    t->func = func;
    t->data = data;
    SDL_SetAtomicInt(&t->running, 1);

    t->thread = SDL_CreateThread(func, name, t);
    if (!t->thread) {
        SDL_SetAtomicInt(&t->running, 0);
        free(t);
        return NULL;
    }

    return t;
}

void thread_stop(thread_t *t)
{
    if (!t) return;

    SDL_SetAtomicInt(&t->running, 0);
    SDL_WaitThread(t->thread, NULL);
    free(t);
}
