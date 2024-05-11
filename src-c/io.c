
#include <gera.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "conc.h"


void gera_std_io_println(GeraString line) {
    char buffer[line.length_bytes + 1];
    memcpy(buffer, line.data, line.length_bytes);
    buffer[line.length_bytes] = '\n';
    fwrite(buffer, sizeof(char), line.length_bytes + 1, stdout);
    fflush(stdout);
    gera___ref_deleted(line.allocation);
}

void gera_std_io_eprintln(GeraString line) {
    char buffer[line.length_bytes + 1];
    memcpy(buffer, line.data, line.length_bytes);
    buffer[line.length_bytes] = '\n';
    fwrite(buffer, sizeof(char), line.length_bytes + 1, stderr);
    fflush(stderr);
    gera___ref_deleted(line.allocation);
}

void gera_std_io_print(GeraString thing) {
    fwrite(thing.data, sizeof(char), thing.length_bytes, stdout);
    fflush(stdout);
    gera___ref_deleted(thing.allocation);
}

void gera_std_io_eprint(GeraString thing) {
    fwrite(thing.data, sizeof(char), thing.length_bytes, stderr);
    fflush(stderr);
    gera___ref_deleted(thing.allocation);
}

GeraString gera_std_io_inputln() {
    size_t buffer_size = 64;
    char* buffer = (char*) malloc(buffer_size);
    size_t i = 0;
    if(buffer == NULL) { gera___panic("Unable to allocate memory!"); }
    for(;;) {
        int c = fgetc(stdin);
        if(c == EOF) { break; }
        if(c == '\r') { c = fgetc(stdin); }
        if(c == '\n') { break; }
        if(i >= buffer_size) {
            buffer_size *= 2;
            buffer = (char*) realloc(buffer, buffer_size);
            if(buffer == NULL) { gera___panic("Unable to allocate memory!"); }
        }
        buffer[i] = c;
        i += 1;
    }
    GeraAllocation* alloc = gera___alloc(i, NULL);
    memcpy(alloc->data, buffer, i);
    free(buffer);
    size_t length = 0;
    for(size_t o = 0; o < i; length += 1) {
        o += gera___codepoint_size(alloc->data[o]);
    }
    return (GeraString) {
        .allocation = alloc,
        .data = alloc->data,
        .length_bytes = i,
        .length = length
    };
}


static const GeraString FILE_SEP = (GeraString) {
    .allocation = NULL,
#ifdef _WIN32
    .data = "\\",
    .length = 1,
    .length_bytes = 1
#else
    .data = "/",
    .length = 1,
    .length_bytes = 1
#endif    
};
GeraString gera_std_io_file_sep() { return FILE_SEP; }

static const GeraString PATH_SEP = (GeraString) {
    .allocation = NULL,
#ifdef _WIN32
    .data = ";",
    .length = 1,
    .length_bytes = 1
#else
    .data = ":",
    .length = 1,
    .length_bytes = 1
#endif  
};
GeraString gera_std_io_path_sep() { return PATH_SEP; }


typedef struct {
    gbool has_error;
    char data[];
} IoResultLayout;

static void free_str_io_result(GeraAllocation* allocation) {
    IoResultLayout* data = (IoResultLayout*) allocation->data;
    GeraString* value = (GeraString*) data->data;
    gera___ref_deleted(value->allocation);
}

static void free_arr_io_result(GeraAllocation* allocation) {
    IoResultLayout* data = (IoResultLayout*) allocation->data;
    GeraArray* value = (GeraArray*) data->data;
    gera___ref_deleted(value->allocation);
}

static GeraObject create_io_result_err(const char* reason) {
    GeraAllocation* a = gera___alloc(
        sizeof(IoResultLayout) + sizeof(GeraString), &free_str_io_result
    );
    IoResultLayout* data = (IoResultLayout*) a->data;
    data->has_error = 1;
    *((GeraString*) data->data) = gera___alloc_string(reason);
    return (GeraObject) { .allocation = a };
}

