#include "mru.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    struct mru_history history = {0}, loaded = {0};
    assert(!mru_touch(&history, NULL));
    assert(!mru_touch(&history, ""));
    assert(mru_touch(&history, "browser"));
    assert(history.count == 1 && mru_rank(&history, "browser") == 0);
    assert(!mru_touch(&history, "browser"));
    assert(mru_touch(&history, "terminal"));
    assert(mru_touch(&history, "editor"));
    const char *ids[] = {"browser", "unknown.one", "editor", "terminal", "unknown.two"};
    size_t order[5];
    mru_sort(&history, ids, 5, order);
    assert(order[0] == 2 && order[1] == 3 && order[2] == 0);
    assert(order[3] == 1 && order[4] == 4); /* Unknowns retain a stable order. */
    assert(mru_touch(&history, "browser"));
    mru_sort(&history, ids, 5, order);
    assert(order[0] == 0 && order[1] == 2); /* Current, previous. */
    assert(mru_touch(&history, "editor"));
    mru_sort(&history, ids, 5, order);
    assert(order[0] == 2 && order[1] == 0); /* Quick Alt+Tab toggles back. */

    char temp[] = "/tmp/cosmic-mru-test.XXXXXX";
    assert(mkdtemp(temp));
    assert(setenv("XDG_RUNTIME_DIR", temp, 1) == 0);
    assert(mru_save(&history) == 0);
    mru_load(&loaded);
    assert(loaded.count == history.count);
    for (size_t i = 0; i < history.count; ++i) assert(strcmp(history.ids[i], loaded.ids[i]) == 0);
    assert(mru_touch(&loaded, "local.snapshot"));
    mru_clear(&loaded);
    mru_load(&loaded);
    assert(mru_rank(&loaded, "local.snapshot") == MRU_LIMIT); /* Cancelled snapshots aren't saved. */
    char path[4096];
    snprintf(path, sizeof(path), "%s/cosmic-cycle-app/focus-history", temp);
    struct stat st;
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(truncate(path, 9) == 0);
    mru_load(&loaded);
    assert(loaded.count == 0); /* A partial file safely falls back to empty. */
    for (int i = 0; i < MRU_LIMIT + 20; ++i) {
        char id[32];
        snprintf(id, sizeof(id), "app.%d", i);
        assert(mru_touch(&history, id));
    }
    assert(history.count == MRU_LIMIT);
    assert(mru_rank(&history, "browser") == MRU_LIMIT);
    assert(mru_save(&history) == 0);
    mru_load(&loaded);
    assert(loaded.count == MRU_LIMIT);
    assert(strcmp(loaded.ids[0], history.ids[0]) == 0);
    mru_clear(&history); mru_clear(&loaded);
    unlink(path);
    snprintf(path, sizeof(path), "%s/cosmic-cycle-app", temp);
    rmdir(path); rmdir(temp);
    puts("MRU: recent focus order, quick switching, snapshots, stable fallback, persistence and bounds passed.");
    return 0;
}
