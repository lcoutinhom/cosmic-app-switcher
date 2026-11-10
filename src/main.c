#include "selection.h"
#include "switcher.h"
#include "mru.h"

#include "cosmic-toplevel-info-unstable-v1-client-protocol.h"
#include "cosmic-toplevel-management-unstable-v1-client-protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <wayland-client.h>

struct app;
struct window {
    struct zcosmic_toplevel_handle_v1 *cosmic;
    char *app_id;
    char *title;
    bool active;
    bool minimized;
    bool closed;
    struct window *next;
    struct app *app;
    bool focus_pending;
};

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct zcosmic_toplevel_info_v1 *toplevel_info;
    struct zcosmic_toplevel_manager_v1 *toplevel_manager;
    struct wl_seat *seat;
    struct window *windows;
    struct window **windows_tail;
    uint32_t toplevel_info_name;
    uint32_t toplevel_info_version;
    uint32_t toplevel_manager_name;
    uint32_t toplevel_manager_version;
    uint32_t seat_name;
    uint32_t seat_version;
    bool tracking;
    struct mru_history history;
};

static void replace_string(char **destination, const char *value)
{
    char *replacement = strdup(value != NULL ? value : "");
    if (replacement == NULL) {
        fprintf(stderr, "cosmic-cycle-app: out of memory\n");
        exit(EXIT_FAILURE);
    }
    free(*destination);
    *destination = replacement;
}

static void cosmic_closed(void *data, struct zcosmic_toplevel_handle_v1 *handle)
{
    struct window *w = data;
    w->closed = true;
    if (w->app && w->app->tracking) {
        struct window **link = &w->app->windows;
        while (*link && *link != w) link = &(*link)->next;
        if (*link) {
            *link = w->next;
            if (!w->next) w->app->windows_tail = link;
        }
        zcosmic_toplevel_handle_v1_destroy(handle);
        free(w->app_id); free(w->title); free(w);
    }
}

static void cosmic_done(void *data, struct zcosmic_toplevel_handle_v1 *handle)
{
    (void)handle;
    struct window *w = data;
    if (w->app && w->app->tracking && w->active && w->focus_pending &&
        w->app_id && w->app_id[0]) {
        w->focus_pending = false;
        if (mru_touch(&w->app->history, w->app_id))
            (void)mru_save(&w->app->history);
    }
}

static void cosmic_title(void *data,
                         struct zcosmic_toplevel_handle_v1 *handle,
                         const char *title)
{
    (void)handle;
    replace_string(&((struct window *)data)->title, title);
}

static void cosmic_app_id(void *data,
                          struct zcosmic_toplevel_handle_v1 *handle,
                          const char *app_id)
{
    (void)handle;
    replace_string(&((struct window *)data)->app_id, app_id);
}

static void cosmic_output_enter(void *data,
                                struct zcosmic_toplevel_handle_v1 *handle,
                                struct wl_output *output)
{
    (void)data;
    (void)handle;
    (void)output;
}

static void cosmic_output_leave(void *data,
                                struct zcosmic_toplevel_handle_v1 *handle,
                                struct wl_output *output)
{
    (void)data;
    (void)handle;
    (void)output;
}

static void cosmic_workspace_enter(
    void *data,
    struct zcosmic_toplevel_handle_v1 *handle,
    struct zcosmic_workspace_handle_v1 *workspace)
{
    (void)data;
    (void)handle;
    (void)workspace;
}

static void cosmic_workspace_leave(
    void *data,
    struct zcosmic_toplevel_handle_v1 *handle,
    struct zcosmic_workspace_handle_v1 *workspace)
{
    (void)data;
    (void)handle;
    (void)workspace;
}

static void cosmic_state(void *data,
                         struct zcosmic_toplevel_handle_v1 *handle,
                         struct wl_array *states)
{
    struct window *window = data;
    uint32_t *state;
    (void)handle;

    bool was_active = window->active;
    window->active = false;
    window->minimized = false;
    wl_array_for_each(state, states)
    {
        if (*state == ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
            window->active = true;
        } else if (*state == ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
            window->minimized = true;
        }
    }
    if (window->active && !was_active) window->focus_pending = true;
    if (!window->active) window->focus_pending = false;
}

