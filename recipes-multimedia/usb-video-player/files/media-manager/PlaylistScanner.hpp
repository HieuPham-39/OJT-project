#pragma once

#include "../common/DBusCommon.hpp"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>

namespace automotive {

using ScanCompleteCallback = std::function<void(const std::vector<VideoTrack>& playlist)>;

class PlaylistScanner {
public:
    PlaylistScanner();
    ~PlaylistScanner();

    // Start async scan of directory
    void scanDirectoryAsync(const std::string& directoryPath, ScanCompleteCallback cb);
    
    // Synchronous scan (useful for testing)
    std::vector<VideoTrack> scanDirectorySync(const std::string& directoryPath);

    void clear();

    std::vector<VideoTrack> getPlaylist() const;
    bool getTrackById(int32_t id, VideoTrack& outTrack) const;
    size_t getTrackCount() const;

private:
    bool isValidVideoFile(const std::string& path) const;

    mutable std::mutex m_mutex;
    std::vector<VideoTrack> m_playlist;
    std::thread m_scanThread;
    std::atomic<bool> m_isScanning{false};
};

} // namespace automotive
