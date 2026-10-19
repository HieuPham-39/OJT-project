#include "HmiController.hpp"
#include "../common/Logger.hpp"

#include <csignal>
#include <iostream>

static automotive::HmiController* g_hmi = nullptr;

static void signalHandler(int sig) {
    if (g_hmi) {
        g_hmi->stop();
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    automotive::HmiController hmi;
    g_hmi = &hmi;

    if (!hmi.initialize()) {
        std::cerr << "Failed to initialize Automotive UI Controller" << std::endl;
        return 1;
    }

    hmi.run();

    std::cout << "\nAutomotive UI Controller exited.\n";
    return 0;
}
