#ifndef COSMIC_APP_SWITCHER_H
#define COSMIC_APP_SWITCHER_H
#include <stdbool.h>
#include <stddef.h>

/* 1 = own the switcher, 0 = delivered to an existing switcher, -1 = error. */
int switcher_begin(bool reverse);
/* Returns chosen app index, or -1 on cancel/error. */
int switcher_show(const char **ids, size_t count, size_t selected);
#endif
