
#include <gera.h>
#include <string.h>
#include <stdlib.h>


static void free_string_array(GeraAllocation* allocation) {
    gera___begin_read(allocation);
    GeraString* items = (GeraString*) allocation->data;
    size_t length = allocation->size / sizeof(GeraString);
    for(size_t i = 0; i < length; i += 1) {
        gera___ref_deleted(items[i].allocation);
    }
    gera___end_read(allocation);
}


GeraArray gera_std_env_args() {
    size_t buffer_size = GERA_ARGS.length * sizeof(GeraString);
    GeraAllocation* alloc = gera___alloc(buffer_size, &free_string_array);
    memcpy(alloc->data, GERA_ARGS.allocation->data, buffer_size);
    return (GeraArray) {
        .allocation = alloc,
        .length = GERA_ARGS.length
    };
}


#ifdef _WIN32
    #include <windows.h>
    
    #define set_env_var(name, value) SetEnvironmentVariable(name, value)
    #define delete_env_var(name) SetEnvironmentVariable(name, NULL)

    GeraArray gera_std_env_vars() {
        LPCH vars_block = GetEnvironmentStringsA();
        if(vars_block == NULL) {
            gera___panic("Unable to access environment variables!");
        }
        size_t var_count = 0;
        for(LPSTR var = (LPSTR) vars_block; *var != '\0'; var += lstrlenA(var) + 1) {
            var_count += 1;
        }
        GeraAllocation* alloc = gera___alloc(
            sizeof(GeraString) * var_count, &free_string_array
        );
        GeraString* var_names = (GeraString*) alloc->data;
        size_t result_size = 0;
        LPSTR var_name_nt = (LPSTR) vars_block;
        for(; result_size < var_count; result_size += 1) {
            if(*var_name_nt == '\0') { break; }    
            size_t var_length = 0;
            size_t var_length_bytes = 0;
            for(;; var_length += 1) {
                char c = var_name_nt[var_length_bytes];
                if(c == '=') { break; }
                if(c == '\0') { break; }
                var_length_bytes += gera___codepoint_size(c);
            }
            GeraAllocation* var_alloc = gera___alloc(
                var_length_bytes, NULL
            );
            memcpy(var_alloc->data, var_name_nt, var_length_bytes);
            var_names[result_size] = (GeraString) {
                .allocation = var_alloc,
                .data = var_alloc->data,
                .length = var_length,
                .length_bytes = var_length_bytes
            };
            var_name_nt += lstrlenA(var_name_nt) + 1;
        }
        FreeEnvironmentStringsA(vars_block);
        return (GeraArray) {
            .allocation = alloc,
            .length = result_size
        };
    }


    gint gera_std_env_run(GeraString command) {
        GERA_STRING_NULL_TERM(command, command_nt);
        gera___ref_deleted(command.allocation);
        STARTUPINFO sinfo;
        ZeroMemory(&sinfo, sizeof(sinfo));
        sinfo.cb = sizeof(sinfo);
        PROCESS_INFORMATION pinfo;
        ZeroMemory(&pinfo, sizeof(pinfo));
        if(!CreateProcess(NULL, command_nt, NULL, NULL, FALSE, 0, NULL, NULL, &sinfo, &pinfo)) {
            gera___panic("Unable to execute system command!");
        }
        WaitForSingleObject(pinfo.hProcess, INFINITE);
        DWORD exit_code;
        GetExitCodeProcess(pinfo.hProcess, &exit_code);
        CloseHandle(pinfo.hProcess);
        CloseHandle(pinfo.hThread);
        return (gint) exit_code;
    }
#else
    #include <unistd.h>
    
    #define set_env_var(name, value) setenv(name, value, 1) == 0
    #define delete_env_var(name) unsetenv(name)

    extern char** environ;

    GeraArray gera_std_env_vars() {
        size_t var_count = 0;
        for(; environ[var_count] != NULL; var_count += 1);
        GeraAllocation* alloc = gera___alloc(
            sizeof(GeraString) * var_count, &free_string_array
        );
        GeraString* var_names = (GeraString*) alloc->data;
        size_t result_size = 0;
        for(; result_size < var_count; result_size += 1) {
            char* var_name_nt = environ[result_size];
            if(var_name_nt == NULL) { break; }
            size_t var_length = 0;
            size_t var_length_bytes = 0;
            for(;; var_length += 1) {
                char c = var_name_nt[var_length_bytes];
                if(c == '=') { break; }
                if(c == '\0') { break; }
                var_length_bytes += gera___codepoint_size(c);
            }
            GeraAllocation* var_alloc = gera___alloc(
                var_length_bytes, NULL
            );
            memcpy(var_alloc->data, var_name_nt, var_length_bytes);
            var_names[result_size] = (GeraString) {
                .allocation = var_alloc,
                .data = var_alloc->data,
                .length = var_length,
                .length_bytes = var_length_bytes
            };
        }
        return (GeraArray) {
            .allocation = alloc,
            .length = result_size
        };
    }


    gint gera_std_env_run(GeraString command) {
        GERA_STRING_NULL_TERM(command, command_nt);
        gera___ref_deleted(command.allocation);
        int exit_code = system(command_nt);
        if(exit_code == -1) { gera___panic("Unable to execute system command!"); }
        return (gint) WEXITSTATUS(exit_code);
    }
#endif

static void get_env_var(GeraString name, gbool* found, GeraString* value) {
    GERA_STRING_NULL_TERM(name, name_nt);
    const char* value_nt = getenv(name_nt);
    if(found != NULL) { *found = value_nt != NULL; }
    if(value_nt == NULL) { return; }
    if(value != NULL) { *value = gera___alloc_string(value_nt); }
}

gbool gera_std_env_has_var(GeraString name) {
    gbool has;
    get_env_var(name, &has, NULL);
    return has;
}

GeraString gera_std_env_get_var(GeraString name) {
    gbool has;
    GeraString value;
    get_env_var(name, &has, &value);
    if(!has) { gera___panic("Environment variable does not exist!"); }
    return value;
}

void gera_std_env_set_var(GeraString value, GeraString name) {
    GERA_STRING_NULL_TERM(value, value_nt);
    GERA_STRING_NULL_TERM(name, name_nt);
    set_env_var(name_nt, value_nt);
}

void gera_std_env_delete_var(GeraString name) {
    GERA_STRING_NULL_TERM(name, name_nt);
    delete_env_var(name_nt);
}


gbool gera_std_env_is_windows() {
    #ifdef _WIN32
        return 1;
    #else
        return 0;
    #endif
}

gbool gera_std_env_is_osx() {
    #ifdef __APPLE__
        #include "TargetConditionals.h"
        #if TARGET_OS_IPHONE
            return 0;
        #else
            return 1;
        #endif
    #else
        return 0;
    #endif
}

gbool gera_std_env_is_ios() {
    #ifdef __APPLE__
        #include "TargetConditionals.h"
        #ifdef TARGET_OS_IPHONE
            return 1;
        #else
            return 0;
        #endif
    #else
        return 0;
    #endif
}

gbool gera_std_env_is_linux() {
    #ifdef __linux__
        return 1;
    #else
        return 0;
    #endif
}

gbool gera_std_env_is_android() {
    #ifdef __ANDROID__
        return 1;
    #else
        return 0;
    #endif
}

gbool gera_std_env_is_unix() {
    #if defined(unix) || defined(__unix__) || defined(__unix)
        return 1;
    #else
        return 0;
    #endif
}