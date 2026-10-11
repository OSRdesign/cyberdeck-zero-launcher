/*
 * Unit test of the on-screen keyboard's injection path (task 014 P2b, F1 phase 1 items 4-5): cp0_keyboard_inject_key()
 * through the real key queue and Esc state, driven by the keyboard core's event sequences (src/cp0_ui_osk.c):
 *   - a key is a press AND a release, both with the key code and the text (consumers read either state);
 *   - Ctrl+C is KEY_C with KBD_MOD_CTRL, press then release, no text;
 *   - the on-screen Esc moves the Esc state like the toolbar's (hold-to-exit), and its release is always queued when
 *     the keyboard goes away mid-press, so the Esc state never stays down;
 *   - the text hint.
 */
#include "cp0_esc_state.h"
#include "cp0_keyboard_queue.h"
#include "cp0_ui_osk.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* what a backend provides (the device reader, SDL or the render harness) */
struct keyboard_queue_t keyboard_queue;
pthread_mutex_t keyboard_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int LVGL_RUN_FLAGE = 1;
volatile uint32_t LV_EVENT_KEYBOARD;
static cp0_keyboard_input_context_t context = KBD_INPUT_CONTEXT_NAVIGATION;
void cp0_keyboard_set_input_context(cp0_keyboard_input_context_t c) { context = c; }
cp0_keyboard_input_context_t cp0_keyboard_get_input_context(void) { return context; }

static int failures;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

static int inject_all(const cp0_ui_osk_event_t *e, int n)
{
    for (int i = 0; i < n; ++i)
        if (cp0_keyboard_inject_key(e[i].code, e[i].state, e[i].mods, e[i].utf8) != 0) return -1;
    return 0;
}

/* pops one item and checks it; returns 1 when it matched */
static int pop_is(uint32_t code, int state, uint32_t mods, const char *utf8)
{
    struct key_item *item = cp0_keyboard_queue_pop();
    if (!item) return 0;
    const int ok = item->key_code == code && item->key_state == state && item->mods == mods &&
                   strcmp(item->utf8, utf8) == 0;
    if (!ok)
        fprintf(stderr, "item code=%u state=%d mods=0x%x utf8='%s'\n", item->key_code, item->key_state, item->mods,
                item->utf8);
    free(item);
    return ok;
}

static const cp0_ui_osk_key_t *find(cp0_ui_osk_page_t page, cp0_ui_osk_kind_t kind, uint32_t code)
{
    static cp0_ui_osk_layout_t l;
    static cp0_ui_osk_place_t p[CP0_UI_OSK_MAX_KEYS];
    cp0_ui_metrics_t m;
    cp0_ui_metrics_compute(640, 480, 0, 1130, CP0_UI_DENSITY_DEFAULT, 0, &m);
    cp0_ui_osk_layout(&m, &l);
    const int n = cp0_ui_osk_place_keys(&l, page, p, CP0_UI_OSK_MAX_KEYS);
    for (int i = 0; i < n; ++i)
        if (p[i].key->kind == kind && (!code || p[i].key->code == code)) return p[i].key;
    return NULL;
}

static void test_inject_key(void)
{
    cp0_keyboard_queue_clear();
    cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_TEXT);
    /* press and release both carry the code and the text */
    CHECK(cp0_keyboard_inject_key(KEY_A, KBD_KEY_PRESSED, 0, "a") == 0);
    CHECK(cp0_keyboard_inject_key(KEY_A, KBD_KEY_RELEASED, 0, "a") == 0);
    struct key_item *item = cp0_keyboard_queue_pop();
    CHECK(item && item->key_code == KEY_A && item->key_state == KBD_KEY_PRESSED && strcmp(item->utf8, "a") == 0 &&
          item->codepoint == 'a' && item->input_context == KBD_INPUT_CONTEXT_TEXT && item->semantic_key == KEY_A);
    free(item);
    CHECK(pop_is(KEY_A, KBD_KEY_RELEASED, 0, "a"));
    /* UTF-8 beyond ASCII, a key without a code */
    CHECK(cp0_keyboard_inject_key(0, KBD_KEY_PRESSED, 0, "\xc3\xa9") == 0);
    item = cp0_keyboard_queue_pop();
    CHECK(item && item->codepoint == 0xe9 && item->key_code == 0 && strcmp(item->utf8, "\xc3\xa9") == 0);
    free(item);
    /* in the navigation context the semantic key follows the Cardputer aliases, as for a physical key */
    cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_NAVIGATION);
    CHECK(cp0_keyboard_inject_key(KEY_C, KBD_KEY_PRESSED, 0, NULL) == 0);
    item = cp0_keyboard_queue_pop();
    CHECK(item && item->semantic_key == KEY_RIGHT && item->utf8[0] == '\0' && item->codepoint == 0);
    free(item);
    cp0_keyboard_set_input_context(KBD_INPUT_CONTEXT_TEXT);
    /* refused: a bad state, invalid or too long UTF-8 */
    CHECK(cp0_keyboard_inject_key(KEY_A, 7, 0, "a") == -1);
    CHECK(cp0_keyboard_inject_key(KEY_A, KBD_KEY_PRESSED, 0, "\xc0\x80") == -1);
    CHECK(cp0_keyboard_inject_key(KEY_A, KBD_KEY_PRESSED, 0, "0123456789abcdef") == -1);
    CHECK(cp0_keyboard_queue_pop() == NULL);
}

