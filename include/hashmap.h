#ifndef HASHMAP_H
#define HASHMAP_H

#include "argparse.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Internal chain node                                                 */
/* ------------------------------------------------------------------ */
typedef struct Entry {
    char *key;       /* heap-allocated copy of the key           */
    ArgResult value; /* shallow copy of the inserted ArgResult   */
    struct Entry *next;
} Entry;

/* ------------------------------------------------------------------ */
/* HashMap                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    Entry **buckets;
    size_t capacity;
    size_t count;
} HashMap;

/* Callback type used by hashmap_iterate */
typedef void (*HashMapIterFn)(const char *key, ArgResult *value, void *userdata);

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/**
 * Allocate and initialise a new HashMap.
 * @param capacity  Number of buckets (use 0 for the default of 64).
 * @return          Pointer to the new map, or NULL on allocation failure.
 */
HashMap *hashmap_new(size_t capacity);

/**
 * Insert or update a key-value pair.
 * The key is strdup'd; the value is copied by value.
 * @return  true on success, false on allocation failure.
 */
bool hashmap_insert(HashMap *map, const char *key, ArgResult value);

/**
 * Look up a key.
 * @return  Pointer to the stored ArgResult, or NULL if not found.
 *          The pointer is valid until the next insert/free on this map.
 */
ArgResult *hashmap_get(HashMap *map, const char *key);

/**
 * Iterate over every entry in the map (order is unspecified).
 * @param fn        Callback invoked for each key-value pair.
 * @param userdata  Passed through to every callback invocation.
 */
void hashmap_iterate(HashMap *map, HashMapIterFn fn, void *userdata);

/**
 * Free all memory owned by the map (keys and bucket array).
 * Does NOT free the ArgResult.name pointers – you own those.
 */
void hashmap_free(HashMap *map);

#endif /* HASHMAP_H */