static void cosmic_geometry(void *data,
                            struct zcosmic_toplevel_handle_v1 *handle,
                            struct wl_output *output,
                            int32_t x,
                            int32_t y,
                            int32_t width,
                            int32_t height)
{
    (void)data;
    (void)handle;
    (void)output;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void cosmic_ext_workspace_enter(
    void *data,
    struct zcosmic_toplevel_handle_v1 *handle,
    struct ext_workspace_handle_v1 *workspace)
{
    (void)data;
    (void)handle;
    (void)workspace;
}

static void cosmic_ext_workspace_leave(
    void *data,
    struct zcosmic_toplevel_handle_v1 *handle,
    struct ext_workspace_handle_v1 *workspace)
{
    (void)data;
    (void)handle;
    (void)workspace;
}

static const struct zcosmic_toplevel_handle_v1_listener cosmic_listener = {
    .closed = cosmic_closed,
    .done = cosmic_done,
    .title = cosmic_title,
    .app_id = cosmic_app_id,
    .output_enter = cosmic_output_enter,
    .output_leave = cosmic_output_leave,
    .workspace_enter = cosmic_workspace_enter,
    .workspace_leave = cosmic_workspace_leave,
    .state = cosmic_state,
    .geometry = cosmic_geometry,
    .ext_workspace_enter = cosmic_ext_workspace_enter,
    .ext_workspace_leave = cosmic_ext_workspace_leave,
};

static void info_toplevel(void *data,
                          struct zcosmic_toplevel_info_v1 *info,
                          struct zcosmic_toplevel_handle_v1 *handle)
{
    struct app *app = data;
    struct window *window = calloc(1, sizeof(*window));
    (void)info;

    if (window == NULL) {
        fprintf(stderr, "cosmic-cycle-app: out of memory\n");
        exit(EXIT_FAILURE);
    }

    window->cosmic = handle;
    window->app = app;
    zcosmic_toplevel_handle_v1_add_listener(
        window->cosmic, &cosmic_listener, window);

    *app->windows_tail = window;
    app->windows_tail = &window->next;
}

static void info_finished(void *data, struct zcosmic_toplevel_info_v1 *info)
{
    (void)data;
    (void)info;
}

static void info_done(void *data, struct zcosmic_toplevel_info_v1 *info)
{
    (void)data;
    (void)info;
}

static const struct zcosmic_toplevel_info_v1_listener info_listener = {
    .toplevel = info_toplevel,
    .finished = info_finished,
    .done = info_done,
};

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
    struct app *app = data;
    (void)registry;

    if (strcmp(interface, zcosmic_toplevel_info_v1_interface.name) == 0) {
        app->toplevel_info_name = name;
        app->toplevel_info_version = version;
    } else if (strcmp(interface,
                      zcosmic_toplevel_manager_v1_interface.name) == 0) {
        app->toplevel_manager_name = name;
        app->toplevel_manager_version = version;
    } else if (strcmp(interface, wl_seat_interface.name) == 0 &&
               app->seat_name == 0) {
        app->seat_name = name;
        app->seat_version = version;
    }
}