static GeraObject create_io_result_empty() {
    GeraAllocation* a = gera___alloc(sizeof(IoResultLayout), NULL);
    IoResultLayout* data = (IoResultLayout*) a->data;
    data->has_error = 0;
    return (GeraObject) { .allocation = a };
}

static GeraObject create_io_result_str(GeraString value) {
    GeraAllocation* a = gera___alloc(
        sizeof(IoResultLayout) + sizeof(GeraString), &free_str_io_result
    );
    IoResultLayout* data = (IoResultLayout*) a->data;
    data->has_error = 0;
    *((GeraString*) data->data) = value;
    return (GeraObject) { .allocation = a };
}

static GeraObject create_io_result_str_arr(GeraArray value) {
    GeraAllocation* a = gera___alloc(
        sizeof(IoResultLayout) + sizeof(GeraArray), &free_arr_io_result
    );
    IoResultLayout* data = (IoResultLayout*) a->data;
    data->has_error = 0;
    *((GeraArray*) data->data) = value;
    return (GeraObject) { .allocation = a };
}

GeraString gera_std_io_result_get_err(GeraObject result) {
    IoResultLayout* data = (IoResultLayout*) result.allocation->data;
    GeraString value = *((GeraString*) data->data);
    gera___ref_copied(value.allocation);
    gera___ref_deleted(result.allocation);
    return value;
}

GeraString gera_std_io_result_get_str(GeraObject result) {
    IoResultLayout* data = (IoResultLayout*) result.allocation->data;
    GeraString value = *((GeraString*) data->data);
    gera___ref_copied(value.allocation);
    gera___ref_deleted(result.allocation);
    return value;
}

GeraArray gera_std_io_result_get_str_arr(GeraObject result) {
    IoResultLayout* data = (IoResultLayout*) result.allocation->data;
    GeraArray value = *((GeraArray*) data->data);
    gera___ref_copied(value.allocation);
    gera___ref_deleted(result.allocation);
    return value;
}


static void free_string_array(GeraAllocation* allocation) {
    gera___begin_read(allocation);
    GeraString* items = (GeraString*) allocation->data;
    size_t length = allocation->size / sizeof(GeraString);
    for(size_t i = 0; i < length; i += 1) {
        gera___ref_deleted(items[i].allocation);
    }
    gera___end_read(allocation);
}


