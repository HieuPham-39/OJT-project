#include "GstPlayer.hpp"
#include "../common/Logger.hpp"

#include <csignal>
#include <iostream>

static automotive::GstPlayer* g_player = nullptr;

static void signalHandler(int sig) {
    LOG_I("Player-Main", "Caught signal (" + std::to_string(sig) + "). Stopping video playback engine...");
    if (g_player) {
        g_player->stop();
    }
}

int main(int argc, char* argv[]) {
    LOG_I("Player-Main", "========================================================");
    LOG_I("Player-Main", "  Automotive Video Playback Engine Process              ");
    LOG_I("Player-Main", "========================================================");

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    automotive::GstPlayer player;
    g_player = &player;

    if (!player.initialize(argc, argv)) {
        LOG_E("Player-Main", "Failed to initialize GStreamer player engine");
        return 1;
    }

    player.run();

    LOG_I("Player-Main", "Video Playback Engine exited cleanly");
    return 0;
}
