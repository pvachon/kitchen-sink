#pragma once

#include "sh1106_config.h"

#include <stdint.h>
#include <stdbool.h>

enum sh1106_text_align {
    SH1106_TEXT_ALIGN_RIGHT,
    SH1106_TEXT_ALIGN_LEFT,
    SH1106_TEXT_ALIGN_CENTER,
    SH1106_TEXT_ALIGN_USER,
};

int sh1106_display_init(uint8_t contrast);
int sh1106_display_shutdown(void);
void sh1106_display_reset(void);

void sh1106_display_set_invert(bool invert);
void sh1106_display_puts(unsigned line, unsigned x_offs, const char *str, bool invert, enum sh1106_text_align align);
void sh1106_clear_page(int page, bool invert);
void sh1106_display_clear(void);
