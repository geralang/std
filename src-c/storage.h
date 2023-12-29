
#ifndef GERASTD_STORAGE_H
#define GERASTD_STORAGE_H


#include <gera.h>
#include <stdlib.h>
#include "conc.h"

typedef struct Storage {
    MUTEX mutex;
    size_t element_size;
    char* data;
    size_t data_size;
    size_t data_length;
    size_t* free;
    size_t free_size;
    size_t free_length;
} Storage;

Storage storage_create(size_t element_size);
size_t storage_insert(Storage* s, void* data);
void* storage_get(Storage* s, size_t idx);
void storage_remove(Storage* s, size_t idx);
void storage_lock(Storage* s);
void storage_unlock(Storage* s);
void storage_free(Storage* s);


#endif