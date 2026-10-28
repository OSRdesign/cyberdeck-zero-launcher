/*
 * Unit test of the app contract's reader and on-screen keyboard rule (src/cp0_app_contract.c, task 014 P5): the
 * per-launch and panel environment, the state file paths, screen.state and input.state read back exactly as the
 * launcher writes them (cp0_ui_state_format, cp0_input_state_format), older and foreign files, the token fallback,
 * and the F1 rule shared with the launcher's own keyboard (cp0_ui_osk_policy_allows).
 */
#define _GNU_SOURCE

#include "cp0_app_contract.h"
#include "cp0_input_state.h"
#include "cp0_ui_metrics.h"
#include "cp0_ui_osk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        const long long va_ = (long long)(a), vb_ = (long long)(b);                                        \
        if (va_ != vb_) {                                                                                  \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s = %lld, expected %lld\n", __FILE__, __LINE__, #a, \
                    va_, vb_);                                                                             \
            failures++;                                                                                    \
        }                                                                                                  \
    } while (0)

static const char *const contract_vars[] = {
    "APPLAUNCH_CONTRACT", "APPLAUNCH_APP_MODE", "APPLAUNCH_WINDOW", "APPLAUNCH_CANVAS", "APPLAUNCH_CANVAS_SCALE",
    "APPLAUNCH_STATE_DIR", "APPLAUNCH_SCREEN_W", "APPLAUNCH_SCREEN_H", "APPLAUNCH_SCREEN_ROTATE",
    "APPLAUNCH_SCREEN_PPMM", "APPLAUNCH_SCREEN_CLASS", "APPLAUNCH_SCREEN_FB", "APPLAUNCH_SCREEN_TOUCH",
};

static void clear_env(void)
{
    for (size_t i = 0; i < sizeof(contract_vars) / sizeof(contract_vars[0]); i++) unsetenv(contract_vars[i]);
}

static cp0_ui_metrics_t compute(int w, int h, int rot, int ppmm)
{
    cp0_ui_metrics_t m;
    memset(&m, 0, sizeof(m));
    CHECK(cp0_ui_metrics_compute(w, h, rot, ppmm, ppmm > 0 ? CP0_UI_DENSITY_PANEL_MM : CP0_UI_DENSITY_DEFAULT, 0,
                                 &m) == 0);
    return m;
}

/* ------------------------------------------------------------------ environment */

static void test_env(void)
{
    clear_env();
    cp0_app_env_t env;
    /* started another way (ssh, an older launcher): no contract */
    CHECK(cp0_app_env_read(&env) == -1);
    CHECK_EQ(env.contract, 0);
    CHECK_EQ(env.mode, CP0_APP_MODE_NONE);
    CHECK_EQ(env.screen_class, CP0_UI_CLASS__COUNT);
    CHECK(env.state_dir[0] == '\0' && env.screen_fb[0] == '\0');

    /* a compat app on the deck (what native_ui::run_external exports, plus the launcher's panel variables) */
    setenv("APPLAUNCH_CONTRACT", "1", 1);
    setenv("APPLAUNCH_APP_MODE", "compat", 1);
    setenv("APPLAUNCH_WINDOW", "0,0,640,340", 1);
    setenv("APPLAUNCH_CANVAS", "320x170", 1);
    setenv("APPLAUNCH_CANVAS_SCALE", "2", 1);
    setenv("APPLAUNCH_STATE_DIR", "/run/user/1000/applaunch", 1);
    setenv("APPLAUNCH_SCREEN_W", "640", 1);
    setenv("APPLAUNCH_SCREEN_H", "480", 1);
    setenv("APPLAUNCH_SCREEN_ROTATE", "0", 1);
    setenv("APPLAUNCH_SCREEN_PPMM", "1130", 1);
    setenv("APPLAUNCH_SCREEN_CLASS", "standard", 1);
    setenv("APPLAUNCH_SCREEN_FB", "/dev/fb0", 1);
    setenv("APPLAUNCH_SCREEN_TOUCH", "/dev/input/event1", 1);
    CHECK(cp0_app_env_read(&env) == 0);
    CHECK_EQ(env.contract, 1);
    CHECK_EQ(env.mode, CP0_APP_MODE_COMPAT);
    CHECK_EQ(env.win_x, 0);
    CHECK_EQ(env.win_y, 0);
    CHECK_EQ(env.win_w, 640);
    CHECK_EQ(env.win_h, 340);
    CHECK_EQ(env.canvas_w, 320);
    CHECK_EQ(env.canvas_h, 170);
    CHECK_EQ(env.canvas_scale, 2);
    CHECK(strcmp(env.state_dir, "/run/user/1000/applaunch") == 0);
    CHECK_EQ(env.screen_w, 640);
    CHECK_EQ(env.screen_h, 480);
    CHECK_EQ(env.screen_rot, 0);
    CHECK_EQ(env.screen_ppmm_x100, 1130);
    CHECK_EQ(env.screen_class, CP0_UI_STANDARD);
    CHECK(strcmp(env.screen_fb, "/dev/fb0") == 0);
    CHECK(strcmp(env.screen_touch, "/dev/input/event1") == 0);

    /* a fullscreen app on the Pi 3A+ (no touch device: empty) */
    setenv("APPLAUNCH_APP_MODE", "fullscreen", 1);
    setenv("APPLAUNCH_WINDOW", "0,0,480,320", 1);
    setenv("APPLAUNCH_CANVAS", "480x320", 1);
    setenv("APPLAUNCH_CANVAS_SCALE", "1", 1);
    setenv("APPLAUNCH_SCREEN_ROTATE", "90", 1);
    setenv("APPLAUNCH_SCREEN_CLASS", "compact", 1);
    setenv("APPLAUNCH_SCREEN_FB", "/dev/fb1", 1);
    setenv("APPLAUNCH_SCREEN_TOUCH", "", 1);
    CHECK(cp0_app_env_read(&env) == 0);
    CHECK_EQ(env.mode, CP0_APP_MODE_FULLSCREEN);
    CHECK_EQ(env.canvas_w, 480);
    CHECK_EQ(env.canvas_scale, 1);
    CHECK_EQ(env.screen_rot, 90);
    CHECK_EQ(env.screen_class, CP0_UI_COMPACT);
    CHECK(env.screen_touch[0] == '\0');

    /* a later contract is still read (keys are only added) */
    setenv("APPLAUNCH_CONTRACT", "2", 1);
    CHECK(cp0_app_env_read(&env) == 0 && env.contract == 2);

    /* malformed per-launch values: not a contract launch, the panel is still read */
    const char *bad[][2] = {
        {"APPLAUNCH_CONTRACT", "x"},      {"APPLAUNCH_CONTRACT", "0"},       {"APPLAUNCH_APP_MODE", "terminal"},
        {"APPLAUNCH_WINDOW", "0,0,640"},  {"APPLAUNCH_WINDOW", "0,0,640,0"},  {"APPLAUNCH_WINDOW", "0,0,640,340,1"},
        {"APPLAUNCH_CANVAS", "320*170"},  {"APPLAUNCH_CANVAS", "320x"},      {"APPLAUNCH_CANVAS_SCALE", "0"},
        {"APPLAUNCH_CANVAS_SCALE", "2x"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        setenv("APPLAUNCH_CONTRACT", "1", 1);
        setenv("APPLAUNCH_APP_MODE", "compat", 1);
        setenv("APPLAUNCH_WINDOW", "0,0,640,340", 1);
        setenv("APPLAUNCH_CANVAS", "320x170", 1);
        setenv("APPLAUNCH_CANVAS_SCALE", "2", 1);
        CHECK(cp0_app_env_read(&env) == 0);
        setenv(bad[i][0], bad[i][1], 1);
        if (cp0_app_env_read(&env) != -1) fprintf(stderr, "accepted %s=%s\n", bad[i][0], bad[i][1]);
        CHECK(cp0_app_env_read(&env) == -1);
        CHECK_EQ(env.screen_w, 640);
    }
    clear_env();
}

static void test_names(void)
{
    CHECK(strcmp(cp0_app_mode_name(CP0_APP_MODE_COMPAT), "compat") == 0);
    CHECK(strcmp(cp0_app_mode_name(CP0_APP_MODE_FULLSCREEN), "fullscreen") == 0);
    CHECK(strcmp(cp0_app_mode_name(CP0_APP_MODE_NONE), "") == 0);
    CHECK_EQ(cp0_app_mode_parse("compat"), CP0_APP_MODE_COMPAT);
    CHECK_EQ(cp0_app_mode_parse("fullscreen"), CP0_APP_MODE_FULLSCREEN);
    CHECK_EQ(cp0_app_mode_parse("Compat"), CP0_APP_MODE_NONE);
    CHECK_EQ(cp0_app_mode_parse(NULL), CP0_APP_MODE_NONE);
    for (int c = 0; c < CP0_UI_CLASS__COUNT; c++)
        CHECK_EQ(cp0_app_class_parse(cp0_ui_class_name((cp0_ui_class_t)c)), c);
    CHECK_EQ(cp0_app_class_parse("huge"), CP0_UI_CLASS__COUNT);
    cp0_app_vkb_t v = CP0_APP_VKB_FORCE;
    CHECK(cp0_app_vkb_parse("auto", &v) == 0 && v == CP0_APP_VKB_AUTO);
    CHECK(cp0_app_vkb_parse("off", &v) == 0 && v == CP0_APP_VKB_OFF);
    CHECK(cp0_app_vkb_parse("force", &v) == 0 && v == CP0_APP_VKB_FORCE);
    CHECK(cp0_app_vkb_parse("on", &v) == -1 && v == CP0_APP_VKB_FORCE);
    CHECK(strcmp(cp0_app_vkb_name(CP0_APP_VKB_AUTO), "auto") == 0);
    CHECK(strcmp(cp0_app_vkb_name(CP0_APP_VKB_OFF), "off") == 0);
    CHECK(strcmp(cp0_app_vkb_name(CP0_APP_VKB_FORCE), "force") == 0);
    /* report 041: the PDA writing zone is the text input */
    CHECK(cp0_app_vkb_parse("zone", &v) == 0 && v == CP0_APP_VKB_ZONE);
    CHECK(strcmp(cp0_app_vkb_name(CP0_APP_VKB_ZONE), "zone") == 0);
}

/* ------------------------------------------------------------------ state files */

static void test_paths(void)
{
    clear_env();
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char saved[512] = "";
    const int had_runtime = runtime != NULL;
    if (runtime) snprintf(saved, sizeof(saved), "%s", runtime);

    char buf[256];
    setenv("APPLAUNCH_STATE_DIR", "/run/user/1000/applaunch", 1);
    setenv("XDG_RUNTIME_DIR", "/elsewhere", 1);
    CHECK(cp0_app_state_path(CP0_APP_SCREEN_FILE, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/run/user/1000/applaunch/screen.state") == 0);
    unsetenv("APPLAUNCH_STATE_DIR");
    CHECK(cp0_app_state_path(CP0_APP_INPUT_FILE, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/elsewhere/applaunch/input.state") == 0);
    unsetenv("XDG_RUNTIME_DIR");
    CHECK(cp0_app_state_path(CP0_APP_SCREEN_FILE, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/tmp/applaunch-screen.state") == 0);
    CHECK(cp0_app_state_path(CP0_APP_INPUT_FILE, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/tmp/applaunch-input.state") == 0);
    /* the same places the launcher writes (cp0_ui_state_path, cp0_input_state_path) */
    char launcher[256];
    CHECK(cp0_ui_state_path(NULL, launcher, sizeof(launcher)) == 0);
    CHECK(cp0_app_state_path(CP0_APP_SCREEN_FILE, buf, sizeof(buf)) == 0 && strcmp(buf, launcher) == 0);
    CHECK(cp0_input_state_path(NULL, launcher, sizeof(launcher)) == 0);
    CHECK(cp0_app_state_path(CP0_APP_INPUT_FILE, buf, sizeof(buf)) == 0 && strcmp(buf, launcher) == 0);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    CHECK(cp0_ui_state_path("/run/user/1000", launcher, sizeof(launcher)) == 0);
    CHECK(cp0_app_state_path(CP0_APP_SCREEN_FILE, buf, sizeof(buf)) == 0 && strcmp(buf, launcher) == 0);
    char small[8];
    CHECK(cp0_app_state_path(CP0_APP_SCREEN_FILE, small, sizeof(small)) == -1);
    CHECK(cp0_app_state_path("", buf, sizeof(buf)) == -1);

    if (had_runtime) setenv("XDG_RUNTIME_DIR", saved, 1);
    else unsetenv("XDG_RUNTIME_DIR");
}

/* every field the launcher writes reads back as the same number */
static void check_screen_matches(const cp0_app_screen_t *s, const cp0_ui_metrics_t *m)
{
    cp0_app_screen_t want;
    cp0_app_screen_from_metrics(m, &want);
    CHECK_EQ(s->w, want.w);
    CHECK_EQ(s->h, want.h);
    CHECK_EQ(s->rot, want.rot);
    CHECK_EQ(s->ppmm_x100, want.ppmm_x100);
    CHECK_EQ(s->density_src, want.density_src);
    CHECK_EQ(s->cls, want.cls);
    CHECK_EQ(s->compat_x, want.compat_x);
    CHECK_EQ(s->compat_y, want.compat_y);
    CHECK_EQ(s->compat_w, want.compat_w);
    CHECK_EQ(s->compat_h, want.compat_h);
    CHECK_EQ(s->compat_scale, want.compat_scale);
    CHECK_EQ(s->toolbar_h, want.toolbar_h);
    CHECK_EQ(s->space_s, want.space_s);
    CHECK_EQ(s->space_m, want.space_m);
    CHECK_EQ(s->space_l, want.space_l);
    CHECK_EQ(s->radius_s, want.radius_s);
    CHECK_EQ(s->radius_m, want.radius_m);
    CHECK_EQ(s->radius_l, want.radius_l);
    CHECK_EQ(s->header_h, want.header_h);
    CHECK_EQ(s->row_h, want.row_h);
    CHECK_EQ(s->target, want.target);
    CHECK_EQ(s->dialog_max_w, want.dialog_max_w);
    CHECK_EQ(s->text_title, want.text_title);
    CHECK_EQ(s->text_heading, want.text_heading);
    CHECK_EQ(s->text_body, want.text_body);
    CHECK_EQ(s->text_caption, want.text_caption);
    CHECK_EQ(s->kb_inset, 0);
}

static void test_screen_round_trip(void)
{
    static const struct {
        int w, h, rot, ppmm;
        const char *fb;
        int bpp;
        const char *touch;
    } sizes[] = {
        {640, 480, 0, 0, "/dev/fb0", 32, "/dev/input/event1"},   /* the deck (pinned) */
        {480, 320, 90, 630, "/dev/fb1", 16, "/dev/input/event0"}, /* the Pi 3A+ (pinned) */
        {800, 480, 90, 920, "/dev/fb0", 32, "/dev/input/event5"},
        {720, 720, 0, 1000, "/dev/fb0", 32, ""},
        {1280, 720, 270, 1160, "/dev/fb0", 32, NULL},
        {1024, 600, 0, 660, "/dev/fb2", 16, "/dev/input/event9"},
    };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        const cp0_ui_metrics_t m = compute(sizes[i].w, sizes[i].h, sizes[i].rot, sizes[i].ppmm);
        const cp0_ui_panel_t panel = {sizes[i].fb, sizes[i].bpp, sizes[i].touch};
        char text[1024];
        const int n = cp0_ui_state_format(&m, &panel, 17 + (unsigned)i, text, sizeof(text));
        CHECK(n > 0);
        cp0_app_screen_t s;
        CHECK(cp0_app_screen_parse(text, (size_t)n, &s) == 0);
        check_screen_matches(&s, &m);
        CHECK(strcmp(s.fb, sizes[i].fb) == 0);
        CHECK_EQ(s.bpp, sizes[i].bpp);
        CHECK(strcmp(s.touch, sizes[i].touch ? sizes[i].touch : "") == 0);
        CHECK_EQ(s.gen, 17 + i);
        CHECK_EQ(s.from_file, 1);

        /* the fallback without the file gives the launcher's numbers for the same panel */
        cp0_app_screen_t c;
        CHECK(cp0_app_screen_compute(sizes[i].w, sizes[i].h, sizes[i].rot, sizes[i].ppmm, &c) == 0);
        check_screen_matches(&c, &m);
        CHECK_EQ(c.from_file, 0);
        CHECK(c.fb[0] == '\0' && c.bpp == 0 && c.gen == 0);
    }

    /* the deck's tokens, spelled out (report 022 / test_ui_metrics) */
    cp0_app_screen_t s;
    CHECK(cp0_app_screen_compute(640, 480, 0, 0, &s) == 0);
    CHECK(s.cls == CP0_UI_STANDARD && s.row_h == 79 && s.target == 79 && s.header_h == 56 && s.dialog_max_w == 560);
    CHECK(s.text_title == 32 && s.text_heading == 28 && s.text_body == 24 && s.text_caption == 18);
    CHECK(s.compat_x == 0 && s.compat_y == 0 && s.compat_w == 640 && s.compat_h == 340 && s.compat_scale == 2);
    CHECK(s.toolbar_h == 140);
    CHECK(cp0_app_screen_compute(0, 480, 0, 0, &s) == -1);
}

static void test_screen_files(void)
{
    cp0_app_screen_t s;
    /* the P1b file (before the contract: no panel, no tokens) still reads; the tokens stay 0 */
    const char *p1b = "v=1\nw=640\nh=480\nrot=0\nppmm=1130\ndensity=default\nclass=standard\ncompat=0,0,640,340\n"
                      "scale=2\ntoolbar=140\nkb_inset=0\ngen=5\n";
    CHECK(cp0_app_screen_parse(p1b, strlen(p1b), &s) == 0);
    CHECK(s.w == 640 && s.h == 480 && s.cls == CP0_UI_STANDARD && s.compat_scale == 2 && s.toolbar_h == 140);
    CHECK(s.row_h == 0 && s.fb[0] == '\0' && s.gen == 5);

    /* unknown keys are ignored, CRLF is tolerated, a bad value keeps its field at 0, a missing class is classified */
    const char *later = "v=1\r\nw=800\r\nh=480\r\nfuture=7\r\nrow=abc\r\ntarget=83\r\ncompat=1,2,3\r\n"
                        "text=1,2,3,4\r\ngen=9\r\n";
    CHECK(cp0_app_screen_parse(later, strlen(later), &s) == 0);
    CHECK(s.w == 800 && s.row_h == 0 && s.target == 83 && s.compat_w == 0 && s.text_caption == 4 && s.gen == 9);
    CHECK_EQ(s.cls, CP0_UI_WIDE);

    /* another version, no version, no size: not a screen file (cleared) */
    const char *v2 = "v=2\nw=640\nh=480\n";
    CHECK(cp0_app_screen_parse(v2, strlen(v2), &s) == -1);
    CHECK(s.w == 0 && s.from_file == 0);
    const char *nov = "w=640\nh=480\n";
    CHECK(cp0_app_screen_parse(nov, strlen(nov), &s) == -1);
    const char *noh = "v=1\nw=640\n";
    CHECK(cp0_app_screen_parse(noh, strlen(noh), &s) == -1);
    CHECK(cp0_app_screen_parse(NULL, 3, &s) == -1);

    /* read from the file the launcher wrote, by path and through APPLAUNCH_STATE_DIR */
    char root[] = "/tmp/cp0-app-contract-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char path[512];
    snprintf(path, sizeof(path), "%s/screen.state", root);
    const cp0_ui_metrics_t m = compute(720, 720, 0, 1000);
    const cp0_ui_panel_t panel = {"/dev/fb0", 32, "/dev/input/event9"};
    char text[1024];
    const int n = cp0_ui_state_format(&m, &panel, 3, text, sizeof(text));
    CHECK(n > 0 && cp0_ui_state_write(path, text, (size_t)n) == 0);
    CHECK(cp0_app_screen_read(path, &s) == 0);
    check_screen_matches(&s, &m);
    setenv("APPLAUNCH_STATE_DIR", root, 1);
    CHECK(cp0_app_screen_read(NULL, &s) == 0 && s.cls == CP0_UI_SQUARE && s.gen == 3);
    CHECK(strcmp(s.touch, "/dev/input/event9") == 0);
    unsetenv("APPLAUNCH_STATE_DIR");
    CHECK(cp0_app_screen_read("/nonexistent/screen.state", &s) == -1);

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

static void test_input_files(void)
{
    cp0_input_state_t st = {.keyboards = 1, .last_seen = 1760000000, .gen = 12};
    st.bt_keyboards = 1;
    st.kb_gone = 300;
    st.bt_gone = 310;
    st.vkb = CP0_APP_VKB_OFF;
    char text[320];
    const int n = cp0_input_state_format(&st, text, sizeof(text));
    CHECK(n > 0);
    cp0_app_input_t in;
    CHECK(cp0_app_input_parse(text, (size_t)n, &in) == 0);
    CHECK(in.keyboards == 1 && in.bt_keyboards == 1 && in.last_seen == 1760000000 && in.kb_gone == 300 &&
          in.bt_gone == 310 && in.vkb == CP0_APP_VKB_OFF && in.gen == 12 && in.pointer_nav == 0);
    /* pointer navigation on, as the launcher writes it */
    st.pointer_nav = 1;
    const int n_on = cp0_input_state_format(&st, text, sizeof(text));
    CHECK(n_on > 0 && cp0_app_input_parse(text, (size_t)n_on, &in) == 0 && in.pointer_nav == 1 && in.gen == 12);
    st.pointer_nav = 0;
    /* hand (report 044): 0 as written by default, 1 when the launcher says Handwriting + keyboard */
    CHECK(in.hand == 0);
    st.hand = 1;
    const int n_hand = cp0_input_state_format(&st, text, sizeof(text));
    CHECK(n_hand > 0 && cp0_app_input_parse(text, (size_t)n_hand, &in) == 0 && in.hand == 1 && in.gen == 12 &&
          in.vkb == CP0_APP_VKB_OFF);
    st.hand = 0;
    const char *bad_hand = "v=1\nhand=yes\ngen=6\n";
    CHECK(cp0_app_input_parse(bad_hand, strlen(bad_hand), &in) == 0 && in.hand == 0 && in.gen == 6);
    const char *crlf_hand = "v=1\r\nhand=1\r\n";
    CHECK(cp0_app_input_parse(crlf_hand, strlen(crlf_hand), &in) == 0 && in.hand == 1);

    /* an F1 phase 0 file (before the contract): vkb reads auto, pointer navigation off */
    const char *phase0 = "v=1\nkeyboards=0\nlast_seen=1760000100\ngen=4\n";
    CHECK(cp0_app_input_parse(phase0, strlen(phase0), &in) == 0);
    CHECK(in.keyboards == 0 && in.vkb == CP0_APP_VKB_AUTO && in.kb_gone == 0 && in.gen == 4 && in.pointer_nav == 0 &&
          in.hand == 0);
    const char *stray = "v=1\npointer=maybe\r\ngen=5\n";
    CHECK(cp0_app_input_parse(stray, strlen(stray), &in) == 0 && in.pointer_nav == 0 && in.gen == 5);
    const char *v2 = "v=2\nkeyboards=1\n";
    CHECK(cp0_app_input_parse(v2, strlen(v2), &in) == -1);
    CHECK(in.keyboards == 0 && in.vkb == CP0_APP_VKB_AUTO);

    char root[] = "/tmp/cp0-app-input-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char path[512];
    CHECK(cp0_input_state_path(root, path, sizeof(path)) == 0);
    CHECK(cp0_input_state_write(path, &st) == 0);
    CHECK(cp0_app_input_read(path, &in) == 0 && in.bt_gone == 310);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/applaunch", root);
    setenv("APPLAUNCH_STATE_DIR", dir, 1);
    CHECK(cp0_app_input_read(NULL, &in) == 0 && in.gen == 12);
    unsetenv("APPLAUNCH_STATE_DIR");
    CHECK(cp0_app_input_read("/nonexistent/input.state", &in) == -1);
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

/* ------------------------------------------------------------------ the on-screen keyboard rule */

static void test_osk_rule(void)
{
    const int64_t now = 100000;
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_AUTO, 0, now, 0, 0) == 1);     /* no keyboard ever seen */
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_AUTO, 1, now, 0, 0) == 0);     /* a keyboard is here */
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_OFF, 0, now, 0, 0) == 0);      /* APPLAUNCH_OSK=off, a desktop session */
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_FORCE, 2, now, now, now) == 1); /* forced, even with keyboards */
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_ZONE, 0, now, 0, 0) == 0);      /* the writing zone is the text input */
    /* a Bluetooth keyboard asleep for 10 minutes */
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_AUTO, 0, now, now - 599, now - 599) == 0);
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_AUTO, 0, now, now - 600, now - 600) == 1);
    /* any keyboard: a 2 s grace */
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_AUTO, 0, now, now - 1, 0) == 0);
    CHECK(cp0_app_osk_rule(CP0_APP_VKB_AUTO, 0, now, now - 2, 0) == 1);

    cp0_app_input_t in;
    memset(&in, 0, sizeof(in));
    CHECK(cp0_app_osk_allowed(&in, now) == 1);
    in.bt_gone = now - 30;
    CHECK(cp0_app_osk_allowed(&in, now) == 0);
    CHECK(cp0_app_osk_allowed(NULL, now) == 0);
    CHECK(cp0_app_monotonic_s() > 0);

    /* the launcher's own keyboard decides alike: for every input, the policy equals the rule with the vkb the
     * launcher publishes (cp0_ui_osk_effective_vkb) */
    const cp0_ui_osk_env_t envs[] = {CP0_UI_OSK_ENV_UNSET, CP0_UI_OSK_ENV_AUTO, CP0_UI_OSK_ENV_FORCE,
                                     CP0_UI_OSK_ENV_OFF};
    const int64_t gones[] = {0, now - 1, now - 2, now - 599, now - 600, now - 5000};
    for (size_t e = 0; e < 4; e++)
        for (int setting = 0; setting < 4; setting++) /* Auto, the old Off (read as Auto), On, Handwriting + keyboard */
            for (int desktop = 0; desktop < 2; desktop++)
                for (unsigned kb = 0; kb < 2; kb++)
                    for (size_t a = 0; a < 6; a++)
                        for (size_t b = 0; b < 6; b++) {
                            cp0_ui_osk_policy_t p;
                            memset(&p, 0, sizeof(p));
                            p.env = envs[e];
                            p.setting_on = setting == CP0_UI_OSK_SETTING_ON;
                            p.desktop_session = desktop;
                            p.keyboards = kb;
                            p.now_s = now;
                            p.any_gone_s = gones[a];
                            p.bt_gone_s = gones[b];
                            cp0_app_input_t app;
                            memset(&app, 0, sizeof(app));
                            app.vkb = cp0_ui_osk_effective_vkb(envs[e], setting, desktop);
                            app.keyboards = kb;
                            app.kb_gone = gones[a];
                            app.bt_gone = gones[b];
                            CHECK_EQ(cp0_ui_osk_policy_allows(&p), cp0_app_osk_allowed(&app, now));
                        }
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_UNSET, 0, 0), CP0_APP_VKB_AUTO);
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_AUTO, 1, 0), CP0_APP_VKB_AUTO); /* the old Off: Auto */
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_UNSET, CP0_UI_OSK_SETTING_ON, 0), CP0_APP_VKB_FORCE);
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_UNSET, CP0_UI_OSK_SETTING_HAND, 0), CP0_APP_VKB_AUTO);
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_UNSET, CP0_UI_OSK_SETTING_ON, 1), CP0_APP_VKB_OFF);
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_OFF, CP0_UI_OSK_SETTING_ON, 0), CP0_APP_VKB_OFF);
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_UNSET, 0, 1), CP0_APP_VKB_OFF);
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_FORCE, 1, 1), CP0_APP_VKB_FORCE);
    CHECK_EQ(cp0_ui_osk_effective_vkb(CP0_UI_OSK_ENV_OFF, 0, 0), CP0_APP_VKB_OFF);
}

int main(void)
{
    test_env();
    test_names();
    test_paths();
    test_screen_round_trip();
    test_screen_files();
    test_input_files();
    test_osk_rule();
    if (failures) {
        fprintf(stderr, "test_app_contract: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_app_contract: PASS\n");
    return 0;
}
