
#include <gera.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"
#include "prc.h"

void gera_std_prc_exit(gint code) {
    exit((int) code);
}


#define GET_HANDLE_ID(closure) \
    GERA_CLOSURE_CALL_NOARGS(closure, GERA_CLOSURE_FPTR_NOARGS(closure, gint))

typedef struct {
    gbool is_finished;
    gint exit_code;
} ProcessStatusLayout;

static gbool HAS_PROCESS_STORAGE = 0;
static Storage PROCESS_STORAGE;

gint process_handle_get(GeraAllocation* captures) {
    return *((gint*) captures->data);
}

#ifdef _WIN32
    #include <windows.h>
    #include <string.h>

    typedef struct PIPE {
        HANDLE read;
        HANDLE write;
    } PIPE;
    
    void init_pipe(PIPE* p) {
        SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
        if(!CreatePipe(&p->read, &p->write, &sa, 0)) {
            gera___panic("Unable to create a pipe!");
        }
    }

    typedef struct ProcessEntry {
        PROCESS_INFORMATION pi;
        gbool is_finished;
        gint exit_code;
        GeraAllocation* handle_allocation;
        MUTEX mutex;
        PIPE stdout_pipe;
        PIPE stderr_pipe;
        PIPE stdin_pipe;
    } ProcessEntry;

    void process_handle_free(GeraAllocation* allocation) {
        size_t id = *((size_t*) allocation->data);
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        mutex_free(&process_entry->mutex);
        CloseHandle(process_entry->pi.hProcess);
        CloseHandle(process_entry->pi.hThread);
        CloseHandle(process_entry->stdout_pipe.read);
        CloseHandle(process_entry->stdout_pipe.write);
        CloseHandle(process_entry->stderr_pipe.read);
        CloseHandle(process_entry->stderr_pipe.write);
        CloseHandle(process_entry->stdin_pipe.read);
        CloseHandle(process_entry->stdin_pipe.write);
        storage_remove(&PROCESS_STORAGE, id);
    }

    GeraClosure gera_std_prc_run(GeraString program, GeraArray args) {
        init_process_storage();
        size_t id = storage_insert(&PROCESS_STORAGE, NULL);
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        process_entry->is_finished = 0;
        process_entry->exit_code = -1;
        process_entry->mutex = mutex_create();
        init_pipe(&process_entry->stdout_pipe);
        init_pipe(&process_entry->stderr_pipe);
        init_pipe(&process_entry->stdin_pipe);
        ZeroMemory(&process_entry->pi, sizeof(process_entry->pi));
        gera___begin_read(args.allocation);
        GeraString* args_str = (GeraString*) args.allocation->data;
        size_t command_length = program.length_bytes;
        for(size_t arg_idx = 0; arg_idx < args.length; arg_idx += 1) {
            command_length += 1 + args_str[arg_idx].length_bytes;
        }
        char command[command_length + 1];
        memcpy(command, program.data, program.length_bytes);
        size_t arg_offset = program.length_bytes;
        for(size_t arg_idx = 0; arg_idx < args.length; arg_idx += 1) {
            command[arg_offset] = ' ';
            arg_offset += 1;
            GeraString arg = args_str[arg_idx];
            memcpy(command + arg_offset, arg.data, arg.length_bytes);
            arg_offset += arg.length_bytes;
        }
        gera___end_read(args.allocation);
        STARTUPINFO si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdOutput = process_entry->stdout_pipe.write;
        si.hStdError = process_entry->stderr_pipe.write;
        si.hStdInput = process_entry->stdin_pipe.read;
        si.dwFlags |= STARTF_USESTDHANDLES;
        command[command_length] = '\0';
        if (!CreateProcessA(
            NULL,        // Application name
            command,
            NULL,        // Process handle not inheritable
            NULL,        // Thread handle not inheritable
            TRUE,        // Inherit handles we just configured
            0,           // No creation flags
            NULL,        // Use parent's environment block
            NULL,        // Use parent's starting directory
            &si,
            &process_entry->pi
        )) {
            DWORD err = GetLastError();
            if(err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                process_entry->is_finished = 1;
                process_entry->exit_code = 1;
            } else {
                gera___panic("Unable to create a child process!");
            }
        }
        GeraAllocation* allocation = gera___alloc(
            sizeof(size_t), &process_handle_free
        );
        *((size_t*) allocation->data) = id;
        process_entry->handle_allocation = allocation;
        gera___ref_deleted(program.allocation);
        gera___ref_deleted(args.allocation);
        return (GeraClosure) {
            .body = &process_handle_get,
            .allocation = allocation
        };
    }

    void gera_std_prc_kill(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return;
        }
        TerminateProcess(process_entry->pi.hProcess, 0);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
    }

    void gera_std_prc_await(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return;
        }
        if(WaitForSingleObject(
            process_entry->pi.hProcess, INFINITE
        ) != WAIT_OBJECT_0) {
            gera___panic("Unable to await process completion!");
        }
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
    }

    static void wait_for_pipe(PIPE* pipe) {
        // I absolutely hate this, but it should work
        for(;;) {
            DWORD bytes_available;
            if(!PeekNamedPipe(
                pipe->read, NULL, 0, NULL, &bytes_available, NULL
            )) {
                gera___panic("Unable to read from pipe!");
            }
            if(bytes_available > 0) { break; }
            Sleep(5);
        }
    }

    void gera_std_prc_await_stdout(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return;
        }
        wait_for_pipe(&process_entry->stdout_pipe);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
    }

    void gera_std_prc_await_stderr(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return;
        }
        wait_for_pipe(&process_entry->stderr_pipe);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
    }

    static GeraString read_string_from_pipe(PIPE* pipe) {
        DWORD bytes_available;
        if(!PeekNamedPipe(pipe->read, NULL, 0, NULL, &bytes_available, NULL)) {
            gera___panic("Unable to read from pipe!");
        }
        if(bytes_available == 0) {
            return (GeraString) {
                .allocation = NULL,
                .data = "",
                .length = 0,
                .length_bytes = 0
            };
        }
        size_t length_bytes = bytes_available;
        GeraAllocation* allocation = gera___alloc(
            length_bytes, NULL
        );
        DWORD bytes_read;
        if(!ReadFile(
            pipe->read, allocation->data, length_bytes, &bytes_read, NULL
        )) {
            gera___panic("Unable to read from pipe!");
        }
        size_t length = 0;
        for(size_t o = 0; o < length_bytes; length += 1) {
            o += gera___codepoint_size(allocation->data[o]);
        }
        return (GeraString) {
            .allocation = allocation,
            .data = allocation->data,
            .length = length,
            .length_bytes = length_bytes
        };
    }

    GeraString gera_std_prc_read_stdout(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        GeraString r = read_string_from_pipe(&process_entry->stdout_pipe);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
        return r;
    }

    GeraString gera_std_prc_read_stderr(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        GeraString r = read_string_from_pipe(&process_entry->stderr_pipe);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
        return r;
    }

    void gera_std_prc_write_stdin(
        GeraClosure process_handle, GeraString text
    ) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            gera___ref_deleted(text.allocation);
            return;
        }
        DWORD bytes_written;
        if(!WriteFile(
            process_entry->stdin_pipe.write, text.data, text.length_bytes,
            &bytes_written, NULL
        )) {
            gera___panic("Unable to write to pipe!");
        }
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
        gera___ref_deleted(text.allocation);
    }

    GeraObject gera_std_prc_status(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            gint exit_code = process_entry->exit_code;
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            GeraAllocation* ra = gera___alloc(
                sizeof(ProcessStatusLayout), NULL
            );
            *((ProcessStatusLayout*) ra->data) = (ProcessStatusLayout) {
                .is_finished = 1,
                .exit_code = exit_code
            };
            return (GeraObject) { .allocation = ra };
        }
        DWORD exit_code;
        if(!GetExitCodeProcess(process_entry->pi.hProcess, &exit_code)) {
            if(exit_code != STILL_ACTIVE) {
                gera___panic("Unable to get the process exit code!");
            }
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return (ProcessStatus) {
                .is_finished = 0,
                .exit_code = -1
            };
        }
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
        GeraAllocation* ra = gera___alloc(sizeof(ProcessStatusLayout), NULL);
        *((ProcessStatusLayout*) ra->data) = (ProcessStatusLayout) {
            .is_finished = exit_code != STILL_ACTIVE,
            .exit_code = exit_code
        };
        return (GeraObject) { .allocation = ra };
    }