static void test_ctrl_c(void)
{
    cp0_keyboard_queue_clear();
    cp0_ui_osk_state_t s;
    cp0_ui_osk_state_init(&s, 0, 0);
    cp0_ui_osk_event_t e[CP0_UI_OSK_MAX_EVENTS];
    const cp0_ui_osk_key_t *ctrl = find(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CTRL, 0);
    const cp0_ui_osk_key_t *c = find(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_CHAR, KEY_C);
    CHECK(ctrl && c);
    if (!ctrl || !c) return;
    CHECK(cp0_ui_osk_key_up(&s, ctrl, 100, e, CP0_UI_OSK_MAX_EVENTS) == 0); /* arming queues nothing */
    CHECK(cp0_ui_osk_key_down(&s, c, e, CP0_UI_OSK_MAX_EVENTS) == 0);       /* a character waits for the click */
    const int n = cp0_ui_osk_key_up(&s, c, 200, e, CP0_UI_OSK_MAX_EVENTS);
    CHECK(n == 2 && inject_all(e, n) == 0);
    CHECK(pop_is(KEY_C, KBD_KEY_PRESSED, KBD_MOD_CTRL, ""));
    CHECK(pop_is(KEY_C, KBD_KEY_RELEASED, KBD_MOD_CTRL, ""));
    CHECK(cp0_keyboard_queue_pop() == NULL);
}

static void test_esc_release_on_destroy(void)
{
    cp0_keyboard_queue_clear();
    cp0_esc_state_reset();
    cp0_ui_osk_state_t s;
    cp0_ui_osk_state_init(&s, 0, 0);
    cp0_ui_osk_event_t e[CP0_UI_OSK_MAX_EVENTS];
    const cp0_ui_osk_key_t *esc = find(CP0_UI_OSK_PAGE_LETTERS, CP0_UI_OSK_K_ESC, KEY_ESC);
    CHECK(esc != NULL);
    if (!esc) return;

    /* finger down on Esc: pressed, the Esc state (hold-to-exit, the watchdog) is down like the toolbar's */
    int n = cp0_ui_osk_key_down(&s, esc, e, CP0_UI_OSK_MAX_EVENTS);
    CHECK(n == 1 && inject_all(e, n) == 0);
    CHECK(cp0_esc_state_read() == KBD_KEY_PRESSED);
    /* the field blurs on that press and the keyboard is destroyed under the finger: the release is still queued */
    n = cp0_ui_osk_release_all(&s, e, CP0_UI_OSK_MAX_EVENTS);
    CHECK(n == 1 && inject_all(e, n) == 0);
    CHECK(cp0_esc_state_read() == KBD_KEY_RELEASED);
    CHECK(pop_is(KEY_ESC, KBD_KEY_PRESSED, 0, "\x1b"));
    CHECK(pop_is(KEY_ESC, KBD_KEY_RELEASED, 0, "\x1b"));
    /* nothing more owed: a second destroy queues nothing */
    CHECK(cp0_ui_osk_release_all(&s, e, CP0_UI_OSK_MAX_EVENTS) == 0);
    CHECK(cp0_keyboard_queue_pop() == NULL);

    /* the finger slides off the key (press lost): released too */
    n = cp0_ui_osk_key_down(&s, esc, e, CP0_UI_OSK_MAX_EVENTS);
    inject_all(e, n);
    CHECK(cp0_esc_state_read() == KBD_KEY_PRESSED);
    n = cp0_ui_osk_key_cancel(&s, e, CP0_UI_OSK_MAX_EVENTS);
    CHECK(n == 1 && inject_all(e, n) == 0);
    CHECK(cp0_esc_state_read() == KBD_KEY_RELEASED);
    cp0_keyboard_queue_clear();
}

static void test_text_hint(void)
{
    CHECK(cp0_keyboard_get_text_hint() == KBD_TEXT_HINT_TEXT);
    cp0_keyboard_set_text_hint(KBD_TEXT_HINT_PASSWORD);
    CHECK(cp0_keyboard_get_text_hint() == KBD_TEXT_HINT_PASSWORD);
    cp0_keyboard_set_text_hint(KBD_TEXT_HINT_NUMBER);
    CHECK(cp0_keyboard_get_text_hint() == KBD_TEXT_HINT_NUMBER);
    cp0_keyboard_set_text_hint((cp0_keyboard_text_hint_t)9);
    CHECK(cp0_keyboard_get_text_hint() == KBD_TEXT_HINT_TEXT);
}

int main(void)
{
    cp0_keyboard_queue_init();
    test_inject_key();
    test_ctrl_c();
    test_esc_release_on_destroy();
    test_text_hint();
    if (failures) {
        fprintf(stderr, "test_osk_inject: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_osk_inject: OK\n");
    return 0;
}