#ifdef _WIN32
    #include <windows.h>

    
    #define GET_CURRENT_ERROR(dest_var) \
        LPVOID dest_var; \
        FormatMessage( \
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, \
            NULL, GetLastError(), 0, (LPSTR) &dest_var, 0, NULL \
        );


    GeraObject gera_std_io_set_cwd(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        if(!SetCurrentDirectoryA(path_nt)) {
            GET_CURRENT_ERROR(error_reason);
            return create_io_result_err(error_reason);
        }
        return create_io_result_empty();
    }


    gbool gera_std_io_file_exists(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        return GetFileAttributesA(path_nt) != INVALID_FILE_ATTRIBUTES;
    }

    GeraObject gera_std_io_canonicalize(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        DWORD path_length = GetFullPathNameA(path_nt, 0, NULL, NULL);
        if(path_length == 0) {
            GET_CURRENT_ERROR(error_reason);
            return create_io_result_err(error_reason);
        }
        GeraAllocation* alloc = gera___alloc(
            path_length, &gera___free_nothing
        );
        if(GetFullPathNameA(path_nt, path_length, alloc->data, NULL) == 0) {
            GET_CURRENT_ERROR(error_reason);
            return create_io_result_err(error_reason);
        }
        size_t length_bytes = path_length - 1;
        size_t length = 0;
        for(size_t i = 0; i < length_bytes; length += 1) {
            i += gera___codepoint_size(alloc->data[i]);
        }
        return create_io_result_str((GeraString) {
            .allocation = alloc,
            .data = alloc->data,
            .length_bytes = length_bytes,
            .length = length
        });
    }


    gbool gera_std_io_is_dir(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        DWORD attr = GetFileAttributesA(path_nt);
        if(attr == INVALID_FILE_ATTRIBUTES) { return 0; }
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    GeraObject gera_std_io_create_dir(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        if(!CreateDirectoryA(path_nt, NULL)) {
            GET_CURRENT_ERROR(error_reason);
            return create_io_result_err(error_reason);
        }
        return create_io_result_empty();
    }

    GeraObject gera_std_io_read_dir(GeraString path) {
        gbool path_ends_with_sep = path.data[path.length_bytes - 1] == '\\';
        size_t path_length = path.length_bytes + (path_ends_with_sep? 3 : 4);
        char path_nt[path_length + 1];
        memcpy(path_nt, path.data, path.length_bytes);
        strcpy(path_nt + path.length_bytes, path_ends_with_sep? "*.*":"\\*.*");
        gera___ref_deleted(path.allocation);
        WIN32_FIND_DATAA entry;
        HANDLE dir = FindFirstFileA(path_nt, &entry);
        if(dir == INVALID_HANDLE_VALUE) {
            GET_CURRENT_ERROR(error_reason);
            return create_io_result_err(error_reason);
        }
        size_t item_count = 0;
        do {
            if(strcmp(entry.cFileName, ".") == 0
                || strcmp(entry.cFileName, "..") == 0) { continue; }
            item_count += 1;
        } while(FindNextFileA(dir, &entry) != 0);
        FindClose(dir);
        dir = FindFirstFileA(path_nt, &entry);
        if(dir == INVALID_HANDLE_VALUE) {
            GET_CURRENT_ERROR(error_reason);
            return create_io_result_err(error_reason);
        }
        GeraAllocation* alloc = gera___alloc(
            sizeof(GeraString) * item_count, &free_string_array
        );
        GeraString* items = (GeraString*) alloc->data;
        size_t item_idx = 0;
        do {
            if(strcmp(entry.cFileName, ".") == 0
                || strcmp(entry.cFileName, "..") == 0) { continue; }
            if(item_idx >= item_count) { break; }
            items[item_idx] = gera___alloc_string(entry.cFileName);
            item_idx += 1;
        } while(FindNextFileA(dir, &entry) != 0);
        FindClose(dir);     
        return create_io_result_str_arr((GeraArray) {
            .allocation = alloc,
            .length = item_count
        });
    }

    GeraObject gera_std_io_delete_dir(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        if(!RemoveDirectoryA(path_nt)) {
            GET_CURRENT_ERROR(error_reason);
            return create_io_result_err(error_reason);
        }
        return create_io_result_empty();
    }


    gbool gera_std_io_is_file(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        DWORD attr = GetFileAttributesA(path_nt);
        if(attr == INVALID_FILE_ATTRIBUTES) { return 0; }
        return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }
#else
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <dirent.h>
    

    GeraObject gera_std_io_set_cwd(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        if(chdir(path_nt) == -1) {
            return create_io_result_err(strerror(errno));
        }
        return create_io_result_empty();
    }


    gbool gera_std_io_file_exists(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        struct stat st;
        return stat(path_nt, &st) != -1;
    }

    GeraObject gera_std_io_canonicalize(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        char* absolute_path_nt = realpath(path_nt, NULL);
        if(absolute_path_nt == NULL) {
            return create_io_result_err(strerror(errno));
        }
        GeraString absolute_path = gera___alloc_string(absolute_path_nt);
        free(absolute_path_nt);
        return create_io_result_str(absolute_path);
    }


    gbool gera_std_io_is_dir(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        struct stat st;
        if(stat(path_nt, &st) == -1) { return 0; }
        return S_ISDIR(st.st_mode);
    }

    static const mode_t DEFAULT_DIR_MODE = S_IRUSR | S_IWUSR | S_IXUSR
        | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

    GeraObject gera_std_io_create_dir(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        struct stat st;
        if(stat(path_nt, &st) == -1) {
            if(mkdir(path_nt, DEFAULT_DIR_MODE) == -1) {
                return create_io_result_err(strerror(errno));
            }
        }
        return create_io_result_empty();
    }

    GeraObject gera_std_io_read_dir(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        DIR* dir = opendir(path_nt);
        struct dirent* entry;
        size_t item_count = 0;
        if(dir == NULL) {
            return create_io_result_err(strerror(errno));
        }
        while ((entry = readdir(dir)) != NULL) {
            if(strcmp(entry->d_name, ".") == 0
                || strcmp(entry->d_name, "..") == 0) { continue; }
            item_count += 1;
        }
        closedir(dir);
        dir = opendir(path_nt);
        if(dir == NULL) {
            return create_io_result_err(strerror(errno));
        }
        GeraAllocation* alloc = gera___alloc(
            sizeof(GeraString) * item_count, &free_string_array
        );
        GeraString* items = (GeraString*) alloc->data;
        size_t item_idx = 0;
        while ((entry = readdir(dir)) != NULL) {
            if(strcmp(entry->d_name, ".") == 0
                    || strcmp(entry->d_name, "..") == 0) { continue; }
            if(item_idx >= item_count) { break; }
            items[item_idx] = gera___alloc_string(entry->d_name);
            item_idx += 1;
        }
        closedir(dir);
        return create_io_result_str_arr((GeraArray) {
            .allocation = alloc,
            .length = item_count
        });
    }

    GeraObject gera_std_io_delete_dir(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        if(rmdir(path_nt) == -1) {
            return create_io_result_err(strerror(errno));
        }
        return create_io_result_empty();
    }


    gbool gera_std_io_is_file(GeraString path) {
        GERA_STRING_NULL_TERM(path, path_nt);
        gera___ref_deleted(path.allocation);
        struct stat st;
        if(stat(path_nt, &st) == -1) { return 0; }
        return S_ISREG(st.st_mode);
    }
#endif

GeraObject gera_std_io_write_file(GeraString content, GeraString path) {
    GERA_STRING_NULL_TERM(content, content_nt);
    GERA_STRING_NULL_TERM(path, path_nt);
    gera___ref_deleted(content.allocation);
    gera___ref_deleted(path.allocation);
    FILE* dest = fopen(path_nt, "w");
    if(dest == NULL) {
        return create_io_result_err(strerror(errno));
    }
    fputs(content_nt, dest);
    fclose(dest);
    return create_io_result_empty();
}

GeraObject gera_std_io_read_file(GeraString path) {
    GERA_STRING_NULL_TERM(path, path_nt);
    gera___ref_deleted(path.allocation);
    FILE* src = fopen(path_nt, "r");
    if(src == NULL) {
        return create_io_result_err(strerror(errno));
    }
    fseek(src, 0, SEEK_END);
    size_t length_bytes = ftell(src);
    fseek(src, 0, SEEK_SET);
    GeraAllocation* alloc = gera___alloc(length_bytes, NULL);
    fread(alloc->data, 1, length_bytes, src);
    fclose(src);
    size_t length = 0;
    for(size_t i = 0; i < length_bytes; length += 1) {
        i += gera___codepoint_size(alloc->data[i]);
    }
    return create_io_result_str((GeraString) {
        .allocation = alloc,
        .data = alloc->data,
        .length_bytes = length_bytes,
        .length = length
    });
}

GeraObject gera_std_io_delete_file(GeraString path) {
    GERA_STRING_NULL_TERM(path, path_nt);
    gera___ref_deleted(path.allocation);
    if(remove(path_nt) == -1) {
        return create_io_result_err(strerror(errno));
    }
    return create_io_result_empty();
}