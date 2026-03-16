#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <stddef.h>

#include "app/app_types.h"

bool network_service_configured(void);
void network_service_start(void);
void network_service_tick(void);
void network_service_publish_samples(const app_samples_t *samples);
void network_service_format_status(char *wifi_buffer, size_t wifi_len, char *server_buffer, size_t server_len);

#endif
