#include "media_manager_service.h"
#include "logger.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static MediaManagerService_t *g_service = NULL;

static void handle_signal(int sig) {
    LOG_I("Main-C", "Caught termination signal (%d). Shutting down daemon gracefully...", sig);
    if (g_service) {
        media_manager_service_stop(g_service);
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    logger_init();

    LOG_I("Main-C", "==========================================================");
    LOG_I("Main-C", "  Automotive USB Video Player - Core Service (Pure C)     ");
    LOG_I("Main-C", "==========================================================");

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    g_service = media_manager_service_create();
    if (!g_service) {
        LOG_E("Main-C", "Failed to allocate Media Manager Service");
        return 1;
    }

    if (!media_manager_service_init(g_service)) {
        LOG_E("Main-C", "Failed to initialize Media Manager Service");
        media_manager_service_destroy(g_service);
        return 1;
    }

    media_manager_service_run(g_service);

    media_manager_service_destroy(g_service);
    g_service = NULL;

    LOG_I("Main-C", "Media Manager Service (Pure C) terminated cleanly");
    return 0;
}
