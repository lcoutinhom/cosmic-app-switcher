#include "switcher.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int command_fd = -1;
static int lock_fd = -1;
static char socket_path[108];

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void close_commands(void)
{
    if (command_fd >= 0) {
        close(command_fd);
        command_fd = -1;
        unlink(socket_path);
    }
    if (lock_fd >= 0) {
        close(lock_fd);
        lock_fd = -1;
    }
}

int switcher_begin(bool reverse)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime == NULL) return -1;
    char directory[4096], path[4096];
    if (snprintf(directory, sizeof(directory), "%s/cosmic-cycle-app", runtime)
        >= (int)sizeof(directory)) return -1;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return -1;
    struct stat st;
    if (lstat(directory, &st) != 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != getuid() || (st.st_mode & 0077) != 0) return -1;
    if (snprintf(path, sizeof(path), "%s/switcher.lock", directory)
        >= (int)sizeof(path)) return -1;
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (snprintf(socket_path, sizeof(socket_path), "%s/switcher.sock", directory)
        >= (int)sizeof(socket_path)) { close(fd); return -1; }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1);
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        int sender = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (sender < 0) return -1;
        char direction = reverse ? '-' : '+';
        long deadline = now_ms() + 150;
        int result = -1;
        do {
            if (sendto(sender, &direction, 1, MSG_DONTWAIT,
                       (struct sockaddr *)&address, sizeof(address)) == 1) {
                result = 0;
                break;
            }
            struct timespec pause = {.tv_nsec = 5000000};
            nanosleep(&pause, NULL);
        } while (now_ms() < deadline);
        close(sender);
        return result;
    }
    lock_fd = fd;
    command_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    unlink(socket_path);
    if (command_fd < 0 || bind(command_fd, (struct sockaddr *)&address,
                              sizeof(address)) != 0) {
        close_commands();
        return -1;
    }
    atexit(close_commands);
    return 1;
}

struct panel {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    struct zwlr_layer_shell_v1 *shell;
    struct zwp_keyboard_shortcuts_inhibit_manager_v1 *inhibit_manager;
    struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    struct wl_buffer **buffers;
    size_t count, selected;
    int width, height;
    bool done, confirmed, shift, configured, entered;
    bool pointer_inside;
    double pointer_x, pointer_y;
    int pressed_item;
};

static void paint(struct panel *p)
{
    if (!p->configured || p->done) return;
    wl_surface_attach(p->surface, p->buffers[p->selected], 0, 0);
    wl_surface_damage(p->surface, 0, 0, p->width, p->height);
    wl_surface_commit(p->surface);
}

static void step(struct panel *p, bool reverse)
{
    if (p->done) return;
    p->selected = reverse ? (p->selected + p->count - 1) % p->count
                          : (p->selected + 1) % p->count;
    paint(p);
}

static void keymap(void *data, struct wl_keyboard *keyboard,
                   uint32_t format, int32_t fd, uint32_t size)
{
    (void)data; (void)keyboard; (void)format; (void)size;
    close(fd);
}

static void enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                  struct wl_surface *surface, struct wl_array *keys)
{
    (void)keyboard; (void)serial; (void)surface;
    struct panel *p = data;
    p->entered = true;
    bool alt = false;
    uint32_t *key;
    wl_array_for_each(key, keys) {
        if (*key == 56 || *key == 100) alt = true;
        if (*key == 42 || *key == 54) p->shift = true;
    }
    /* A quick tap may release Alt before the panel is ready. */
    if (!alt) { p->done = true; p->confirmed = true; }
}

static void leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                  struct wl_surface *surface)
{
    (void)keyboard; (void)serial; (void)surface;
    ((struct panel *)data)->done = true;
}

static void key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                uint32_t time, uint32_t code, uint32_t state)
{
    (void)keyboard; (void)serial; (void)time;
    struct panel *p = data;
    bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (p->done) return;
    if (code == 42 || code == 54) p->shift = pressed;
    if ((code == 56 || code == 100) && !pressed) {
        p->done = true; p->confirmed = true;
    } else if (pressed && code == 1) {
        p->done = true;
    } else if (pressed && (code == 28 || code == 96)) {
        p->done = true; p->confirmed = true;
    } else if (pressed && (code == 15 || code == 105 || code == 106)) {
        step(p, code == 105 || (code == 15 && p->shift));
    }
}

static void modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                      uint32_t depressed, uint32_t latched, uint32_t locked,
                      uint32_t group)
{
    (void)keyboard; (void)serial; (void)locked; (void)group;
    struct panel *p = data;
    /* Standard XKB Shift mask; physical key events also update this state. */
    p->shift = ((depressed | latched) & 1) != 0;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keymap, .enter = enter, .leave = leave,
    .key = key, .modifiers = modifiers,
};

