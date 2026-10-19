#ifndef AUTOMOTIVE_USB_MONITOR_H
#define AUTOMOTIVE_USB_MONITOR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_inserted_cb_t)(const char *dev_node, const char *mount_point, void *user_data);
typedef void (*usb_removed_cb_t)(const char *dev_node, void *user_data);

typedef struct UsbMonitor UsbMonitor_t;

UsbMonitor_t* usb_monitor_create(void);
void usb_monitor_destroy(UsbMonitor_t *mon);

bool usb_monitor_init(UsbMonitor_t *mon);
void usb_monitor_start(UsbMonitor_t *mon);
void usb_monitor_stop(UsbMonitor_t *mon);

void usb_monitor_set_callbacks(UsbMonitor_t *mon,
                               usb_inserted_cb_t ins_cb,
                               usb_removed_cb_t rem_cb,
                               void *user_data);

void usb_monitor_simulate_insert(UsbMonitor_t *mon, const char *mount_path);
void usb_monitor_simulate_remove(UsbMonitor_t *mon);

#ifdef __cplusplus
}
#endif

#endif /* AUTOMOTIVE_USB_MONITOR_H */