static void registry_global_remove(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static uint32_t minimum(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static bool bind_protocols(struct app *app)
{
    if (app->toplevel_info_name == 0 || app->toplevel_manager_name == 0 ||
        app->seat_name == 0 || app->toplevel_info_version < 1) {
        fprintf(stderr,
                "cosmic-cycle-app: required COSMIC Wayland "
                "protocols are unavailable\n");
        return false;
    }

    app->toplevel_info = wl_registry_bind(
        app->registry,
        app->toplevel_info_name,
        &zcosmic_toplevel_info_v1_interface,
        1);
    zcosmic_toplevel_info_v1_add_listener(
        app->toplevel_info, &info_listener, app);
    app->toplevel_manager = wl_registry_bind(
        app->registry,
        app->toplevel_manager_name,
        &zcosmic_toplevel_manager_v1_interface,
        minimum(app->toplevel_manager_version, 4));
    app->seat = wl_registry_bind(
        app->registry, app->seat_name, &wl_seat_interface, 1);
    return true;
}

static int collect_windows(struct app *app)
{
    app->display = wl_display_connect(NULL);
    if (app->display == NULL) {
        fprintf(stderr,
                "cosmic-cycle-app: cannot connect to Wayland\n");
        return -1;
    }

    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    if (wl_display_roundtrip(app->display) < 0 || !bind_protocols(app)) {
        return -1;
    }

    if (wl_display_roundtrip(app->display) < 0 ||
        wl_display_roundtrip(app->display) < 0) {
        fprintf(stderr,
                "cosmic-cycle-app: failed to read window state\n");
        return -1;
    }

    return 0;
}

static size_t window_count(const struct app *app)
{
    size_t count = 0;
    for (const struct window *window = app->windows; window != NULL;
         window = window->next) {
        if (!window->closed) {
            ++count;
        }
    }
    return count;
}

static struct window *choose_window(struct app *app, bool reverse, bool apps)
{
    const size_t count = window_count(app);
    if (count == 0) {
        return NULL;
    }
    struct window_summary *summaries = calloc(count, sizeof(*summaries));
    struct window **windows = calloc(count, sizeof(*windows));
    struct window *window;
    size_t index = 0;
    int selected;

    if (summaries == NULL || windows == NULL) {
        free(summaries);
        free(windows);
        fprintf(stderr, "cosmic-cycle-app: out of memory\n");
        return NULL;
    }

    for (window = app->windows; window != NULL; window = window->next) {
        if (!window->closed) {
            windows[index] = window;
            summaries[index].app_id = window->app_id;
            summaries[index].active = window->active;
            ++index;
        }
    }

    selected = apps ? select_other_app_window(summaries, count, reverse)
                    : select_same_app_window(summaries, count, reverse);
    window = selected >= 0 ? windows[selected] : NULL;
    free(summaries);
    free(windows);
    return window;
}

static struct window *choose_app_panel(struct app *app, bool reverse, bool dry_run)
{
    size_t total = window_count(app), count = 0;
    if (total == 0) return NULL;
    const char **ids = calloc(total, sizeof(*ids));
    struct window **windows = calloc(total, sizeof(*windows));
    if (!ids || !windows) { free(ids); free(windows); return NULL; }
    const char *active_id = NULL;
    for (struct window *w = app->windows; w != NULL; w = w->next) {
        if (w->closed || !w->app_id || !w->app_id[0]) continue;
        if (w->active) active_id = w->app_id;
        bool duplicate = false;
        for (size_t i = 0; i < count; ++i)
            if (strcmp(ids[i], w->app_id) == 0) duplicate = true;
        if (duplicate) continue;
        ids[count] = w->app_id;
        windows[count] = w;
        ++count;
    }
    struct mru_history history = {0};
    mru_load(&history);
    /* Correct a possible tracker-event race using the current snapshot. */
    if (active_id) (void)mru_touch(&history, active_id);
    size_t *order = calloc(count ? count : 1, sizeof(*order));
    struct window **sorted = calloc(count ? count : 1, sizeof(*sorted));
    const char **sorted_ids = calloc(count ? count : 1, sizeof(*sorted_ids));
    if (!order || !sorted || !sorted_ids) {
        free(order); free(sorted); free(sorted_ids); free(ids); free(windows);
        mru_clear(&history);
        return NULL;
    }
    mru_sort(&history, ids, count, order);
    for (size_t i = 0; i < count; ++i) {
        sorted[i] = windows[order[i]];
        sorted_ids[i] = ids[order[i]];
    }
    free(order); free(ids); free(windows);
    ids = sorted_ids; windows = sorted;
    mru_clear(&history);
    size_t selected = count > 1 && active_id ? (reverse ? count - 1 : 1) : 0;
    int choice = count > 1 ? (dry_run ? (int)selected : switcher_show(ids, count, selected)) : -1;
    struct window *target = choice >= 0 ? windows[choice] : NULL;
    free(ids); free(windows);
    /* The target may have closed while the chooser was open. */
    if (wl_display_roundtrip(app->display) < 0 || (target && target->closed))
        return NULL;
    return target;
}

static int track_focus(struct app *app)
{
    int lock = mru_tracker_lock();
    if (lock < 0) return EXIT_SUCCESS; /* Already running for this session. */
    app->tracking = true;
    mru_load(&app->history);
    if (collect_windows(app) < 0) { close(lock); return EXIT_FAILURE; }
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0) { close(lock); return EXIT_FAILURE; }
    executable[length] = '\0';
    struct stat running;
    if (stat("/proc/self/exe", &running) != 0) { close(lock); return EXIT_FAILURE; }
    for (;;) {
        struct stat current;
        /* Exit after uninstall/upgrade; the next invocation starts new code. */
        if (stat(executable, &current) != 0 || current.st_ino != running.st_ino ||
            current.st_dev != running.st_dev) break;
        int prepared;
        bool failed = false;
        while ((prepared = wl_display_prepare_read(app->display)) != 0) {
            if (wl_display_dispatch_pending(app->display) < 0) { failed = true; break; }
        }
        if (failed) break;
        wl_display_flush(app->display);
        struct pollfd fd = {.fd = wl_display_get_fd(app->display), .events = POLLIN};
        int ready = poll(&fd, 1, 2000);
        if (ready > 0 && (fd.revents & POLLIN)) {
            if (wl_display_read_events(app->display) < 0 ||
                wl_display_dispatch_pending(app->display) < 0) break;
        } else {
            wl_display_cancel_read(app->display);
        }
        if (fd.revents & (POLLERR | POLLHUP)) break;
    }
    mru_clear(&app->history);
    wl_display_disconnect(app->display);
    close(lock);
    return EXIT_SUCCESS;
}

static void ensure_focus_tracker(void)
{
    int lock = mru_tracker_lock();
    if (lock < 0) return;
    close(lock);
    pid_t child = fork();
    if (child == 0) {
        if (fork() == 0) {
            (void)setsid();
            int null_fd = open("/dev/null", O_RDWR);
            if (null_fd >= 0) {
                dup2(null_fd, STDIN_FILENO); dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                if (null_fd > STDERR_FILENO) close(null_fd);
            }
            execl("/proc/self/exe", "cosmic-cycle-app", "--track-focus", (char *)NULL);
        }
        _exit(0);
    }
    if (child > 0) (void)waitpid(child, NULL, 0);
}

static void show_app_indicator(const struct window *target)
{
    const char *home = getenv("HOME");
    if (home == NULL) {
        return;
    }
    char path[4096];
    int length = snprintf(path, sizeof(path),
                          "%s/.local/lib/cosmic-cycle-app/show_app.py", home);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return;
    }
    /* Double fork keeps the indicator independent without leaving zombies. */
    pid_t child = fork();
    if (child == 0) {
        if (fork() == 0) {
            execl("/usr/bin/python3", "python3", path, target->app_id,
                  target->title != NULL ? target->title : "", (char *)NULL);
        }
        _exit(0);
    }
    if (child > 0) {
        (void)waitpid(child, NULL, 0);
    }
}

