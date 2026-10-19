#include "CanSocketReceiver.hpp"
#include "../common/Logger.hpp"

#include <csignal>
#include <iostream>
#include <string>

static automotive::CanSocketReceiver* g_canReceiver = nullptr;

static void signalHandler(int sig) {
    LOG_I("CAN-Main", "Caught signal (" + std::to_string(sig) + "). Terminating...");
    if (g_canReceiver) {
        g_canReceiver->stop();
    }
}

int main(int argc, char* argv[]) {
    LOG_I("CAN-Main", "========================================================");
    LOG_I("CAN-Main", "  Automotive CAN Bus Telematics Processor Process       ");
    LOG_I("CAN-Main", "========================================================");

    std::string canInterface = "vcan0";
    bool forceSimulate = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--simulate" || arg == "-s") {
            forceSimulate = true;
        } else if (arg == "--interface" && i + 1 < argc) {
            canInterface = argv[++i];
        } else if (arg[0] != '-') {
            canInterface = arg;
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    automotive::CanSocketReceiver receiver;
    g_canReceiver = &receiver;

    if (!receiver.initialize(canInterface)) {
        LOG_E("CAN-Main", "Initialization failed");
        return 1;
    }

    if (forceSimulate) {
        receiver.startSimulationCycle();
    } else {
        receiver.start();
    }

    LOG_I("CAN-Main", "Interactive commands available: 's <speed>' to set speed, 'g <P|D|R|N>' to set gear, 'q' to quit");

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "q" || line == "quit") {
            break;
        }
        if (line[0] == 's' || line.rfind("speed", 0) == 0) {
            std::string valStr = line.substr(line.find(' ') + 1);
            try {
                double s = std::stod(valStr);
                receiver.setSimulatedSpeed(s);
            } catch (...) {
                std::cout << "Invalid speed value: " << valStr << std::endl;
            }
        } else if (line[0] == 'g' || line.rfind("gear", 0) == 0) {
            std::string gear = line.substr(line.find(' ') + 1);
            receiver.setSimulatedGear(gear);
        } else {
            std::cout << "Unknown command. Use: 's <speed>' (e.g. s 45), 'g <gear>' (e.g. g D), 'q'" << std::endl;
        }
    }

    receiver.stop();
    LOG_I("CAN-Main", "CAN process exited cleanly");
    return 0;
}
