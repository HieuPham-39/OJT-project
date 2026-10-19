#include "playlist_scanner.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    PlaylistScanner_t *scanner;
    char dir_path[512];
    scan_complete_cb_t cb;
    void *user_data;
} AsyncScanArgs_t;

PlaylistScanner_t* playlist_scanner_create(void) {
    PlaylistScanner_t *scanner = (PlaylistScanner_t*)calloc(1, sizeof(PlaylistScanner_t));
    if (!scanner) return NULL;
    pthread_mutex_init(&scanner->lock, NULL);
    return scanner;
}

void playlist_scanner_destroy(PlaylistScanner_t *scanner) {
    if (!scanner) return;
    playlist_scanner_clear(scanner);
    pthread_mutex_destroy(&scanner->lock);
    free(scanner);
}

void playlist_scanner_clear(PlaylistScanner_t *scanner) {
    if (!scanner) return;
    pthread_mutex_lock(&scanner->lock);
    if (scanner->tracks) {
        free(scanner->tracks);
        scanner->tracks = NULL;
    }
    scanner->count = 0;
    scanner->capacity = 0;
    pthread_mutex_unlock(&scanner->lock);
    LOG_I("Scanner-C", "Playlist memory cleared");
}

static bool has_valid_video_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return false;

    // Filter valid video formats: .mp4, .mkv
    if (strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".mkv") == 0) {
        return true;
    }
    return false;
}

static void add_track(PlaylistScanner_t *scanner, const char *path, const char *title, uint64_t size) {
    if (scanner->count >= scanner->capacity) {
        size_t new_cap = (scanner->capacity == 0) ? 16 : scanner->capacity * 2;
        VideoTrack_t *new_arr = (VideoTrack_t*)realloc(scanner->tracks, new_cap * sizeof(VideoTrack_t));
        if (!new_arr) {
            LOG_E("Scanner-C", "Failed to allocate memory for track list");
            return;
        }
        scanner->tracks = new_arr;
        scanner->capacity = new_cap;
    }

    VideoTrack_t *t = &scanner->tracks[scanner->count];
    t->id = (int32_t)scanner->count;
    strncpy(t->title, title, sizeof(t->title) - 1);
    t->title[sizeof(t->title) - 1] = '\0';
    strncpy(t->file_path, path, sizeof(t->file_path) - 1);
    t->file_path[sizeof(t->file_path) - 1] = '\0';
    t->file_size = size;

    LOG_I("Scanner-C", "  [Track %d] Added: %s (%.1f MB)",
          t->id, t->title, (double)size / (1024.0 * 1024.0));

    scanner->count++;
}

static void scan_recursive(PlaylistScanner_t *scanner, const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *entry;
    char full_path[1024];

    while ((entry = readdir(d)) != NULL) {
        const char *name = entry->d_name;

        // Skip '.' and '..'
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        // Skip hidden files / directories (starting with '.')
        if (name[0] == '.') {
            continue;
        }

        // Skip Windows / macOS system directories
        if (strcasecmp(name, "System Volume Information") == 0 ||
            strcasecmp(name, "$RECYCLE.BIN") == 0 ||
            strcasecmp(name, ".Trashes") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);

        struct stat st;
        if (stat(full_path, &st) < 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Recurse into subdirectories
            scan_recursive(scanner, full_path);
        } else if (S_ISREG(st.st_mode)) {
            if (has_valid_video_extension(name) && st.st_size > 0) {
                // Extract title by stripping extension
                char title[128];
                strncpy(title, name, sizeof(title) - 1);
                title[sizeof(title) - 1] = '\0';
                char *dot = strrchr(title, '.');
                if (dot) *dot = '\0';

                add_track(scanner, full_path, title, (uint64_t)st.st_size);
            }
        }
    }

    closedir(d);
}

size_t playlist_scanner_scan_sync(PlaylistScanner_t *scanner, const char *dir_path) {
    if (!scanner || !dir_path) return 0;

    LOG_I("Scanner-C", "Starting recursive scan for .mp4 and .mkv in: %s", dir_path);

    playlist_scanner_clear(scanner);

    pthread_mutex_lock(&scanner->lock);
    scan_recursive(scanner, dir_path);
    size_t count = scanner->count;
    pthread_mutex_unlock(&scanner->lock);

    LOG_I("Scanner-C", "Scan completed. Found %zu valid video files.", count);
    return count;
}

static void* async_scan_worker(void *arg) {
    AsyncScanArgs_t *args = (AsyncScanArgs_t*)arg;
    size_t count = playlist_scanner_scan_sync(args->scanner, args->dir_path);

    args->scanner->is_scanning = false;
    if (args->cb) {
        args->cb(count, args->user_data);
    }

    free(args);
    return NULL;
}

void playlist_scanner_scan_async(PlaylistScanner_t *scanner,
                                 const char *dir_path,
                                 scan_complete_cb_t cb,
                                 void *user_data) {
    if (!scanner || !dir_path) return;

    AsyncScanArgs_t *args = (AsyncScanArgs_t*)malloc(sizeof(AsyncScanArgs_t));
    if (!args) return;

    args->scanner = scanner;
    strncpy(args->dir_path, dir_path, sizeof(args->dir_path) - 1);
    args->dir_path[sizeof(args->dir_path) - 1] = '\0';
    args->cb = cb;
    args->user_data = user_data;

    scanner->is_scanning = true;
    pthread_create(&scanner->thread, NULL, async_scan_worker, args);
    pthread_detach(scanner->thread);
}

size_t playlist_scanner_get_count(PlaylistScanner_t *scanner) {
    if (!scanner) return 0;
    pthread_mutex_lock(&scanner->lock);
    size_t count = scanner->count;
    pthread_mutex_unlock(&scanner->lock);
    return count;
}

bool playlist_scanner_get_track_by_id(PlaylistScanner_t *scanner, int32_t id, VideoTrack_t *out_track) {
    if (!scanner || !out_track) return false;
    pthread_mutex_lock(&scanner->lock);
    for (size_t i = 0; i < scanner->count; ++i) {
        if (scanner->tracks[i].id == id) {
            *out_track = scanner->tracks[i];
            pthread_mutex_unlock(&scanner->lock);
            return true;
        }
    }
    pthread_mutex_unlock(&scanner->lock);
    return false;
}

bool playlist_scanner_get_track_by_index(PlaylistScanner_t *scanner, size_t index, VideoTrack_t *out_track) {
    if (!scanner || !out_track) return false;
    pthread_mutex_lock(&scanner->lock);
    if (index < scanner->count) {
        *out_track = scanner->tracks[index];
        pthread_mutex_unlock(&scanner->lock);
        return true;
    }
    pthread_mutex_unlock(&scanner->lock);
    return false;
}
