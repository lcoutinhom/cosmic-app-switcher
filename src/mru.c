#include "mru.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int runtime_path(char *path, size_t size, const char *name)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime) return -1;
    char directory[4096];
    int n = snprintf(directory, sizeof(directory), "%s/cosmic-cycle-app", runtime);
    if (n < 0 || (size_t)n >= sizeof(directory)) return -1;
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return -1;
    struct stat st;
    if (lstat(directory, &st) != 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != getuid() || (st.st_mode & 0077) != 0) return -1;
    n = snprintf(path, size, "%s/%s", directory, name);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}

void mru_clear(struct mru_history *history)
{
    for (size_t i = 0; i < history->count; ++i) free(history->ids[i]);
    memset(history, 0, sizeof(*history));
}

bool mru_touch(struct mru_history *history, const char *app_id)
{
    if (!app_id || !app_id[0] || strlen(app_id) > 1024) return false;
    size_t position = history->count;
    for (size_t i = 0; i < history->count; ++i) {
        if (strcmp(history->ids[i], app_id) == 0) { position = i; break; }
    }
    if (history->count > 0 && position == 0) return false;
    char *id;
    if (position < history->count) {
        id = history->ids[position];
    } else {
        id = strdup(app_id);
        if (!id) return false;
        if (history->count == MRU_LIMIT) {
            free(history->ids[MRU_LIMIT - 1]);
            position = MRU_LIMIT - 1;
        } else {
            ++history->count;
        }
    }
    memmove(&history->ids[1], &history->ids[0], position * sizeof(*history->ids));
    history->ids[0] = id;
    return true;
}

size_t mru_rank(const struct mru_history *history, const char *app_id)
{
    for (size_t i = 0; app_id && i < history->count; ++i)
        if (strcmp(history->ids[i], app_id) == 0) return i;
    return MRU_LIMIT;
}

void mru_sort(const struct mru_history *history, const char **ids, size_t count,
              size_t *indices)
{
    for (size_t i = 0; i < count; ++i) {
        size_t j = i;
        size_t rank = mru_rank(history, ids[i]);
        while (j > 0 && mru_rank(history, ids[indices[j - 1]]) > rank) {
            indices[j] = indices[j - 1];
            --j;
        }
        indices[j] = i;
    }
}

static bool read_all(int fd, void *data, size_t size)
{
    char *bytes = data;
    while (size) {
        ssize_t n = read(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes += n; size -= (size_t)n;
    }
    return true;
}

static bool write_all(int fd, const void *data, size_t size)
{
    const char *bytes = data;
    while (size) {
        ssize_t n = write(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes += n; size -= (size_t)n;
    }
    return true;
}

static const char magic[8] = "CMRUv1\n";

void mru_load(struct mru_history *history)
{
    mru_clear(history);
    char path[4096];
    if (runtime_path(path, sizeof(path), "focus-history") != 0) return;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    struct stat st;
    char header[sizeof(magic)];
    uint32_t count;
    bool valid = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid() &&
        st.st_size <= MRU_LIMIT * 1030 && read_all(fd, header, sizeof(header)) &&
        memcmp(header, magic, sizeof(magic)) == 0 && read_all(fd, &count, sizeof(count)) &&
        count <= MRU_LIMIT;
    for (uint32_t i = 0; valid && i < count; ++i) {
        uint32_t length;
        valid = read_all(fd, &length, sizeof(length)) && length > 0 && length <= 1024;
        if (!valid) break;
        char *id = malloc((size_t)length + 1);
        if (!id) { valid = false; break; }
        valid = read_all(fd, id, length) && memchr(id, '\0', length) == NULL;
        id[length] = '\0';
        if (!valid) { free(id); break; }
        history->ids[history->count++] = id;
    }
    close(fd);
    if (!valid) mru_clear(history);
}

int mru_save(const struct mru_history *history)
{
    char path[4096], temp[4096];
    if (runtime_path(path, sizeof(path), "focus-history") != 0 ||
        runtime_path(temp, sizeof(temp), ".focus-history.XXXXXX") != 0) return -1;
    int fd = mkstemp(temp);
    if (fd < 0) return -1;
    uint32_t count = (uint32_t)history->count;
    bool ok = write_all(fd, magic, sizeof(magic)) && write_all(fd, &count, sizeof(count));
    for (size_t i = 0; ok && i < history->count; ++i) {
        uint32_t length = (uint32_t)strlen(history->ids[i]);
        ok = write_all(fd, &length, sizeof(length)) && write_all(fd, history->ids[i], length);
    }
    if (close(fd) != 0) ok = false;
    if (ok && rename(temp, path) != 0) ok = false;
    unlink(temp);
    return ok ? 0 : -1;
}

int mru_tracker_lock(void)
{
    char path[4096];
    if (runtime_path(path, sizeof(path), "focus-tracker.lock") != 0) return -1;
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); return -1; }
    return fd;
}
