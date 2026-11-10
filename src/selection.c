#include "selection.h"

#include <string.h>

static bool first_app_window(const struct window_summary *windows, size_t index)
{
    const char *id = windows[index].app_id;
    if (id == NULL || id[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < index; ++i) {
        if (windows[i].app_id != NULL && strcmp(id, windows[i].app_id) == 0) {
            return false;
        }
    }
    return true;
}

int select_other_app_window(const struct window_summary *windows,
                            size_t count, bool reverse)
{
    size_t active = count;
    for (size_t i = 0; i < count; ++i) {
        if (windows[i].active && windows[i].app_id != NULL &&
            windows[i].app_id[0] != '\0') {
            active = i;
            break;
        }
    }
    if (active == count) {
        return -1;
    }
    /* Anchor at the app's first window, regardless of which window has focus.
     * Otherwise interleaved windows can make cycling skip whole applications. */
    for (size_t i = 0; i < active; ++i) {
        if (windows[i].app_id != NULL &&
            strcmp(windows[i].app_id, windows[active].app_id) == 0) {
            active = i;
            break;
        }
    }
    for (size_t offset = 1; offset < count; ++offset) {
        size_t candidate = reverse ? (active + count - offset) % count
                                   : (active + offset) % count;
        if (first_app_window(windows, candidate) &&
            strcmp(windows[candidate].app_id, windows[active].app_id) != 0) {
            return (int)candidate;
        }
    }
    return -1;
}

int select_same_app_window(const struct window_summary *windows,
                           size_t count,
                           bool reverse)
{
    size_t active = count;

    for (size_t i = 0; i < count; ++i) {
        if (windows[i].active && windows[i].app_id != NULL &&
            windows[i].app_id[0] != '\0') {
            active = i;
            break;
        }
    }

    if (active == count) {
        return -1;
    }

    for (size_t offset = 1; offset < count; ++offset) {
        size_t candidate = reverse
                               ? (active + count - offset) % count
                               : (active + offset) % count;

        if (windows[candidate].app_id != NULL &&
            strcmp(windows[active].app_id, windows[candidate].app_id) == 0) {
            return (int)candidate;
        }
    }

    return -1;
}
