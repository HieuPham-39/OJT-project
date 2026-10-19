#ifndef AUTOMOTIVE_PLAYLIST_SCANNER_H
#define AUTOMOTIVE_PLAYLIST_SCANNER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t id;
    char title[128];
    char file_path[512];
    uint64_t file_size;
} VideoTrack_t;

typedef void (*scan_complete_cb_t)(size_t count, void *user_data);

typedef struct PlaylistScanner {
    VideoTrack_t *tracks;
    size_t count;
    size_t capacity;
    pthread_mutex_t lock;

    pthread_t thread;
    bool is_scanning;
} PlaylistScanner_t;

PlaylistScanner_t* playlist_scanner_create(void);
void playlist_scanner_destroy(PlaylistScanner_t *scanner);

void playlist_scanner_scan_async(PlaylistScanner_t *scanner,
                                 const char *dir_path,
                                 scan_complete_cb_t cb,
                                 void *user_data);

size_t playlist_scanner_scan_sync(PlaylistScanner_t *scanner, const char *dir_path);
void playlist_scanner_clear(PlaylistScanner_t *scanner);
size_t playlist_scanner_get_count(PlaylistScanner_t *scanner);
bool playlist_scanner_get_track_by_id(PlaylistScanner_t *scanner, int32_t id, VideoTrack_t *out_track);
bool playlist_scanner_get_track_by_index(PlaylistScanner_t *scanner, size_t index, VideoTrack_t *out_track);

#ifdef __cplusplus
}
#endif

#endif /* AUTOMOTIVE_PLAYLIST_SCANNER_H */
