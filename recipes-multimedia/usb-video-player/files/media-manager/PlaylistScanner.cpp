#include "PlaylistScanner.hpp"
#include "../common/Logger.hpp"

#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace automotive {

PlaylistScanner::PlaylistScanner() = default;

PlaylistScanner::~PlaylistScanner() {
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

bool PlaylistScanner::isValidVideoFile(const std::string& pathStr) const {
    fs::path p(pathStr);
    
    // Check filename doesn't start with dot (hidden file / temp file)
    std::string filename = p.filename().string();
    if (filename.empty() || filename[0] == '.') {
        return false;
    }

    // Filter valid automotive video formats: .mp4, .mkv
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".mp4" || ext == ".mkv") {
        return true;
    }

    return false;
}

std::vector<VideoTrack> PlaylistScanner::scanDirectorySync(const std::string& directoryPath) {
    LOG_I("Scanner", "Scanning directory for valid video files (.mp4, .mkv): " + directoryPath);

    std::vector<VideoTrack> results;

    std::error_code ec;
    if (!fs::exists(directoryPath, ec) || !fs::is_directory(directoryPath, ec)) {
        LOG_W("Scanner", "Directory does not exist or is inaccessible: " + directoryPath);
        return results;
    }

    int32_t trackId = 0;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(directoryPath, 
                                      fs::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                LOG_W("Scanner", "Skipping error path: " + ec.message());
                continue;
            }

            if (entry.is_regular_file(ec)) {
                std::string fullPath = entry.path().string();
                if (isValidVideoFile(fullPath)) {
                    uintmax_t sizeBytes = entry.file_size(ec);
                    if (sizeBytes > 0) {
                        VideoTrack track;
                        track.id = trackId++;
                        track.title = entry.path().stem().string();
                        track.filePath = fullPath;
                        track.fileSizeBytes = sizeBytes;
                        track.durationMs = 0; // Populated by player when loaded

                        LOG_I("Scanner", "  [Track " + std::to_string(track.id) + "] Found: " + 
                                         track.title + " (" + std::to_string(sizeBytes / (1024 * 1024)) + " MB)");

                        results.push_back(track);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_E("Scanner", "Exception during directory iteration: " + std::string(e.what()));
    }

    LOG_I("Scanner", "Scan complete. Total valid video files found: " + std::to_string(results.size()));

    std::lock_guard<std::mutex> lock(m_mutex);
    m_playlist = results;
    return results;
}

void PlaylistScanner::scanDirectoryAsync(const std::string& directoryPath, ScanCompleteCallback cb) {
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }

    m_isScanning = true;
    m_scanThread = std::thread([this, directoryPath, cb]() {
        auto list = scanDirectorySync(directoryPath);
        m_isScanning = false;
        if (cb) {
            cb(list);
        }
    });
}

void PlaylistScanner::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playlist.clear();
    LOG_I("Scanner", "Playlist cleared");
}

std::vector<VideoTrack> PlaylistScanner::getPlaylist() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playlist;
}

bool PlaylistScanner::getTrackById(int32_t id, VideoTrack& outTrack) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& track : m_playlist) {
        if (track.id == id) {
            outTrack = track;
            return true;
        }
    }
    return false;
}

size_t PlaylistScanner::getTrackCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playlist.size();
}

} // namespace automotive