/* The renderer uses fixed pages of seven icons. Hovering an icon never
 * moves the page underneath the cursor; only explicit navigation does. */
static int pointer_item(const struct panel *p)
{
    if (!p->pointer_inside || p->count == 0 || p->pointer_y < 24 ||
        p->pointer_y >= 140 || p->pointer_x < 27) return -1;
    int slot = (int)((p->pointer_x - 24) / 112);
    if (slot < 0 || slot >= 7) return -1;
    double local = p->pointer_x - (24 + slot * 112);
    if (local < 3 || local >= 109) return -1;
    size_t item = (p->selected / 7) * 7 + (size_t)slot;
    return item < p->count ? (int)item : -1;
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    (void)surface;
    struct panel *p = data;
    p->pointer_inside = true;
    p->pointer_x = wl_fixed_to_double(x);
    p->pointer_y = wl_fixed_to_double(y);
    p->pressed_item = -1;
    /* Do not override the keyboard selection merely because the panel opens
     * under a stationary cursor. The first actual motion enables hovering. */
    if (p->cursor_theme && p->cursor_surface) {
        struct wl_cursor *cursor = wl_cursor_theme_get_cursor(p->cursor_theme, "left_ptr");
        if (cursor && cursor->image_count) {
            struct wl_cursor_image *image = cursor->images[0];
            struct wl_buffer *buffer = wl_cursor_image_get_buffer(image);
            if (buffer) {
                wl_surface_attach(p->cursor_surface, buffer, 0, 0);
                wl_surface_damage(p->cursor_surface, 0, 0, image->width, image->height);
                wl_surface_commit(p->cursor_surface);
                wl_pointer_set_cursor(pointer, serial, p->cursor_surface,
                                      image->hotspot_x, image->hotspot_y);
            }
        }
    }
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface)
{
    (void)pointer; (void)serial; (void)surface;
    struct panel *p = data;
    p->pointer_inside = false;
    p->pressed_item = -1;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t x, wl_fixed_t y)
{
    (void)pointer; (void)time;
    struct panel *p = data;
    if (p->done) return;
    double next_x = wl_fixed_to_double(x), next_y = wl_fixed_to_double(y);
    bool moved = next_x != p->pointer_x || next_y != p->pointer_y;
    p->pointer_x = next_x; p->pointer_y = next_y;
    int item = pointer_item(p);
    if (moved && item >= 0 && p->selected != (size_t)item) {
        p->selected = (size_t)item;
        paint(p);
    }
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state)
{
    (void)pointer; (void)serial; (void)time;
    struct panel *p = data;
    if (p->done || button != 0x110) return; /* Linux BTN_LEFT */
    int item = pointer_item(p);
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        p->pressed_item = item;
        if (item >= 0) {
            p->selected = (size_t)item;
            paint(p);
        }
    } else {
        if (item >= 0 && p->pressed_item == item) {
            p->selected = (size_t)item;
            p->done = true; p->confirmed = true;
        }
        p->pressed_item = -1;
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value)
{
    (void)pointer; (void)time; (void)axis;
    struct panel *p = data;
    if (value != 0) {
        p->pressed_item = -1;
        step(p, value < 0);
    }
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
    .button = pointer_button, .axis = pointer_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    struct panel *p = data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !p->pointer) {
        p->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(p->pointer, &pointer_listener, p);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && p->pointer) {
        wl_pointer_destroy(p->pointer);
        p->pointer = NULL;
        p->pointer_inside = false;
        p->pressed_item = -1;
    }
}

static const struct wl_seat_listener seat_listener = {.capabilities = seat_capabilities};

