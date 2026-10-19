#include "logger.h"

#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

static pthread_mutex_t s_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_initialized = 0;

void logger_init(void) {
    pthread_mutex_init(&s_log_mutex, NULL);
    s_initialized = 1;
}

void logger_log(LogLevel_t level, const char *module, const char *fmt, ...) {
    pthread_mutex_lock(&s_log_mutex);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_buf;
    localtime_r(&tv.tv_sec, &tm_buf);

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    // Timestamp
    printf("\033[90m[%s.%03ld]\033[0m ", time_str, tv.tv_usec / 1000);

    // Level
    switch (level) {
        case LOG_LEVEL_DEBUG:
            printf("\033[36m[DEBUG ]\033[0m");
            break;
        case LOG_LEVEL_INFO:
            printf("\033[32m[INFO  ]\033[0m");
            break;
        case LOG_LEVEL_WARN:
            printf("\033[33m[WARN  ]\033[0m");
            break;
        case LOG_LEVEL_ERROR:
            printf("\033[31;1m[ERROR ]\033[0m");
            break;
        case LOG_LEVEL_SAFETY:
            printf("\033[35;1m[SAFETY]\033[0m");
            break;
        case LOG_LEVEL_FSM:
            printf("\033[34;1m[FSM   ]\033[0m");
            break;
    }

    // Module tag
    printf(" \033[1m[%s]\033[0m ", module ? module : "CORE");

    // Message
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
    fflush(stdout);

    pthread_mutex_unlock(&s_log_mutex);
}
