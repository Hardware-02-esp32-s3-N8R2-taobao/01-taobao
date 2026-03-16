#ifndef UI_PAGES_H
#define UI_PAGES_H

#include "app/app_types.h"

int ui_pages_count(void);
void ui_render_boot_screen(void);
void ui_render_current_page(int page_index, const app_samples_t *samples, const char *wifi_text, const char *server_text);

#endif