#else
    #include <sys/wait.h>
    #include <sys/select.h>
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

    void process_handle_free(GeraAllocation* allocation) {
        size_t id = *((size_t*) allocation->data);
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

    GeraClosure gera_std_prc_run(GeraString program, GeraArray args) {
        GERA_STRING_NULL_TERM(program, program_nt);
        gera___begin_read(args.allocation);
        GeraString* args_gs = (GeraString*) args.allocation->data;
        char* args_nt[args.length + 2];
        args_nt[0] = program_nt;
        for(size_t ai = 0; ai < args.length; ai += 1) {
            char* arg_nt = malloc(args_gs[ai].length_bytes + 1);
            memcpy(arg_nt, args_gs[ai].data, args_gs[ai].length_bytes);
            arg_nt[args_gs[ai].length_bytes] = '\0';
            args_nt[ai + 1] = arg_nt;
        }
        gera___end_read(args.allocation);
        int stdout_pipe[2];
        int stderr_pipe[2];
        int stdin_pipe[2];
        if(pipe(stdout_pipe) == -1
            || pipe(stderr_pipe) == -1
            || pipe(stdin_pipe) == -1
        ) {
            gera___panic("Unable to initialize pipes!");
        }
        if(fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK) == -1
            || fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK) == -1
        ) {
            gera___panic("Unable to make a pipe non-blocking!");
        }
        pid_t child_os_id = fork();
        if(child_os_id == -1) {
            gera___panic("Unable to create a fork of the current process!");
        }
        if(child_os_id == 0) {
            // child process continues executing here
            args_nt[args.length + 1] = NULL;
            close(stdout_pipe[0]); // doesn't read
            dup2(stdout_pipe[1], STDOUT_FILENO); // redirect stdout to write
            close(stderr_pipe[0]); // doesn't read
            dup2(stderr_pipe[1], STDERR_FILENO); // redirect stderr to write
            close(stdin_pipe[1]); // doesn't write
            dup2(stdin_pipe[0], STDIN_FILENO); // redirect read to stdin
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
        GeraAllocation* allocation = gera___alloc(
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
        gera___ref_deleted(program.allocation);
        gera___ref_deleted(args.allocation);
        return (GeraClosure) {
            .body = &process_handle_get,
            .allocation = allocation
        };
    }

    void gera_std_prc_kill(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return;
        }
        if(kill(process_entry->os_id, SIGTERM) != 0) {
            gera___panic("Unable to terminate child process!");
        }    
        int status;
        pid_t result = waitpid(process_entry->os_id, &status, 0);
        update_status(process_entry, status, result);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
    }

    void gera_std_prc_await(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return;
        }
        int status;
        pid_t result = waitpid(process_entry->os_id, &status, 0);
        update_status(process_entry, status, result);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
    }

    void gera_std_prc_await_stdout(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return;
        }
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(process_entry->stdout_pipe[0], &read_fds);
        select(process_entry->stdout_pipe[0] + 1, &read_fds, NULL, NULL, NULL);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
    }

    void gera_std_prc_await_stderr(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            return;
        }
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(process_entry->stderr_pipe[0], &read_fds);
        select(process_entry->stderr_pipe[0] + 1, &read_fds, NULL, NULL, NULL);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
    }

    static GeraString read_string_from_pipe(int pipe_read) {
        size_t buffer_size = 1024;
        char* buffer = (char*) malloc(buffer_size);
        if(buffer == NULL) {
            gera___panic("Unable to allocate buffer!");
        }
        size_t total_read = 0;
        for(;;) {
            size_t remaining_space = buffer_size - total_read;
            ssize_t bytes_read = read(
                pipe_read, buffer + total_read, remaining_space
            );
            if(bytes_read == -1) { break; }
            total_read += bytes_read;
            if(bytes_read < remaining_space) { break; }
            if(total_read < buffer_size) { continue; }
            buffer_size *= 2;
            buffer = (char*) realloc(buffer, buffer_size);
            if(buffer == NULL) {
                gera___panic("Unable to reallocate buffer!");
            }
        }
        GeraAllocation* allocation = gera___alloc(
            total_read, NULL
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

    GeraString gera_std_prc_read_stdout(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        GeraString r = read_string_from_pipe(process_entry->stdout_pipe[0]);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
        return r;
    }

    GeraString gera_std_prc_read_stderr(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        GeraString r = read_string_from_pipe(process_entry->stderr_pipe[0]);
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
        return r;
    }

    void gera_std_prc_write_stdin(
        GeraClosure process_handle, GeraString text
    ) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(process_entry->is_finished) {
            mutex_unlock(&process_entry->mutex);
            gera___ref_deleted(process_handle.allocation);
            gera___ref_deleted(text.allocation);
            return;
        }
        if(write(
            process_entry->stdin_pipe[1], text.data, text.length_bytes
        ) == -1) {
            gera___panic("Unable to write to pipe!");
        }
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
        gera___ref_deleted(text.allocation);
    }

    GeraObject gera_std_prc_status(GeraClosure process_handle) {
        init_process_storage();
        gint sid = GET_HANDLE_ID(process_handle);
        size_t id = *((size_t*) &sid);
        if(id >= PROCESS_STORAGE.data_length) {
            gera___panic("The provided process handle is invalid!");
        }
        ProcessEntry* process_entry = storage_get(&PROCESS_STORAGE, id);
        if(process_entry->handle_allocation != process_handle.allocation) {
            gera___panic("The provided process handle is invalid!");
        }
        mutex_lock(&process_entry->mutex);
        if(!process_entry->is_finished) {
            int status;
            pid_t result = waitpid(process_entry->os_id, &status, WNOHANG);
            update_status(process_entry, status, result);
        }
        GeraAllocation* ra = gera___alloc(sizeof(ProcessStatusLayout), NULL);
        *((ProcessStatusLayout*) ra->data) = (ProcessStatusLayout) {
            .is_finished = process_entry->is_finished,
            .exit_code = process_entry->exit_code
        };
        GeraObject r = (GeraObject) { .allocation = ra };
        mutex_unlock(&process_entry->mutex);
        gera___ref_deleted(process_handle.allocation);
        return r;
    }
#endif

void init_process_storage() {
    if(HAS_PROCESS_STORAGE) { return; }
    PROCESS_STORAGE = storage_create(sizeof(ProcessEntry));
    HAS_PROCESS_STORAGE = 1;
}