#ifndef COSMIC_CYCLE_SELECTION_H
#define COSMIC_CYCLE_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

struct window_summary {
    const char *app_id;
    bool active;
};

/*
 * Returns the index of the next/previous window belonging to the active
 * window's app, or -1 when there is no other matching window.
 */
int select_same_app_window(const struct window_summary *windows,
                           size_t count,
                           bool reverse);

/* Cycle distinct nonempty app IDs in first-appearance order. */
int select_other_app_window(const struct window_summary *windows,
                            size_t count,
                            bool reverse);

#endif
