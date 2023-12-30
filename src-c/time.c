
#include <gera.h>

#ifdef _WIN32
    #include <windows.h>

    static const gint windows_to_unix_epoch = (gint) (1601 - 1970)
        * 365.25
        * 24
        * 60
        * 60
        * 1e7;

    gint std_time_utc_unix_millis() {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        return (uli.QuadPart + windows_to_unix_epoch) / 10000;
    }

    gint std_time_local_unix_millis() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        FILETIME ft;
        SystemTimeToFileTime(&st, &ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        return (uli.QuadPart + windows_to_unix_epoch) / 10000;
    }
#else
    #include <sys/time.h>
    #include <time.h>

    gint std_time_utc_unix_millis() {
        struct timeval utv;
        gettimeofday(&utv, NULL);
        return (gint) utv.tv_sec * 1000 + utv.tv_usec / 1000;
    }

    gint std_time_local_unix_millis() {
        struct timeval utv;
        gettimeofday(&utv, NULL);
        struct tm* lt = localtime(&utv.tv_sec);
        gint ls;
        ls = lt->tm_year - 70;
        ls = ls * 365.25 + lt->tm_yday;
        ls = ls * 24 + lt->tm_hour;
        ls = ls * 60 + lt->tm_min;
        ls = ls * 60 + lt->tm_sec;
        return (gint) ls * 1000 + ((gint) utv.tv_usec / 1000);
    }
#endif