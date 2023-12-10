
#include "gera.h"
#include <string.h>


#ifdef _WIN32
    #include <windows.h>
    #define set_env_var(name, value) SetEnvironmentVariable(name, value)
    #define delete_env_var(name) SetEnvironmentVariable(name, NULL)
#else
    #include <unistd.h>
    #define set_env_var(name, value) setenv(name, value, 1) == 0
    #define delete_env_var(name) unsetenv(name)
#endif


gint gera_std_env_argc() {
    return (gint) GERA_ARGC;
}

GeraString gera_std_env_argv(gint index) {
    if(index < 0 || index > GERA_ARGC) { gera___panic("index is invalid"); } 
    char* argv = GERA_ARGV[index];
    size_t length_bytes = 0;
    size_t length = 0;
    for(size_t i = 0;;) {
        char c = argv[i];
        if(c == '\0') { break; }
        size_t s = gera___codepoint_size(c);
        length_bytes += s;
        length += 1;
        i += s;
    }
    return (GeraString) {
        .allocation = NULL,
        .length_bytes = length_bytes,
        .length = length,
        .data = argv
    };
}


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
    if(!has) { gera___panic("environment variable does not exist"); }
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