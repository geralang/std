
#include "storage.h"
#include <gera.h>
#include <string.h>

Storage storage_create(size_t element_size) {
    Storage s;
    s.mutex = mutex_create();
    s.element_size = element_size;
    s.data_size = 16;
    s.data_length = 0;
    s.data = (char*) malloc(element_size * s.data_size);
    s.free_size = 8;
    s.free_length = 0;
    s.free = (size_t*) malloc(sizeof(size_t) * s.free_size);
    return s;
}

size_t storage_insert(Storage* s, void* data) {
    mutex_lock(&s->mutex);
    size_t idx;
    if(s->free_length > 0) {
        s->free_length -= 1;
        idx = s->free[s->free_length];
    } else {
        idx = s->data_length;
        s->data_length += 1;
        if(s->data_length > s->data_size) {
            s->data_size *= 2;
            s->data = (char*) realloc(s->data, s->element_size * s->data_size);
        }
    }
    memcpy(s->data + idx * s->element_size, data, s->element_size);
    mutex_unlock(&s->mutex);
    return idx;
}

void* storage_get(Storage* s, size_t idx) {
    return s->data + idx * s->element_size;
}

void storage_remove(Storage* s, size_t idx) {
    mutex_lock(&s->mutex);
    size_t fidx = s->free_length;
    s->free_length += 1;
    if(s->free_length > s->free_size) {
        s->free_size *= 2;
        s->free = (size_t*) realloc(s->free, sizeof(size_t) * s->free_size);
    }
    s->free[fidx] = idx;
    mutex_unlock(&s->mutex);
}

void storage_lock(Storage* s) {
    mutex_lock(&s->mutex);
}

void storage_unlock(Storage* s) {
    mutex_unlock(&s->mutex);
}

void storage_free(Storage* s) {
    mutex_free(&s->mutex);
    free(s->data);
    free(s->free);
}

