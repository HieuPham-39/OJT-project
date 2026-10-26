#!/bin/bash
# Generate sample test videos using GStreamer on the Linux host
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAMPLE_DIR="${SCRIPT_DIR}/sample_videos"
mkdir -p "${SAMPLE_DIR}"

echo "Generating sample videos in: ${SAMPLE_DIR}"

# 1. demo_01_adas_overview.mkv (10 seconds, color bars with moving ball)
if [ ! -f "${SAMPLE_DIR}/demo_01_adas_overview.mkv" ]; then
    echo ">> Rendering demo_01_adas_overview.mkv..."
    gst-launch-1.0 -e videotestsrc pattern=ball num-buffers=300 ! \
        video/x-raw,width=640,height=360,framerate=30/1 ! \
        videoconvert ! vp8enc ! matroskamux ! filesink location="${SAMPLE_DIR}/demo_01_adas_overview.mkv" >/dev/null 2>&1 || true
fi

# 2. demo_02_can_telematics.mkv (10 seconds, snow pattern)
if [ ! -f "${SAMPLE_DIR}/demo_02_can_telematics.mkv" ]; then
    echo ">> Rendering demo_02_can_telematics.mkv..."
    gst-launch-1.0 -e videotestsrc pattern=snow num-buffers=300 ! \
        video/x-raw,width=640,height=360,framerate=30/1 ! \
        videoconvert ! vp8enc ! matroskamux ! filesink location="${SAMPLE_DIR}/demo_02_can_telematics.mkv" >/dev/null 2>&1 || true
fi

# 3. demo_03_cockpit_hmi.mkv (10 seconds, smpte color bars)
if [ ! -f "${SAMPLE_DIR}/demo_03_cockpit_hmi.mkv" ]; then
    echo ">> Rendering demo_03_cockpit_hmi.mkv..."
    gst-launch-1.0 -e videotestsrc pattern=smpte num-buffers=300 ! \
        video/x-raw,width=640,height=360,framerate=30/1 ! \
        videoconvert ! vp8enc ! matroskamux ! filesink location="${SAMPLE_DIR}/demo_03_cockpit_hmi.mkv" >/dev/null 2>&1 || true
fi

echo "Sample video files ready:"
ls -lh "${SAMPLE_DIR}"