static void registry(void *data, struct wl_registry *registry, uint32_t name,
                     const char *interface, uint32_t version)
{
    (void)version;
    struct panel *p = data;
    if (strcmp(interface, "wl_compositor") == 0)
        p->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    else if (strcmp(interface, "wl_shm") == 0)
        p->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (strcmp(interface, "wl_seat") == 0 && p->seat == NULL) {
        p->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(p->seat, &seat_listener, p);
    }
    else if (strcmp(interface, "zwlr_layer_shell_v1") == 0)
        p->shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    else if (strcmp(interface, "zwp_keyboard_shortcuts_inhibit_manager_v1") == 0)
        p->inhibit_manager = wl_registry_bind(registry, name,
            &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{ (void)data; (void)registry; (void)name; }
static const struct wl_registry_listener registry_listener = {
    .global = registry, .global_remove = registry_remove,
};

static void configure(void *data, struct zwlr_layer_surface_v1 *layer,
                      uint32_t serial, uint32_t width, uint32_t height)
{
    (void)width; (void)height;
    struct panel *p = data;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    p->configured = true;
    paint(p);
}
static void closed(void *data, struct zwlr_layer_surface_v1 *layer)
{ (void)layer; ((struct panel *)data)->done = true; }
static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = configure, .closed = closed,
};

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

/* Keep one immutable pixel file in the private runtime directory. Reusing it
 * skips Python/GTK startup entirely; atomic replacement leaves existing SHM
 * buffers valid. A trailer keys it to app order, dimensions and renderer. */
static int prepare_frames(const char **ids, struct panel *p)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR"), *home = getenv("HOME");
    if (!runtime || !home) return -1;
    char image[4096], cache[4096], script[4096], width[20], height[20];
    if (snprintf(image, sizeof(image), "%s/cosmic-cycle-app/frames.XXXXXX", runtime)
        >= (int)sizeof(image) ||
        snprintf(script, sizeof(script), "%s/.local/lib/cosmic-cycle-app/render_switcher.py", home)
        >= (int)sizeof(script)) return -1;
    snprintf(width, sizeof(width), "%d", p->width);
    snprintf(height, sizeof(height), "%d", p->height);
    const char *renderer = getenv("COSMIC_CYCLE_RENDERER");
    if (renderer != NULL && renderer[0] == '/') {
        if (snprintf(script, sizeof(script), "%s", renderer) >= (int)sizeof(script))
            return -1;
    }
    if (snprintf(cache, sizeof(cache), "%s/cosmic-cycle-app/frames.cache", runtime)
        >= (int)sizeof(cache)) return -1;
    size_t pixels = (size_t)p->width * p->height * 4 * p->count;
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = hash_bytes(hash, &p->width, sizeof(p->width));
    hash = hash_bytes(hash, &p->height, sizeof(p->height));
    for (size_t i = 0; i < p->count; ++i)
        hash = hash_bytes(hash, ids[i], strlen(ids[i]) + 1);
    hash = hash_bytes(hash, script, strlen(script));
    struct stat renderer_stat;
    if (stat(script, &renderer_stat) != 0) return -1;
    hash = hash_bytes(hash, &renderer_stat.st_mtim.tv_sec, sizeof(renderer_stat.st_mtim.tv_sec));
    hash = hash_bytes(hash, &renderer_stat.st_mtim.tv_nsec, sizeof(renderer_stat.st_mtim.tv_nsec));
    hash = hash_bytes(hash, &renderer_stat.st_size, sizeof(renderer_stat.st_size));
    const char *locale_names[] = {"LANG", "LC_ALL", "LC_MESSAGES", NULL};
    for (size_t i = 0; locale_names[i]; ++i) {
        const char *value = getenv(locale_names[i]);
        if (value) hash = hash_bytes(hash, value, strlen(value) + 1);
    }
    /* wl_shm servers map the pool PROT_READ | PROT_WRITE, even though we
     * never modify cached pixels. A read-only fd fails that shared mapping. */
    int cached = open(cache, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (cached >= 0) {
        struct stat st;
        uint64_t stored;
        time_t now = time(NULL);
        if (fstat(cached, &st) == 0 && S_ISREG(st.st_mode) &&
            st.st_uid == getuid() && st.st_size == (off_t)(pixels + sizeof(hash)) &&
            now >= st.st_mtime && now - st.st_mtime <= 300 &&
            pread(cached, &stored, sizeof(stored), (off_t)pixels) == sizeof(stored) &&
            stored == hash) return cached;
        close(cached);
    }
    int fd = mkstemp(image);
    if (fd < 0) return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    char **args = calloc(p->count + 6, sizeof(*args));
    if (!args) { close(fd); unlink(image); return -1; }
    args[0] = "python3"; args[1] = script; args[2] = image;
    args[3] = width; args[4] = height;
    for (size_t i = 0; i < p->count; ++i) args[5 + i] = (char *)ids[i];
    pid_t child = fork();
    if (child == 0) {
        execv("/usr/bin/python3", args);
        _exit(127);
    }
    free(args);
    if (child < 0) { close(fd); unlink(image); return -1; }
    int status = 0;
    long deadline = now_ms() + 4000;
    while (waitpid(child, &status, WNOHANG) == 0) {
        if (now_ms() >= deadline) {
            kill(child, SIGKILL); waitpid(child, &status, 0);
            close(fd); unlink(image); return -1;
        }
        struct timespec pause = {.tv_nsec = 5000000};
        nanosleep(&pause, NULL);
    }
    struct stat st;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || fstat(fd, &st) != 0 ||
        st.st_size != (off_t)pixels) { close(fd); unlink(image); return -1; }
    if (pwrite(fd, &hash, sizeof(hash), (off_t)pixels) == sizeof(hash)) {
        (void)rename(image, cache);
    }
    unlink(image);
    return fd;
}

