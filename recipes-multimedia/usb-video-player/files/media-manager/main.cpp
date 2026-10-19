#include "MediaManagerService.hpp"
#include "../common/Logger.hpp"

#include <csignal>
#include <iostream>

static automotive::MediaManagerService* g_service = nullptr;

static void signalHandler(int sig) {
    LOG_I("Main", "Caught termination signal (" + std::to_string(sig) + "). Shutting down gracefully...");
    if (g_service) {
        g_service->stop();
    }
}

int main(int argc, char* argv[]) {
    LOG_I("Main", "========================================================");
    LOG_I("Main", "  Automotive USB Video Player - Core Service Daemon     ");
    LOG_I("Main", "========================================================");

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    automotive::MediaManagerService service;
    g_service = &service;

    if (!service.initialize()) {
        LOG_E("Main", "Failed to initialize Media Manager Service");
        return 1;
    }

    service.run();

    LOG_I("Main", "Media Manager Service terminated cleanly");
    return 0;
}
