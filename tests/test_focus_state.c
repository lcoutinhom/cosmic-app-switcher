#define main switcher_program_main
#include "../src/main.c"
#undef main
#include <assert.h>

int main(void)
{
    char temp[] = "/tmp/cosmic-focus-test.XXXXXX";
    assert(mkdtemp(temp));
    assert(setenv("XDG_RUNTIME_DIR", temp, 1) == 0);
    struct app app = {.tracking = true};
    struct window browser = {.app = &app, .app_id = "browser"};
    struct window terminal = {.app = &app, .app_id = "terminal"};
    struct wl_array states;
    wl_array_init(&states);
    uint32_t *state = wl_array_add(&states, sizeof(*state));
    *state = ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
    cosmic_state(&browser, NULL, &states);
    cosmic_done(&browser, NULL);
    assert(app.history.count == 1 && mru_rank(&app.history, "browser") == 0);
    cosmic_state(&terminal, NULL, &states);
    cosmic_done(&terminal, NULL);
    assert(mru_rank(&app.history, "terminal") == 0);
    /* Repeated state updates (e.g. resizing) aren't a new focus event. */
    cosmic_state(&browser, NULL, &states);
    cosmic_done(&browser, NULL);
    assert(mru_rank(&app.history, "terminal") == 0);
    wl_array_release(&states);
    wl_array_init(&states);
    cosmic_state(&browser, NULL, &states);
    cosmic_done(&browser, NULL);
    assert(mru_rank(&app.history, "terminal") == 0);
    /* A notification/urgency state without activation must not reorder apps. */
    uint32_t *urgent = wl_array_add(&states, sizeof(*urgent));
    *urgent = 999;
    cosmic_state(&browser, NULL, &states);
    cosmic_done(&browser, NULL);
    assert(mru_rank(&app.history, "terminal") == 0);
    *urgent = ZCOSMIC_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
    cosmic_state(&browser, NULL, &states);
    cosmic_done(&browser, NULL);
    assert(mru_rank(&app.history, "browser") == 0);
    struct mru_history readback = {0};
    mru_load(&readback);
    assert(mru_rank(&readback, "browser") == 0);
    mru_clear(&readback); mru_clear(&app.history); wl_array_release(&states);
    char path[4096];
    snprintf(path, sizeof(path), "%s/cosmic-cycle-app/focus-history", temp);
    unlink(path);
    snprintf(path, sizeof(path), "%s/cosmic-cycle-app", temp);
    rmdir(path); rmdir(temp);
    puts("Focus tracking: activation updates MRU; repeated state and notifications do not reorder it.");
    return 0;
}
