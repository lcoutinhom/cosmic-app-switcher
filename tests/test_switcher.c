#include "switcher.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"
#include <wayland-server.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct server {
    struct wl_resource *keyboard, *pointer, *surface, *layer;
    bool attached, mapped, exclusive;
    int scenario;
};

static void destroy(struct wl_client *client, struct wl_resource *r)
{ (void)client; wl_resource_destroy(r); }
static void attach(struct wl_client *c, struct wl_resource *r,
                   struct wl_resource *buffer, int32_t x, int32_t y)
{
    (void)c; (void)x; (void)y;
    struct server *s = wl_resource_get_user_data(r);
    if (r == s->surface) s->attached = buffer != NULL;
}
static void damage(struct wl_client *c, struct wl_resource *r,
                   int32_t x, int32_t y, int32_t w, int32_t h)
{ (void)c; (void)r; (void)x; (void)y; (void)w; (void)h; }
static void commit(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    struct server *s = wl_resource_get_user_data(resource);
    if (!s->layer || resource != s->surface) return;
    if (!s->attached) {
        zwlr_layer_surface_v1_send_configure(s->layer, 1, 0, 0);
        return;
    }
    if (s->mapped) return;
    s->mapped = true;
    assert(s->exclusive);
    assert(s->keyboard);
    struct wl_array keys;
    wl_array_init(&keys);
    if (s->scenario != 3) {
        uint32_t *alt = wl_array_add(&keys, sizeof(*alt));
        *alt = 56;
    }
    wl_keyboard_send_enter(s->keyboard, 2, resource, &keys);
    wl_array_release(&keys);
    wl_keyboard_send_modifiers(s->keyboard, 3, 8, 0, 0, 0);
    if (s->scenario == 0) {
        wl_keyboard_send_key(s->keyboard, 4, 10, 15, WL_KEYBOARD_KEY_STATE_PRESSED);
    } else if (s->scenario == 1) {
        wl_keyboard_send_key(s->keyboard, 4, 10, 1, WL_KEYBOARD_KEY_STATE_PRESSED);
    } else if (s->scenario == 2) {
        wl_keyboard_send_modifiers(s->keyboard, 4, 9, 0, 0, 0);
        wl_keyboard_send_key(s->keyboard, 5, 10, 15, WL_KEYBOARD_KEY_STATE_PRESSED);
    } else if (s->scenario == 4 || s->scenario == 5) {
        assert(s->pointer);
        int x = s->scenario == 4 ? 40 : 270;
        wl_pointer_send_enter(s->pointer, 4, resource,
                              wl_fixed_from_int(x), wl_fixed_from_int(60));
        wl_pointer_send_motion(s->pointer, 10,
                               wl_fixed_from_int(x + 10), wl_fixed_from_int(60));
        if (s->scenario == 4) {
            wl_pointer_send_button(s->pointer, 5, 11, 0x110, WL_POINTER_BUTTON_STATE_PRESSED);
            wl_pointer_send_button(s->pointer, 6, 12, 0x110, WL_POINTER_BUTTON_STATE_RELEASED);
        }
    }
    wl_keyboard_send_key(s->keyboard, 6, 11, 56, WL_KEYBOARD_KEY_STATE_RELEASED);
}
static const struct wl_surface_interface surface_impl = {
    .destroy = destroy, .attach = attach, .damage = damage, .commit = commit,
};
static void create_surface(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
    struct server *s = wl_resource_get_user_data(r);
    struct wl_resource *surface = wl_resource_create(c, &wl_surface_interface, 1, id);
    wl_resource_set_implementation(surface, &surface_impl, s, NULL);
}
static const struct wl_compositor_interface compositor_impl = {
    .create_surface = create_surface,
};
static void bind_compositor(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)version;
    struct wl_resource *r = wl_resource_create(c, &wl_compositor_interface, 1, id);
    wl_resource_set_implementation(r, &compositor_impl, data, NULL);
}
static void get_keyboard(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
    struct server *s = wl_resource_get_user_data(r);
    s->keyboard = wl_resource_create(c, &wl_keyboard_interface, 1, id);
    int fd = open("/dev/null", O_RDONLY);
    wl_keyboard_send_keymap(s->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, fd, 0);
    close(fd);
}
static void set_cursor(struct wl_client *c, struct wl_resource *r, uint32_t serial,
                       struct wl_resource *surface, int32_t x, int32_t y)
{ (void)c; (void)r; (void)serial; (void)surface; (void)x; (void)y; }
static const struct wl_pointer_interface pointer_impl = {.set_cursor = set_cursor};
static void get_pointer(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
    struct server *s = wl_resource_get_user_data(r);
    s->pointer = wl_resource_create(c, &wl_pointer_interface, 1, id);
    wl_resource_set_implementation(s->pointer, &pointer_impl, s, NULL);
}
static const struct wl_seat_interface seat_impl = {
    .get_keyboard = get_keyboard, .get_pointer = get_pointer,
};
static void bind_seat(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)version;
    struct wl_resource *r = wl_resource_create(c, &wl_seat_interface, 1, id);
    wl_resource_set_implementation(r, &seat_impl, data, NULL);
    wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
}
static void set_size(struct wl_client *c, struct wl_resource *r, uint32_t w, uint32_t h)
{ (void)c; (void)r; assert(w > 0 && h > 0); }
static void set_zone(struct wl_client *c, struct wl_resource *r, int32_t zone)
{ (void)c; (void)r; assert(zone == -1); }
static void set_keyboard(struct wl_client *c, struct wl_resource *r, uint32_t value)
{
    (void)c;
    ((struct server *)wl_resource_get_user_data(r))->exclusive = value == 1;
}
static void ack(struct wl_client *c, struct wl_resource *r, uint32_t serial)
{ (void)c; (void)r; assert(serial == 1); }
static const struct zwlr_layer_surface_v1_interface layer_impl = {
    .destroy = destroy, .set_size = set_size, .set_exclusive_zone = set_zone,
    .set_keyboard_interactivity = set_keyboard, .ack_configure = ack,
};
static void get_layer(struct wl_client *c, struct wl_resource *r, uint32_t id,
                      struct wl_resource *surface, struct wl_resource *output,
                      uint32_t layer, const char *name)
{
    (void)output;
    struct server *s = wl_resource_get_user_data(r);
    assert(layer == 3);
    s->surface = surface;
    assert(strcmp(name, "cosmic-app-switcher") == 0);
    s->layer = wl_resource_create(c, &zwlr_layer_surface_v1_interface, 1, id);
    wl_resource_set_implementation(s->layer, &layer_impl, s, NULL);
}
static const struct zwlr_layer_shell_v1_interface shell_impl = {.get_layer_surface = get_layer};
static void bind_shell(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    (void)version;
    struct wl_resource *r = wl_resource_create(c, &zwlr_layer_shell_v1_interface, 1, id);
    wl_resource_set_implementation(r, &shell_impl, data, NULL);
}

