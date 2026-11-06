/*
 * PuTTY-url
 * Hyperlink: CORE FILE! Don't forget to COPY IT TO THE NEXT VERSION
 */
#ifndef _URLHACK_H
#define _URLHACK_H

#include "re_lib/regexp.h"

typedef struct { int x0, y0, x1, y1; } text_region;

/*
 * ((https?|ftp)://|www\.).(([^ ]*\([^ ]*\))([^ ()]*[^ ,;.:"')>])?|([^ ()]*[^ ,;.:"')>]))
 */
#define URLHACK_DEFAULT_REGEX "((https?|ftp)://|www\\.).(([^ ]*\\([^ ]*\\))([^ ()]*[^ ,;.:\"')>])?|([^ ()]*[^ ,;.:\"')>]))"

/* Per-terminal hyperlink state (a process may host many terminals) */
typedef struct URLHack {
    /* Mouse/cursor state, maintained by the window code */
    int mouse_old_x, mouse_old_y, current_region;
    int cursor_is_hand;

    text_region **link_regions;
    unsigned int link_regions_len;
    unsigned int link_regions_current_pos;

    int urlhack_disabled;
    int is_regexp_compiled;
    regexp *urlhack_rx;

    char *window_text;
    int window_text_len;
    int window_text_current_pos;
} URLHack;

URLHack *urlhack_new(void);
void urlhack_free(URLHack *u);

void urlhack_reset(URLHack *u);
void urlhack_go_find_me_some_hyperlinks(URLHack *u, int screen_width);
void urlhack_putchar(URLHack *u, char ch);
text_region urlhack_get_link_region(URLHack *u, int index);

int urlhack_is_in_link_region(URLHack *u, int x, int y);
int urlhack_is_in_this_link_region(text_region r, int x, int y);
text_region urlhack_get_link_bounds(URLHack *u, int x, int y);
void urlhack_add_link_region(URLHack *u, int x0, int y0, int x1, int y1);
void urlhack_link_regions_clear(URLHack *u);
void urlhack_launch_url(const char *app, const char *url);
int urlhack_is_ctrl_pressed();
/* Pass URLHACK_DEFAULT_REGEX for the default expression */
void urlhack_set_regular_expression(URLHack *u, const char *expression);

#endif /* _URLHACK_H */
