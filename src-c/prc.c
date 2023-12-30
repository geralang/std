
#include <gera.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"
#include "prc.h"

void gera_std_prc_exit(gint code) {
    exit((int) code);
}


typedef GERA_CLOSURE_NOARGS(gint) ProcessHandle;

typedef struct ProcessExitCodeResult {
    gbool has_error;
    gbool has_value;
    GeraString error;
    gint value;
} ProcessExitCodeResult;

typedef struct ProcessStatus {
    gbool is_finished;
    gint exit_code;
} ProcessStatus;

static gbool HAS_PROCESS_STORAGE = 0;
static Storage PROCESS_STORAGE;

gint process_handle_get(GeraAllocation* captures) {
    return *((gint*) captures->data);
}

#ifdef _WIN32


#else
    #include <sys/wait.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <signal.h>

    typedef struct ProcessEntry {
        pid_t os_id;
        gbool is_finished;
        gint exit_code;
        GeraAllocation* handle_allocation;
        MUTEX mutex;
        int stdout_pipe[2];
        int stderr_pipe[2];
        int stdin_pipe[2];
    } ProcessEntry;

    void process_handle_free(char* data, size_t size) {
        size_t id = *((size_t*) data);
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        mutex_free(&process_entry->mutex);
        close(process_entry->stdout_pipe[0]); // parent reads - close
        close(process_entry->stderr_pipe[0]); // parent reads - close
        close(process_entry->stdin_pipe[1]); // parent writes - close
        storage_remove(&PROCESS_STORAGE, id);
    }

    static void update_status(ProcessEntry* process_entry, int status, pid_t result) {
        if(result == 0) {
            // // default values
            // process_entry->is_finished = 0;
            // process_entry->exit_code = -1;
        } else if(result == process_entry->os_id) {
            process_entry->is_finished = 1;
            if(WIFEXITED(status)) {
                process_entry->exit_code = WEXITSTATUS(status);
            } else {
                process_entry->exit_code = -1;
            }
        } else {
            gera___panic("Unable to update status of child process!");
        }
    }

    ProcessHandle gera_std_prc_run(GeraString program, GeraArray args) {
        int stdout_pipe[2];
        int stderr_pipe[2];
        int stdin_pipe[2];
        if(pipe(stdout_pipe) == -1
            || pipe(stderr_pipe) == -1
            || pipe(stdin_pipe) == -1
        ) {
            gera___panic("Unable to initialize pipes!");
        }
        pid_t child_os_id = fork();
        if(child_os_id == -1) {
            gera___panic("Unable to create a fork of the current process!");
        }
        if(child_os_id == 0) {
            // child process continues executing here
            close(stdout_pipe[0]); // doesn't read
            dup2(stdout_pipe[1], STDOUT_FILENO); // redirect stdout to write
            close(stderr_pipe[0]); // doesn't read
            dup2(stderr_pipe[1], STDERR_FILENO); // redirect stderr to write
            close(stdin_pipe[1]); // doesn't write
            dup2(stdin_pipe[0], STDIN_FILENO); // redirect read to stdin
            GERA_STRING_NULL_TERM(program, program_nt);
            GeraString* args_gs = (GeraString*) args.data;
            char* args_nt[args.length + 2];
            args_nt[0] = program_nt;
            for(size_t ai = 0; ai < args.length; ai += 1) {
                char* arg_nt = malloc(args_gs[ai].length_bytes + 1);
                memcpy(arg_nt, args_gs[ai].data, args_gs[ai].length_bytes);
                arg_nt[args_gs[ai].length_bytes] = '\0';
                args_nt[ai + 1] = arg_nt;
            }
            args_nt[args.length + 1] = NULL;
            execvp(program_nt, args_nt);
            for(size_t ai = 0; ai < args.length; ai += 1) {
                free(args_nt[ai + 1]);
            }
            exit(EXIT_FAILURE);
        }
        // parent process continues executing here
        close(stdout_pipe[1]); // doesn't write
        close(stderr_pipe[1]); // doesn't write
        close(stdin_pipe[0]); // doesn't read
        init_process_storage();
        GeraAllocation* allocation = gera___rc_alloc(
            sizeof(size_t), &process_handle_free
        );
        ProcessEntry process_entry = (ProcessEntry) {
            .os_id = child_os_id,
            .handle_allocation = allocation,
            .is_finished = 0,
            .exit_code = -1,
            .mutex = mutex_create()
        };
        memcpy(process_entry.stdout_pipe, stdout_pipe, sizeof(int) * 2);
        memcpy(process_entry.stderr_pipe, stderr_pipe, sizeof(int) * 2);
        memcpy(process_entry.stdin_pipe, stdin_pipe, sizeof(int) * 2);
        size_t id = storage_insert(&PROCESS_STORAGE, &process_entry);
        *((size_t*) allocation->data) = id;
        return (ProcessHandle) {
            .call = &process_handle_get,
            .captures = allocation
        };
    }

    void gera_std_prc_kill(ProcessHandle process_handle) {
        init_process_storage();
        gint sid = process_handle.call(process_handle.captures);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.captures) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            return;
        }
        if(kill(process_entry->os_id, SIGTERM) != 0) {
            gera___panic("Unable to terminate child process!");
        }    
        int status;
        pid_t result = waitpid(process_entry->os_id, &status, 0);
        update_status(process_entry, status, result);
        mutex_unlock(&process_entry->mutex);
    }

    void gera_std_prc_await(ProcessHandle process_handle) {
        init_process_storage();
        gint sid = process_handle.call(process_handle.captures);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.captures) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            return;
        }
        int status;
        pid_t result = waitpid(process_entry->os_id, &status, 0);
        update_status(process_entry, status, result);
        mutex_unlock(&process_entry->mutex);
    }

    static GeraString read_string_from_pipe(int pipe_read) {
        if(fcntl(pipe_read, F_SETFL, O_NONBLOCK) == -1) {
            gera___panic("Unable to make a pipe non-blocking!");
        }
        size_t buffer_size = 1024;
        char* buffer = malloc(buffer_size);
        if(buffer == NULL) {
            gera___panic("Unable to allocate buffer!");
        }
        size_t total_read = 0;
        for(;;) {
            size_t remaining_space = buffer_size - total_read;
            ssize_t bytes_read = read(
                pipe_read, buffer + total_read, remaining_space
            );
            total_read += bytes_read;
            if(bytes_read < remaining_space) { break; }
            if(total_read < buffer_size) { continue; }
            buffer_size *= 2;
            buffer = realloc(buffer, buffer_size);
        }
        GeraAllocation* allocation = gera___rc_alloc(
            total_read, &gera___free_nothing
        );
        memcpy(allocation->data, buffer, total_read);
        free(buffer);
        size_t length = 0;
        for(size_t o = 0; o < total_read; length += 1) {
            o += gera___codepoint_size(allocation->data[o]);
        }
        return (GeraString) {
            .allocation = allocation,
            .data = allocation->data,
            .length_bytes = total_read,
            .length = length
        };
    }

    GeraString gera_std_prc_read_stdout(ProcessHandle process_handle) {
        init_process_storage();
        gint sid = process_handle.call(process_handle.captures);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.captures) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        GeraString r = read_string_from_pipe(process_entry->stdout_pipe[0]);
        mutex_unlock(&process_entry->mutex);
        return r;
    }

    GeraString gera_std_prc_read_stderr(ProcessHandle process_handle) {
        init_process_storage();
        gint sid = process_handle.call(process_handle.captures);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.captures) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        GeraString r = read_string_from_pipe(process_entry->stderr_pipe[0]);
        mutex_unlock(&process_entry->mutex);
        return r;
    }

    void gera_std_prc_write_stdin(
        ProcessHandle process_handle, GeraString text
    ) {
        init_process_storage();
        gint sid = process_handle.call(process_handle.captures);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.captures) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(write(
            process_entry->stdin_pipe[1], text.data, text.length_bytes
        ) == -1) {
            gera___panic("Unable to write to pipe!");
        }
        mutex_unlock(&process_entry->mutex);
    }

    ProcessStatus gera_std_prc_status(ProcessHandle process_handle) {
        init_process_storage();
        gint sid = process_handle.call(process_handle.captures);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.captures) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(!process_entry->is_finished) {
            int status;
            pid_t result = waitpid(process_entry->os_id, &status, WNOHANG);
            update_status(process_entry, status, result);
        }
        ProcessStatus r = (ProcessStatus) {
            .is_finished = process_entry->is_finished,
            .exit_code = process_entry->exit_code
        };
        mutex_unlock(&process_entry->mutex);
        return r;
    }
#endif

void init_process_storage() {
    if(HAS_PROCESS_STORAGE) { return; }
    PROCESS_STORAGE = storage_create(sizeof(ProcessEntry));
    HAS_PROCESS_STORAGE = 1;
}