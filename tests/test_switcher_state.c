/* Exercise keyboard state independently of a desktop or socket permissions. */
#include "../src/switcher.c"
#include <assert.h>

static struct panel panel(void)
{
    struct panel p = {.count = 3, .selected = 1};
    return p;
}

static void frame_cache(void)
{
    char temp[] = "/tmp/cosmic-switcher-cache.XXXXXX";
    assert(mkdtemp(temp));
    assert(setenv("XDG_RUNTIME_DIR", temp, 1) == 0);
    char directory[4096], cache[4096];
    snprintf(directory, sizeof(directory), "%s/cosmic-cycle-app", temp);
    snprintf(cache, sizeof(cache), "%s/cosmic-cycle-app/frames.cache", temp);
    assert(mkdir(directory, 0700) == 0);
    const char *ids[] = {"firefox", "com.system76.CosmicTerm", "com.system76.CosmicFiles",
                        "code", "org.gnome.TextEditor", "com.system76.CosmicSettings"};
    struct panel p = {.count = 6, .width = 720, .height = 204};
    long start = now_ms();
    int first = prepare_frames(ids, &p);
    long cold = now_ms() - start;
    assert(first >= 0);
    struct stat initial, hit, changed;
    assert(fstat(first, &initial) == 0);
    start = now_ms();
    for (int i = 0; i < 20; ++i) {
        int cached = prepare_frames(ids, &p);
        assert(cached >= 0 && fstat(cached, &hit) == 0);
        assert(initial.st_ino == hit.st_ino);
        /* Match the compositor's wl_shm pool mapping, which requires a
         * writable file descriptor on cache hits as well as fresh renders. */
        void *mapped = mmap(NULL, (size_t)hit.st_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, cached, 0);
        assert(mapped != MAP_FAILED);
        assert(munmap(mapped, (size_t)hit.st_size) == 0);
        close(cached);
    }
    printf("Rendering benchmark: cold %ld ms; 20 cached loads %ld ms total.\n",
           cold, now_ms() - start);
    ids[0] = "other.app";
    int next = prepare_frames(ids, &p);
    assert(next >= 0 && fstat(next, &changed) == 0);
    assert(initial.st_ino != changed.st_ino);
    /* Old compositor buffers retain their original backing file. */
    assert(fstat(first, &hit) == 0 && hit.st_size == initial.st_size);
    close(next);
    p.width = 608;
    next = prepare_frames(ids, &p);
    assert(next >= 0 && fstat(next, &hit) == 0);
    assert(hit.st_ino != changed.st_ino);
    assert(hit.st_size == (off_t)(608 * 204 * 4 * 6 + sizeof(uint64_t)));
    close(next);
    assert(truncate(cache, 100) == 0);
    next = prepare_frames(ids, &p);
    assert(next >= 0 && fstat(next, &hit) == 0);
    assert(hit.st_size == (off_t)(608 * 204 * 4 * 6 + sizeof(uint64_t)));
    struct timespec old[2] = {{.tv_sec = time(NULL) - 301}, {.tv_sec = time(NULL) - 301}};
    assert(utimensat(AT_FDCWD, cache, old, 0) == 0);
    int refreshed = prepare_frames(ids, &p);
    assert(refreshed >= 0 && fstat(refreshed, &changed) == 0);
    assert(hit.st_ino != changed.st_ino);
    close(refreshed); close(next); close(first);
    unlink(cache); rmdir(directory); rmdir(temp);
    puts("Frame cache: reuse, app/dimension changes, corruption, expiry and immutable buffers passed.");
}

int main(void)
{
    struct panel p = panel();
    key(&p, NULL, 0, 0, 15, WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(p.selected == 2 && !p.done && !p.confirmed);
    key(&p, NULL, 0, 0, 15, WL_KEYBOARD_KEY_STATE_RELEASED);
    assert(p.selected == 2);
    key(&p, NULL, 0, 0, 15, WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(p.selected == 0);
    key(&p, NULL, 0, 0, 56, WL_KEYBOARD_KEY_STATE_RELEASED);
    assert(p.done && p.confirmed);
    key(&p, NULL, 0, 0, 15, WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(p.selected == 0);

    p = panel();
    modifiers(&p, NULL, 0, 9, 0, 0, 0);
    key(&p, NULL, 0, 0, 15, WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(p.selected == 0 && !p.done);
    key(&p, NULL, 0, 0, 15, WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(p.selected == 2);

    p = panel();
    key(&p, NULL, 0, 0, 1, WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(p.done && !p.confirmed);
    key(&p, NULL, 0, 0, 56, WL_KEYBOARD_KEY_STATE_RELEASED);
    assert(p.done && !p.confirmed);

    p = panel();
    key(&p, NULL, 0, 0, 28, WL_KEYBOARD_KEY_STATE_PRESSED);
    assert(p.done && p.confirmed);

    struct wl_array held;
    wl_array_init(&held);
    p = panel();
    enter(&p, NULL, 0, NULL, &held);
    assert(p.done && p.confirmed);
    uint32_t *alt = wl_array_add(&held, sizeof(*alt));
    *alt = 56;
    p = panel();
    enter(&p, NULL, 0, NULL, &held);
    assert(!p.done && p.entered);
    leave(&p, NULL, 0, NULL);
    assert(p.done && !p.confirmed);
    wl_array_release(&held);
    puts("Switcher keyboard states: Tab, wrap, Shift, Alt release, Esc, Enter and focus loss passed.");
    frame_cache();
    return 0;
}
