#include "selection.h"

#include <assert.h>

static void selects_next_matching_app(void)
{
    const struct window_summary windows[] = {
        {.app_id = "terminal", .active = true},
        {.app_id = "browser", .active = false},
        {.app_id = "terminal", .active = false},
    };

    assert(select_same_app_window(windows, 3, false) == 2);
}

static void wraps_forward(void)
{
    const struct window_summary windows[] = {
        {.app_id = "terminal", .active = false},
        {.app_id = "browser", .active = false},
        {.app_id = "terminal", .active = true},
    };

    assert(select_same_app_window(windows, 3, false) == 0);
}

static void selects_previous_and_wraps(void)
{
    const struct window_summary windows[] = {
        {.app_id = "terminal", .active = true},
        {.app_id = "browser", .active = false},
        {.app_id = "terminal", .active = false},
    };

    assert(select_same_app_window(windows, 3, true) == 2);
}

static void rejects_missing_active_or_peer(void)
{
    const struct window_summary no_active[] = {
        {.app_id = "terminal", .active = false},
        {.app_id = "terminal", .active = false},
    };
    const struct window_summary no_peer[] = {
        {.app_id = "terminal", .active = true},
        {.app_id = "browser", .active = false},
    };
    const struct window_summary missing_id[] = {
        {.app_id = NULL, .active = true},
        {.app_id = NULL, .active = false},
    };

    assert(select_same_app_window(no_active, 2, false) == -1);
    assert(select_same_app_window(no_peer, 2, false) == -1);
    assert(select_same_app_window(missing_id, 2, false) == -1);
}

static void cycles_distinct_apps(void)
{
    struct window_summary windows[] = {
        {.app_id = "browser", .active = false},
        {.app_id = "terminal", .active = false},
        {.app_id = "browser", .active = true},
        {.app_id = NULL, .active = false},
        {.app_id = "", .active = false},
        {.app_id = "editor", .active = false},
        {.app_id = "terminal", .active = false},
    };
    assert(select_other_app_window(windows, 7, false) == 1);
    assert(select_other_app_window(windows, 7, true) == 5);
    windows[2].active = false;
    windows[6].active = true;
    assert(select_other_app_window(windows, 7, false) == 5);
    assert(select_other_app_window(windows, 7, true) == 0);
    windows[6].active = false;
    windows[5].active = true;
    assert(select_other_app_window(windows, 7, false) == 0);
    assert(select_other_app_window(windows, 7, true) == 1);
    windows[5].active = false;
    assert(select_other_app_window(windows, 7, false) == -1);
    assert(select_other_app_window(NULL, 0, false) == -1);
    const struct window_summary single_app[] = {
        {.app_id = "browser", .active = true},
        {.app_id = "browser", .active = false},
    };
    assert(select_other_app_window(single_app, 2, false) == -1);
    assert(select_other_app_window(single_app, 2, true) == -1);
}

int main(void)
{
    cycles_distinct_apps();
    selects_next_matching_app();
    wraps_forward();
    selects_previous_and_wraps();
    rejects_missing_active_or_peer();
    return 0;
}