int main(void)
{
    char runtime[] = "/tmp/cosmic-switcher-test.XXXXXX";
    assert(mkdtemp(runtime));
    assert(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
    char directory[4096];
    snprintf(directory, sizeof(directory), "%s/cosmic-cycle-app", runtime);
    assert(mkdir(directory, 0700) == 0);
    for (int scenario = 0; scenario < 6; ++scenario) {
        struct server s = {.scenario = scenario};
        struct wl_display *display = wl_display_create();
        assert(display);
        assert(wl_display_init_shm(display) == 0);
        int connection[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, connection) == 0);
        /* Some managed environments forbid the peer-credential query which
         * libwayland-server needs, even for an in-process socketpair. */
        char credentials[128];
        socklen_t credential_size = sizeof(credentials);
        if (getsockopt(connection[0], SOL_SOCKET, 17, credentials, &credential_size) != 0 &&
            (errno == EPERM || errno == EACCES)) {
            close(connection[0]); close(connection[1]);
            wl_display_destroy(display);
            rmdir(directory); rmdir(runtime);
            puts("SKIP: native compositor test requires socket peer credentials denied by sandbox.");
            return 77;
        }
        wl_global_create(display, &wl_compositor_interface, 1, &s, bind_compositor);
        wl_global_create(display, &wl_seat_interface, 1, &s, bind_seat);
        wl_global_create(display, &zwlr_layer_shell_v1_interface, 1, &s, bind_shell);
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            close(connection[0]);
            char fd[32];
            snprintf(fd, sizeof(fd), "%d", connection[1]);
            assert(setenv("WAYLAND_SOCKET", fd, 1) == 0);
            const char *ids[] = {"firefox", "com.system76.CosmicTerm", "com.system76.CosmicFiles"};
            int selected = switcher_show(ids, 3, 1);
            int expected[] = {2, -1, 0, 1, 0, 2};
            exit(selected == expected[scenario] ? 0 : 1);
        }
        close(connection[1]);
        assert(wl_client_create(display, connection[0]));
        struct wl_event_loop *loop = wl_display_get_event_loop(display);
        int status = 0, elapsed = 0;
        while (waitpid(child, &status, WNOHANG) == 0 && elapsed < 6000) {
            wl_event_loop_dispatch(loop, 10);
            wl_display_flush_clients(display);
            elapsed += 10;
        }
        if (elapsed >= 6000) { kill(child, SIGKILL); waitpid(child, &status, 0); }
        assert(elapsed < 6000);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        assert(s.mapped);
        wl_display_destroy_clients(display);
        wl_display_destroy(display);
    }
    char path[4096];
    snprintf(path, sizeof(path), "%s/cosmic-cycle-app/switcher.lock", runtime);
    unlink(path);
    snprintf(path, sizeof(path), "%s/cosmic-cycle-app", runtime);
    rmdir(path);
    rmdir(runtime);
    puts("Native Wayland switcher: keyboard navigation, cancellation, quick Alt release and mouse selection passed.");
    return 0;
}
