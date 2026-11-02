#ifndef MENU_THEME_TEXT_H
#define MENU_THEME_TEXT_H

#include <stddef.h>

/* Theme metadata is plain text. Escape libdragon's font/style controls before
 * drawing it, including literal trailing '$' or '^'. Never split an escape. */
static inline void theme_text_escape(char *out, size_t capacity, const char *text) {
    if (!capacity) return;
    size_t used = 0;
    while (*text) {
        char c = *text++;
        size_t needed = (c == '$' || c == '^') ? 2 : 1;
        if (needed >= capacity - used) break;
        out[used++] = c;
        if (needed == 2) out[used++] = c;
    }
    out[used] = '\0';
}

#endif
