/*
 * SPDX-License-Identifier: MIT
 *
 * cyberdeckzero-ctl: talk to the running launcher through launcher.sock (report 035, docs/APP-CONTRACT.md section 14).
 * For terminal apps, scripts, ssh and the board test protocols. Plain C, no LVGL; built from the app client
 * (cp0_app_client.c).
 *
 *   cyberdeckzero-ctl hello | caps
 *   cyberdeckzero-ctl beep [notify|done|warn]
 *   cyberdeckzero-ctl toast TEXT...
 *   cyberdeckzero-ctl volume get | set N | up | down | mute | unmute
 *   cyberdeckzero-ctl brightness get | set N
 *   cyberdeckzero-ctl dim allow | deny
 *   cyberdeckzero-ctl board            (prints board.state)
 *
 * Prints the launcher's reply. Exit status: 0 ok, 1 refused (the reply starts with err), 2 usage, 3 no launcher service
 * (not running, an older launcher, no XDG_RUNTIME_DIR).
 */
#define _POSIX_C_SOURCE 200809L
#include "cp0_app_client.h"
#include "cp0_app_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CTL_TIMEOUT_MS 3000 /* a volume change may run pactl */

static int usage(FILE *to)
{
    fputs("usage: cyberdeckzero-ctl hello | caps\n"
          "       cyberdeckzero-ctl beep [notify|done|warn]\n"
          "       cyberdeckzero-ctl toast TEXT...\n"
          "       cyberdeckzero-ctl volume get | set N | up | down | mute | unmute\n"
          "       cyberdeckzero-ctl brightness get | set N\n"
          "       cyberdeckzero-ctl dim allow | deny\n"
          "       cyberdeckzero-ctl board\n"
          "exit status: 0 ok, 1 refused (err ...), 2 usage, 3 no launcher service\n",
          to);
    return to == stdout ? 0 : 2;
}

static int show_board(void)
{
    char path[CP0_APP_PATH_MAX + 32];
    if (cp0_app_state_path(CP0_APP_BOARD_FILE, path, sizeof(path)) != 0) return 3;
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cyberdeckzero-ctl: no %s (launcher not running, or older than 0.8.6)\n", path);
        return 3;
    }
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) fwrite(buf, 1, n, stdout);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) return usage(stderr);
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) return usage(stdout);
    if (!strcmp(argv[1], "board")) return argc == 2 ? show_board() : usage(stderr);

    char line[CP0_APP_CLIENT_LINE_MAX];
    size_t at = 0;
    for (int i = 1; i < argc; i++) {
        const size_t n = strlen(argv[i]);
        if (at + n + 2 >= sizeof(line) - 1) {
            fprintf(stderr, "cyberdeckzero-ctl: request too long\n");
            return 2;
        }
        if (i > 1) line[at++] = ' ';
        memcpy(line + at, argv[i], n);
        at += n;
    }
    line[at] = '\0';
    for (char *p = line; *p; p++)
        if (*p == '\n' || *p == '\r') *p = ' ';

    char reply[CP0_APP_CLIENT_LINE_MAX];
    const int r = cp0_app_client_call(line, reply, sizeof(reply), CTL_TIMEOUT_MS);
    if (r < 0) {
        fprintf(stderr, "cyberdeckzero-ctl: the launcher does not answer (not running, older than 0.8.6, or no "
                        "XDG_RUNTIME_DIR)\n");
        return 3;
    }
    puts(reply);
    return r == 0 ? 0 : 1;
}
