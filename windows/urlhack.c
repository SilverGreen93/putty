/*
 * PuTTY-url
 */
#include <windows.h>
#include <string.h>
#include "urlhack.h"
#include "misc.h"
#include "puttymem.h"

int urlhack_is_in_link_region(URLHack *u, int x, int y)
{
    unsigned int i = 0;

    while (i != u->link_regions_current_pos) {
        text_region r = *u->link_regions[i];

        if ((r.y0 == r.y1 && y == r.y0 && y == r.y1 && x >= r.x0 && x < r.x1) ||
            (r.y0 != r.y1 && ((y == r.y0 && x >= r.x0) || (y == r.y1 && x < r.x1) || (y > r.y0 && y < r.y1))))
            return i+1;
        i++;
    }

    return 0;
}

int urlhack_is_in_this_link_region(text_region r, int x, int y)
{
    if ((r.y0 == r.y1 && y == r.y0 && y == r.y1 && x >= r.x0 && x < r.x1) ||
        (r.y0 != r.y1 && ((y == r.y0 && x >= r.x0) || (y == r.y1 && x < r.x1) || (y > r.y0 && y < r.y1)))) {
        return 1;
    }

    return 0;
}

text_region urlhack_get_link_bounds(URLHack *u, int x, int y)
{
    unsigned int i = 0;
    text_region region;

    while (i != u->link_regions_current_pos) {
        text_region r = *u->link_regions[i];

        if ((r.y0 == r.y1 && y == r.y0 && y == r.y1 && x >= r.x0 && x < r.x1) ||
            (r.y0 != r.y1 && ((y == r.y0 && x >= r.x0) || (y == r.y1 && x < r.x1) || (y > r.y0 && y < r.y1)))) {
            return *u->link_regions[i];
        }

        i++;
    }

    region.x0 = region.y0 = region.x1 = region.y1 = -1;
    return region;
}

text_region urlhack_get_link_region(URLHack *u, int index)
{
    text_region region;

    if (index < 0 || index >= u->link_regions_current_pos) {
        region.x0 = region.y0 = region.x1 = region.y1 = -1;
        return region;
    }
    else {
        return *u->link_regions[index];
    }
}

void urlhack_add_link_region(URLHack *u, int x0, int y0, int x1, int y1)
{
    if (u->link_regions_current_pos >= u->link_regions_len) {
        unsigned int i;
        u->link_regions_len *= 2;
        u->link_regions = sresize(u->link_regions, u->link_regions_len, text_region*);
        for (i = u->link_regions_current_pos; i < u->link_regions_len; ++i) {
            u->link_regions[i] = NULL;
        }
    }

    u->link_regions[u->link_regions_current_pos] = snew(text_region);
    u->link_regions[u->link_regions_current_pos]->x0 = x0;
    u->link_regions[u->link_regions_current_pos]->y0 = y0;
    u->link_regions[u->link_regions_current_pos]->x1 = x1;
    u->link_regions[u->link_regions_current_pos]->y1 = y1;

    u->link_regions_current_pos++;
}

void urlhack_launch_url(const char *app, const char *url)
{
    if (app) {
        ShellExecute(NULL, NULL, app, url, NULL, SW_SHOWNORMAL);
    } else {
        ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    }
}

int urlhack_is_ctrl_pressed()
{
    return HIWORD(GetAsyncKeyState(VK_CONTROL));
}

void urlhack_link_regions_clear(URLHack *u)
{
    unsigned int i;
    for (i = 0; i < u->link_regions_len; ++i) {
        if (u->link_regions[i] != NULL) {
            sfree(u->link_regions[i]);
            u->link_regions[i] = NULL;
        }
    }
    u->link_regions_current_pos = 0;
}

/* Regular expression stuff */

URLHack *urlhack_new(void)
{
    unsigned int i;
    URLHack *u = snew(URLHack);
    memset(u, 0, sizeof(*u));

    u->mouse_old_x = u->mouse_old_y = u->current_region = -1;

    /* 32 links seems like a sane base value */
    u->link_regions_len = 32;
    u->link_regions = snewn(u->link_regions_len, text_region*);
    for (i = 0; i < u->link_regions_len; ++i)
        u->link_regions[i] = NULL;

    /* Start with default terminal size */
    u->window_text_len = 80*24+1;
    u->window_text = snewn(u->window_text_len, char);
    urlhack_reset(u);
    return u;
}

void urlhack_free(URLHack *u)
{
    if (!u)
        return;
    urlhack_link_regions_clear(u);
    sfree(u->link_regions);
    sfree(u->window_text);
    free(u->urlhack_rx);               /* allocated by regcomp() with malloc */
    sfree(u);
}

void urlhack_putchar(URLHack *u, char ch)
{
    if (u->window_text_current_pos >= u->window_text_len) {
        u->window_text = sresize(u->window_text, 2 * u->window_text_len, char);
        memset(u->window_text + u->window_text_current_pos, '\0', u->window_text_len - u->window_text_current_pos);
        u->window_text_len *= 2;
    }
    u->window_text[u->window_text_current_pos++] = ch;
}

void urlhack_reset(URLHack *u)
{
    memset(u->window_text, '\0', u->window_text_len);
    u->window_text_current_pos = 0;
}

static void rtfm(char *error)
{
    char std_msg[] = "The following error occured when compiling the regular expression\n" \
        "for the hyperlink support. Hyperlink detection is disabled during\n" \
        "this session (restart PuTTY Tray to try again).\n\n";

    char *full_msg = dupprintf("%s%s", std_msg, error);

    MessageBox(0, full_msg, "PuTTY Tray Error", MB_OK);
    free(full_msg);
}

void urlhack_set_regular_expression(URLHack *u, const char *expression)
{
    u->is_regexp_compiled = 0;
    u->urlhack_disabled = 0;

    free(u->urlhack_rx);
    u->urlhack_rx = NULL;

    set_regerror_func(rtfm);
    u->urlhack_rx = regcomp((char *)expression);

    if (u->urlhack_rx == 0)
        u->urlhack_disabled = 1;

    u->is_regexp_compiled = 1;
}

void urlhack_go_find_me_some_hyperlinks(URLHack *u, int screen_width)
{
    char* text_pos;

    if (u->urlhack_disabled != 0) return;

    if (u->is_regexp_compiled == 0) {
        urlhack_set_regular_expression(u, URLHACK_DEFAULT_REGEX);
    }

    urlhack_link_regions_clear(u);

    text_pos = u->window_text;

    while (regexec(u->urlhack_rx, text_pos) == 1) {
        char* start_pos = *u->urlhack_rx->startp[0] == ' ' ? u->urlhack_rx->startp[0] + 1: u->urlhack_rx->startp[0];

        int x0 = (start_pos - u->window_text) % screen_width;
        int y0 = (start_pos - u->window_text) / screen_width;
        int x1 = (u->urlhack_rx->endp[0] - u->window_text) % screen_width;
        int y1 = (u->urlhack_rx->endp[0] - u->window_text) / screen_width;

        if (x0 >= screen_width) x0 = screen_width - 1;
        if (x1 >= screen_width) x1 = screen_width - 1;

        urlhack_add_link_region(u, x0, y0, x1, y1);

        text_pos = u->urlhack_rx->endp[0] + 1;
    }
}
