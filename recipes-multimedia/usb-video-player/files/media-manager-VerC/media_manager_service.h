#ifndef AUTOMOTIVE_MEDIA_MANAGER_SERVICE_H
#define AUTOMOTIVE_MEDIA_MANAGER_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MediaManagerService MediaManagerService_t;

MediaManagerService_t* media_manager_service_create(void);
void media_manager_service_destroy(MediaManagerService_t *svc);

bool media_manager_service_init(MediaManagerService_t *svc);
void media_manager_service_run(MediaManagerService_t *svc);
void media_manager_service_stop(MediaManagerService_t *svc);

#ifdef __cplusplus
}
#endif

#endif /* AUTOMOTIVE_MEDIA_MANAGER_SERVICE_H */