static void print_windows(const struct app *app)
{
    for (const struct window *window = app->windows; window != NULL;
         window = window->next) {
        if (!window->closed) {
            printf("%c\t%s\t%s\n",
                   window->active ? '*' : ' ',
                   window->app_id != NULL ? window->app_id : "",
                   window->title != NULL ? window->title : "");
        }
    }
}

int main(int argc, char **argv)
{
    struct app app = {.windows_tail = &app.windows};
    struct window *target;
    bool reverse = false;
    bool list_only = false;
    bool apps = false;
    bool dry_run = false;
    bool switcher = false;
    bool tracker = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--reverse") == 0) {
            reverse = true;
        } else if (strcmp(argv[i], "--list") == 0) {
            list_only = true;
        } else if (strcmp(argv[i], "--apps") == 0) {
            apps = true;
        } else if (strcmp(argv[i], "--switcher") == 0) {
            switcher = true;
            apps = true;
        } else if (strcmp(argv[i], "--track-focus") == 0) {
            tracker = true;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: %s [--apps | --switcher | --track-focus] [--reverse] [--list] [--dry-run]\n", argv[0]);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "usage: %s [--apps | --switcher | --track-focus] [--reverse] [--list] [--dry-run]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (tracker) return track_focus(&app);
    if (switcher && !list_only && !dry_run) {
        int started = switcher_begin(reverse);
        if (started == 0) return EXIT_SUCCESS;
        if (started < 0) {
            fprintf(stderr, "cosmic-cycle-app: cannot start application switcher\n");
            return EXIT_FAILURE;
        }
        ensure_focus_tracker();
    }
    if (collect_windows(&app) < 0) {
        return EXIT_FAILURE;
    }
    if (list_only) {
        print_windows(&app);
        return EXIT_SUCCESS;
    }

    target = switcher ? choose_app_panel(&app, reverse, dry_run)
                      : choose_window(&app, reverse, apps);
    if (target == NULL) {
        return EXIT_SUCCESS;
    }
    if (dry_run) {
        printf("%s\t%s\n", target->app_id,
               target->title != NULL ? target->title : "");
        return EXIT_SUCCESS;
    }

    if (target->minimized) {
        zcosmic_toplevel_manager_v1_unset_minimized(
            app.toplevel_manager, target->cosmic);
    }
    zcosmic_toplevel_manager_v1_activate(
        app.toplevel_manager, target->cosmic, app.seat);
    if (wl_display_roundtrip(app.display) < 0) {
        fprintf(stderr,
                "cosmic-cycle-app: activation request failed\n");
        return EXIT_FAILURE;
    }

    if (apps && !switcher) {
        show_app_indicator(target);
    }

    return EXIT_SUCCESS;
}