static int render(const char **ids, struct panel *p)
{
    int fd = prepare_frames(ids, p);
    if (fd < 0) return -1;
    size_t frame = (size_t)p->width * p->height * 4;
    struct wl_shm_pool *pool = wl_shm_create_pool(p->shm, fd, (int32_t)(frame * p->count));
    close(fd);
    p->buffers = calloc(p->count, sizeof(*p->buffers));
    if (!p->buffers) { wl_shm_pool_destroy(pool); return -1; }
    for (size_t i = 0; i < p->count; ++i) {
        p->buffers[i] = wl_shm_pool_create_buffer(pool, (int32_t)(i * frame),
            p->width, p->height, p->width * 4, WL_SHM_FORMAT_ARGB8888);
    }
    wl_shm_pool_destroy(pool);
    return 0;
}

int switcher_show(const char **ids, size_t count, size_t selected)
{
    if (count < 2 || count > 128 || selected >= count) return -1;
    struct panel p = {.count = count, .selected = selected,
        .width = (int)(count < 7 ? count : 7) * 112 + 48, .height = 204,
        .pressed_item = -1};
    p.display = wl_display_connect(NULL);
    if (!p.display) return -1;
    struct wl_registry *reg = wl_display_get_registry(p.display);
    wl_registry_add_listener(reg, &registry_listener, &p);
    if (wl_display_roundtrip(p.display) < 0 || !p.compositor || !p.shm ||
        !p.seat || !p.shell || render(ids, &p) != 0) {
        fprintf(stderr, "cosmic-cycle-app: cannot prepare native switcher\n");
        wl_display_disconnect(p.display);
        return -1;
    }
    p.keyboard = wl_seat_get_keyboard(p.seat);
    wl_keyboard_add_listener(p.keyboard, &keyboard_listener, &p);
    p.surface = wl_compositor_create_surface(p.compositor);
    p.cursor_theme = wl_cursor_theme_load(NULL, 24, p.shm);
    if (p.cursor_theme) p.cursor_surface = wl_compositor_create_surface(p.compositor);
    p.layer = zwlr_layer_shell_v1_get_layer_surface(p.shell, p.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "cosmic-app-switcher");
    zwlr_layer_surface_v1_add_listener(p.layer, &layer_listener, &p);
    zwlr_layer_surface_v1_set_size(p.layer, p.width, p.height);
    zwlr_layer_surface_v1_set_exclusive_zone(p.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(p.layer,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    if (p.inhibit_manager)
        p.inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
            p.inhibit_manager, p.surface, p.seat);
    wl_surface_commit(p.surface);
    long deadline = now_ms() + 15000;
    while (!p.done && now_ms() < deadline) {
        int prepared;
        while ((prepared = wl_display_prepare_read(p.display)) != 0) {
            if (wl_display_dispatch_pending(p.display) < 0) { p.done = true; break; }
        }
        if (p.done) {
            if (prepared == 0) wl_display_cancel_read(p.display);
            break;
        }
        wl_display_flush(p.display);
        struct pollfd fds[2] = {
            {.fd = wl_display_get_fd(p.display), .events = POLLIN},
            {.fd = command_fd, .events = POLLIN},
        };
        int ready = poll(fds, 2, 100);
        if (ready > 0 && (fds[0].revents & POLLIN)) {
            if (wl_display_read_events(p.display) < 0) break;
            if (wl_display_dispatch_pending(p.display) < 0) break;
        } else {
            wl_display_cancel_read(p.display);
        }
        if ((ready < 0 && errno != EINTR) ||
            (fds[0].revents & (POLLERR | POLLHUP))) break;
        if (fds[1].revents & POLLIN) {
            char direction;
            while (recv(command_fd, &direction, 1, MSG_DONTWAIT) == 1)
                step(&p, direction == '-');
        }
    }
    int result = p.confirmed ? (int)p.selected : -1;
    if (p.inhibitor) zwp_keyboard_shortcuts_inhibitor_v1_destroy(p.inhibitor);
    zwlr_layer_surface_v1_destroy(p.layer);
    wl_surface_destroy(p.surface);
    wl_display_roundtrip(p.display);
    if (p.cursor_surface) wl_surface_destroy(p.cursor_surface);
    if (p.cursor_theme) wl_cursor_theme_destroy(p.cursor_theme);
    for (size_t i = 0; i < count; ++i) wl_buffer_destroy(p.buffers[i]);
    free(p.buffers);
    wl_display_disconnect(p.display);
    return result;
}
