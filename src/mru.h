#ifndef COSMIC_APP_MRU_H
#define COSMIC_APP_MRU_H
#include <stdbool.h>
#include <stddef.h>

#define MRU_LIMIT 128
struct mru_history {
    char *ids[MRU_LIMIT];
    size_t count;
};

void mru_clear(struct mru_history *history);
bool mru_touch(struct mru_history *history, const char *app_id);
size_t mru_rank(const struct mru_history *history, const char *app_id);
void mru_sort(const struct mru_history *history, const char **ids, size_t count,
              size_t *indices);
void mru_load(struct mru_history *history);
int mru_save(const struct mru_history *history);
/* Own the focus tracker for this runtime session; returns locked fd or -1. */
int mru_tracker_lock(void);
#endif
