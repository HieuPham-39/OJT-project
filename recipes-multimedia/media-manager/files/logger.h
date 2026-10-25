#ifndef AUTOMOTIVE_LOGGER_H
#define AUTOMOTIVE_LOGGER_H

#include <stdio.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_SAFETY,
    LOG_LEVEL_FSM
} LogLevel_t;

void logger_init(void);
void logger_log(LogLevel_t level, const char *module, const char *fmt, ...);

#define LOG_D(mod, ...) logger_log(LOG_LEVEL_DEBUG, mod, __VA_ARGS__)
#define LOG_I(mod, ...) logger_log(LOG_LEVEL_INFO, mod, __VA_ARGS__)
#define LOG_W(mod, ...) logger_log(LOG_LEVEL_WARN, mod, __VA_ARGS__)
#define LOG_E(mod, ...) logger_log(LOG_LEVEL_ERROR, mod, __VA_ARGS__)
#define LOG_S(mod, ...) logger_log(LOG_LEVEL_SAFETY, mod, __VA_ARGS__)
#define LOG_FSM(mod, ...) logger_log(LOG_LEVEL_FSM, mod, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* AUTOMOTIVE_LOGGER_H */
