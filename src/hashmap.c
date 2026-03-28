#include "hashmap.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define HASHMAP_DEFAULT_CAPACITY 64u

/* ------------------------------------------------------------------ */
/* djb2 hash                                                           */
/* ------------------------------------------------------------------ */
static size_t hash_key(const char *key, size_t capacity) {
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*key++))
        h = ((h << 5) + h) + (unsigned long)c; /* h * 33 + c */
    return (size_t)(h % capacity);
}

/* ------------------------------------------------------------------ */
/* hashmap_new                                                         */
/* ------------------------------------------------------------------ */
HashMap *hashmap_new(size_t capacity) {
    if (capacity == 0)
        capacity = HASHMAP_DEFAULT_CAPACITY;

    HashMap *map = malloc(sizeof(HashMap));
    if (!map)
        return NULL;

    map->buckets = calloc(capacity, sizeof(Entry *));
    if (!map->buckets) {
        free(map);
        return NULL;
    }

    map->capacity = capacity;
    map->count = 0;
    return map;
}

/* ------------------------------------------------------------------ */
/* hashmap_insert                                                      */
/* ------------------------------------------------------------------ */
bool hashmap_insert(HashMap *map, const char *key, void *value) {
    size_t idx = hash_key(key, map->capacity);
    Entry *chain = map->buckets[idx];

    /* Walk the chain – update in place if key already exists */
    for (Entry *e = chain; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            e->value = value; /* struct copy */
            return true;
        }
    }

    /* Key not found – prepend a new node */
    Entry *node = malloc(sizeof(Entry));
    if (!node)
        return false;

    node->key = SDL_strdup(key);
    if (!node->key) {
        free(node);
        return false;
    }

    node->value = value; /* struct copy */
    node->next = chain;
    map->buckets[idx] = node;
    map->count++;
    return true;
}

/* ------------------------------------------------------------------ */
/* hashmap_get                                                         */
/* ------------------------------------------------------------------ */
void *hashmap_get(HashMap *map, const char *key) {
    size_t idx = hash_key(key, map->capacity);

    for (Entry *e = map->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key, key) == 0)
            return &e->value;
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* hashmap_iterate                                                     */
/* ------------------------------------------------------------------ */
void hashmap_iterate(HashMap *map, HashMapIterFn fn, void *userdata) {
    for (size_t i = 0; i < map->capacity; i++) {
        for (Entry *e = map->buckets[i]; e; e = e->next) {
            fn(e->key, &e->value, userdata);
        }
    }
}

/* ------------------------------------------------------------------ */
/* hashmap_free                                                        */
/* ------------------------------------------------------------------ */
void hashmap_free(HashMap *map) {
    if (!map)
        return;

    for (size_t i = 0; i < map->capacity; i++) {
        Entry *e = map->buckets[i];
        while (e) {
            Entry *next = e->next;
            free(e->key); /* we own the key copy  */
            free(e);
            e = next;
        }
    }

    free(map->buckets);
    free(map);
}
