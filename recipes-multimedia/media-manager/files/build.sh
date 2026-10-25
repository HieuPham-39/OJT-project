#!/bin/bash
set -e

echo "=== Building Automotive Media Manager (Version C) ==="

# Verify dependencies
if ! pkg-config --exists glib-2.0 gio-2.0 libudev; then
    echo "Error: Missing required development packages."
    echo "Please install on Ubuntu/Debian:"
    echo "  sudo apt-get update && sudo apt-get install -y libglib2.0-dev libudev-dev build-essential"
    exit 1
fi

CFLAGS=$(pkg-config --cflags glib-2.0 gio-2.0 libudev)
LIBS=$(pkg-config --libs glib-2.0 gio-2.0 libudev)

gcc -O2 -Wall -Wextra -std=c11 \
    main.c logger.c fsm.c usb_monitor.c playlist_scanner.c media_manager_service.c \
    $CFLAGS $LIBS -lpthread \
    -o ojt-media-manager

echo "✅ Build completed successfully: $(pwd)/ojt-media-manager"
