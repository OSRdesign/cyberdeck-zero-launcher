/*
 * SPDX-License-Identifier: MIT
 *
 * strokes-capture: records stylus strokes into a corpus file (README "Capturing a corpus"). Reads a Linux touch
 * screen live (/dev/input/eventN, read-only, optionally grabbed) or the text that `evtest` prints, follows one
 * contact, and writes one corpus line per stroke, labelled from the list of glyphs the writer was asked for.
 */
#include "evparse.h"
#include "tools_common.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#endif

#define MAX_LABELS 4096

typedef struct {
    FILE *out;
    char **labels;
    int nlabels, next;
    int box_mode; /* -1 auto, else enum strokes_box */
    int quiet, live;
    strokes_table *table;
    strokes_config cfg;
    long written;
} cap_ctx;

static volatile sig_atomic_t stop_flag;

static void on_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

static void usage(FILE *f)
{
    fprintf(f,
            "usage: strokes-capture (--evdev DEV | --evtest FILE | --list) [options]\n"
            "  --list               list the input devices (touch screens marked)\n"
            "  --evdev DEV          read a touch screen live, e.g. /dev/input/event2 (Linux; launcher stopped or --grab)\n"
            "  --evtest FILE        read the text output of evtest (a file, or - for stdin)\n"
            "  --out FILE           corpus file to write (default: stdout); --append adds to it\n"
            "  --labels \"a b c\"     the glyphs that will be written, in order (table ids: a..z 0..9 space bksp enter\n"
            "                       shift tap period); without labels every sample is \"?\"\n"
            "  --labels-file FILE   the same, whitespace-separated in a file\n"
            "  --reps N             each label N times (default 1)\n"
            "  --order blocks|rounds  a a a b b b (default) or a b c a b c\n"
            "  --box auto|letters|digits  box column (auto: digits for 0-9, letters otherwise)\n"
            "  --size WxH           map the raw axes onto this panel size in px (native orientation; default raw)\n"
            "  --rotate 0|90|180|270  turn the coordinates clockwise into the writing orientation\n"
            "  --ppmm P             density of the written coordinates (default 8.54, the 4.3\" DSI)\n"
            "  --panel-mm WxH       or: the panel size in mm in the writing orientation (ppmm from --size)\n"
            "  --group-ms MS        contacts closer than this are one sample (default 150)\n"
            "  --grab               grab the device while capturing (no other reader sees the touches)\n"
            "  --table FILE         recognise each sample with this table (default: the shipped glyphs.txt if found)\n"
            "  --no-recognise       do not recognise\n"
            "  --quiet              no prompts\n");
}

static void prompt(cap_ctx *c)
{
    if (c->quiet || !c->live)
        return;
    if (c->nlabels > 0 && c->next < c->nlabels)
        fprintf(stderr, "[%d/%d] write: %s\n", c->next + 1, c->nlabels, c->labels[c->next]);
    else if (c->nlabels == 0)
        fprintf(stderr, "[%ld] write a stroke (Ctrl+C ends)\n", c->written + 1);
}

static void on_sample(void *ctx, stk_sample *s)
{
    cap_ctx *c = ctx;
    const char *label = "?";
    if (c->nlabels > 0) {
        if (c->next < c->nlabels)
            label = c->labels[c->next];
        c->next++;
    }
    snprintf(s->label, sizeof s->label, "%s", label);
    if (c->box_mode >= 0)
        s->box = c->box_mode;
    else
        s->box = (strlen(label) == 1 && label[0] >= '0' && label[0] <= '9') ? STROKES_BOX_DIGITS : STROKES_BOX_LETTERS;
    corpus_write(c->out, s);
    fflush(c->out);
    c->written++;
    if (c->quiet)
        return;
    float minx = s->p[0].x, maxx = minx, miny = s->p[0].y, maxy = miny;
    for (int i = 1; i < s->n; ++i) {
        minx = s->p[i].x < minx ? s->p[i].x : minx;
        maxx = s->p[i].x > maxx ? s->p[i].x : maxx;
        miny = s->p[i].y < miny ? s->p[i].y : miny;
        maxy = s->p[i].y > maxy ? s->p[i].y : maxy;
    }
    double dur = s->nl ? (double)(s->lift_t[s->nl - 1] - s->p[0].t_us) / 1000.0 : 0.0;
    fprintf(stderr, "  %s: %d points, %d contact(s), %.0f ms, %.1f x %.1f mm", label, s->n, s->nl, dur,
            (double)((maxx - minx) / s->ppmm), (double)((maxy - miny) / s->ppmm));
    if (c->table) {
        c->cfg.ppmm = s->ppmm;
        strokes_engine *e = strokes_engine_new(c->table, &c->cfg);
        if (e) {
            int set = tools_label_set(c->table, label, s->box);
            strokes_set_mode(e, replay_mode_for_set(set));
            strokes_result res[4];
            int n = replay_sample(e, s, res, 4);
            for (int i = 0; i < n; ++i)
                fprintf(stderr, "%s %s%s %.2f (dirs %s)", i ? "," : " ->", res[i].kind == STROKES_REJECT ? "rejected " : "",
                        res[i].glyph[0] ? res[i].glyph : strokes_reject_name(res[i].reject), (double)res[i].confidence,
                        res[i].dirs);
            strokes_engine_free(e);
        }
    }
    fprintf(stderr, "\n");
    if (c->written == 1)
        fprintf(stderr, "  (orientation check: an L written as down-then-right reads dirs 26; if not, change --rotate)\n");
    prompt(c);
}

static int parse_wh(const char *v, float *w, float *h)
{
    char x;
    return sscanf(v, "%f%c%f", w, &x, h) == 3 && (x == 'x' || x == 'X') && *w > 0.0f && *h > 0.0f ? 0 : -1;
}

static int add_labels(cap_ctx *c, char **list, int *n, const char *text)
{
    const char *s = text;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == ',')
            s++;
        if (!*s)
            break;
        const char *e = s;
        while (*e && *e != ' ' && *e != '\t' && *e != '\n' && *e != '\r' && *e != ',')
            e++;
        if (*n >= MAX_LABELS || (size_t)(e - s) >= STROKES_ID_MAX)
            return -1;
        list[*n] = malloc((size_t)(e - s) + 1);
        if (!list[*n])
            return -1;
        memcpy(list[*n], s, (size_t)(e - s));
        list[*n][e - s] = 0;
        (*n)++;
        s = e;
    }
    (void)c;
    return 0;
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    int ch;
    while (buf && (ch = fgetc(f)) != EOF) {
        if (n + 1 >= cap) {
            char *nb = cap < (1u << 20) ? realloc(buf, cap * 2) : NULL;
            if (!nb) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = nb;
            cap *= 2;
        }
        buf[n++] = (char)ch;
    }
    if (buf)
        buf[n] = 0;
    fclose(f);
    return buf;
}

static int run_evtest(evp *p, const char *path)
{
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }
    char line[512];
    while (!stop_flag && fgets(line, sizeof line, f)) {
        unsigned long long sec, usec;
        if (sscanf(line, "Event: time %llu.%llu,", &sec, &usec) == 2)
            evp_tick(p, (uint64_t)sec * 1000000ull + (uint64_t)usec);
        evp_evtest_line(p, line);
    }
    evp_finish(p);
    if (f != stdin)
        fclose(f);
    return 0;
}

#ifdef __linux__
static int has_bit(const unsigned long *bits, int bit)
{
    const int w = (int)(8 * sizeof(unsigned long));
    return (int)((bits[bit / w] >> (bit % w)) & 1ul);
}

static int list_devices(void)
{
    int found = 0;
    for (int i = 0; i < 64; ++i) {
        char path[64], name[128] = "";
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        found++;
        unsigned long absbits[(ABS_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];
        unsigned long keybits[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))];
        memset(absbits, 0, sizeof absbits);
        memset(keybits, 0, sizeof keybits);
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbits), absbits);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits);
        int mt = has_bit(absbits, ABS_MT_POSITION_X), st = has_bit(absbits, ABS_X), touch = has_bit(keybits, BTN_TOUCH);
        printf("%-20s %-36s %s%s%s\n", path, name, (mt || (st && touch)) ? "touch" : "-", mt ? " multitouch" : "",
               touch ? " BTN_TOUCH" : "");
        close(fd);
    }
    if (!found)
        fprintf(stderr, "no readable /dev/input/event* (are you in the input group? is this Linux with evdev?)\n");
    return found ? 0 : 1;
}

static int run_evdev(evp *p, const char *dev, int grab, cap_ctx *c)
{
    int fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", dev, strerror(errno));
        return -1;
    }
    ioctl(fd, EVIOCGNAME(sizeof p->device), p->device);
    static const int codes[] = {ABS_X, ABS_Y, ABS_MT_POSITION_X, ABS_MT_POSITION_Y};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; ++i) {
        struct input_absinfo ai;
        if (ioctl(fd, EVIOCGABS(codes[i]), &ai) == 0)
            evp_set_axis(p, codes[i], ai.minimum, ai.maximum);
    }
    int clk = CLOCK_MONOTONIC;
    clockid_t tick_clock = ioctl(fd, EVIOCSCLOCKID, &clk) == 0 ? CLOCK_MONOTONIC : CLOCK_REALTIME;
    if (grab && ioctl(fd, EVIOCGRAB, 1) != 0)
        fprintf(stderr, "warning: cannot grab %s: %s\n", dev, strerror(errno));
    fprintf(stderr, "reading %s \"%s\" (Ctrl+C ends)\n", dev, p->device);
    prompt(c);
    struct input_event ev[64];
    while (!stop_flag && (c->nlabels == 0 || c->next < c->nlabels)) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int r = poll(&pfd, 1, 50);
        if (r < 0 && errno != EINTR)
            break;
        if (r > 0) {
            ssize_t n = read(fd, ev, sizeof ev);
            if (n < 0 && errno != EAGAIN && errno != EINTR) {
                fprintf(stderr, "%s: %s\n", dev, strerror(errno));
                break;
            }
            for (ssize_t i = 0; n > 0 && i < n / (ssize_t)sizeof ev[0]; ++i) {
                uint64_t t = (uint64_t)ev[i].input_event_sec * 1000000ull + (uint64_t)ev[i].input_event_usec;
                evp_event(p, t, ev[i].type, ev[i].code, ev[i].value);
            }
        }
        struct timespec ts;
        clock_gettime(tick_clock, &ts);
        evp_tick(p, (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull);
    }
    evp_finish(p);
    if (grab)
        ioctl(fd, EVIOCGRAB, 0);
    close(fd);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *evdev_path = NULL, *evtest_path = NULL, *out_path = NULL, *labels_text = NULL, *labels_file = NULL;
    const char *table_path = NULL, *order = "blocks", *box = "auto";
    int reps = 1, grab = 0, append = 0, quiet = 0, list = 0, recognise = 1, rotate = 0;
    float size_w = 0.0f, size_h = 0.0f, ppmm = 0.0f, mm_w = 0.0f, mm_h = 0.0f, group_ms = 150.0f;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        int takes = 1;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "--list") == 0) {
            list = 1;
            takes = 0;
        } else if (strcmp(a, "--grab") == 0) {
            grab = 1;
            takes = 0;
        } else if (strcmp(a, "--append") == 0) {
            append = 1;
            takes = 0;
        } else if (strcmp(a, "--quiet") == 0) {
            quiet = 1;
            takes = 0;
        } else if (strcmp(a, "--no-recognise") == 0) {
            recognise = 0;
            takes = 0;
        } else if (!v) {
            fprintf(stderr, "%s needs a value (or is unknown; see -h)\n", a);
            return 2;
        } else if (strcmp(a, "--evdev") == 0)
            evdev_path = v;
        else if (strcmp(a, "--evtest") == 0)
            evtest_path = v;
        else if (strcmp(a, "--out") == 0)
            out_path = v;
        else if (strcmp(a, "--labels") == 0)
            labels_text = v;
        else if (strcmp(a, "--labels-file") == 0)
            labels_file = v;
        else if (strcmp(a, "--reps") == 0)
            reps = atoi(v);
        else if (strcmp(a, "--order") == 0)
            order = v;
        else if (strcmp(a, "--box") == 0)
            box = v;
        else if (strcmp(a, "--size") == 0) {
            if (parse_wh(v, &size_w, &size_h)) {
                fprintf(stderr, "--size wants WxH\n");
                return 2;
            }
        } else if (strcmp(a, "--rotate") == 0)
            rotate = atoi(v);
        else if (strcmp(a, "--ppmm") == 0)
            ppmm = (float)atof(v);
        else if (strcmp(a, "--panel-mm") == 0) {
            if (parse_wh(v, &mm_w, &mm_h)) {
                fprintf(stderr, "--panel-mm wants WxH\n");
                return 2;
            }
        } else if (strcmp(a, "--group-ms") == 0)
            group_ms = (float)atof(v);
        else if (strcmp(a, "--table") == 0)
            table_path = v;
        else {
            fprintf(stderr, "unknown option %s\n", a);
            usage(stderr);
            return 2;
        }
        i += takes;
    }
#ifdef __linux__
    if (list)
        return list_devices();
#else
    if (list || evdev_path) {
        fprintf(stderr, "--list and --evdev need Linux evdev\n");
        return 2;
    }
#endif
    if (!!evdev_path + !!evtest_path != 1) {
        fprintf(stderr, "give exactly one of --evdev, --evtest (see -h)\n");
        return 2;
    }
    if (rotate != 0 && rotate != 90 && rotate != 180 && rotate != 270) {
        fprintf(stderr, "--rotate must be 0, 90, 180 or 270\n");
        return 2;
    }
    if (reps < 1)
        reps = 1;

    cap_ctx c;
    memset(&c, 0, sizeof c);
    c.quiet = quiet;
    c.live = evdev_path != NULL;
    c.box_mode = strcmp(box, "letters") == 0 ? STROKES_BOX_LETTERS : strcmp(box, "digits") == 0 ? STROKES_BOX_DIGITS
                                                                                                : -1;
    char *base[MAX_LABELS];
    int nbase = 0;
    if (labels_text && add_labels(&c, base, &nbase, labels_text)) {
        fprintf(stderr, "bad --labels\n");
        return 2;
    }
    if (labels_file) {
        char *txt = read_text(labels_file);
        if (!txt || add_labels(&c, base, &nbase, txt)) {
            fprintf(stderr, "cannot read --labels-file %s\n", labels_file);
            free(txt);
            return 2;
        }
        free(txt);
    }
    if (nbase * reps > MAX_LABELS) {
        fprintf(stderr, "more than %d labels\n", MAX_LABELS);
        return 2;
    }
    c.labels = malloc(sizeof(char *) * (size_t)(nbase * reps + 1));
    for (int k = 0; c.labels && k < nbase * reps; ++k) {
        int idx = strcmp(order, "rounds") == 0 ? k % nbase : k / reps;
        c.labels[c.nlabels++] = base[idx];
    }

    if (recognise) {
        char tbuf[512];
        const char *tp = table_path ? table_path : tools_default_table(argv[0], tbuf, sizeof tbuf);
        FILE *probe = fopen(tp, "rb");
        if (probe) {
            fclose(probe);
            c.table = tools_load_table(tp, NULL, 0);
        } else if (table_path) {
            fprintf(stderr, "%s: cannot open\n", table_path);
            return 2;
        }
    }
    strokes_config_default(&c.cfg);

    evp p;
    if (evp_init(&p)) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    p.out_w = size_w;
    p.out_h = size_h;
    p.rotate = rotate;
    p.group_ms = group_ms;
    if (ppmm <= 0.0f && mm_w > 0.0f && size_w > 0.0f) {
        float lw = (rotate == 90 || rotate == 270) ? size_h : size_w;
        ppmm = lw / mm_w;
    }
    if (ppmm <= 0.0f) {
        ppmm = 8.54f;
        if (!quiet)
            fprintf(stderr, "note: --ppmm not given, assuming 8.54 px/mm (the 4.3\" DSI)\n");
    }
    p.ppmm = ppmm;
    p.on_sample = on_sample;
    p.ctx = &c;

    c.out = out_path ? fopen(out_path, append ? "a" : "w") : stdout;
    if (!c.out) {
        fprintf(stderr, "%s: %s\n", out_path, strerror(errno));
        return 2;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int rc;
    if (evtest_path) {
        /* the header (axis ranges, device name) comes first in the text: the corpus header is written after it */
        char note[512];
        snprintf(note, sizeof note, "source: evtest text %s; ppmm %.3f; size %.0fx%.0f; rotate %d", evtest_path,
                 (double)ppmm, (double)size_w, (double)size_h, rotate);
        corpus_write_header(c.out, "strokes-capture", note);
        rc = run_evtest(&p, evtest_path);
    } else {
#ifdef __linux__
        char note[512];
        time_t now = time(NULL);
        char date[32];
        strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", localtime(&now));
        snprintf(note, sizeof note, "source: %s; ppmm %.3f; size %.0fx%.0f; rotate %d; %s", evdev_path, (double)ppmm,
                 (double)size_w, (double)size_h, rotate, date);
        corpus_write_header(c.out, "strokes-capture", note);
        rc = run_evdev(&p, evdev_path, grab, &c);
#else
        rc = -1;
#endif
    }
    if (!quiet)
        fprintf(stderr, "%ld sample(s) written (%ld events, %ld frames%s)\n", c.written, p.events, p.frames,
                p.overflows ? ", some samples truncated at 1024 points" : "");
    if (c.nlabels > 0 && c.next != c.nlabels && !quiet)
        fprintf(stderr, "warning: %d labels expected, %d samples seen\n", c.nlabels, c.next);
    if (c.out != stdout)
        fclose(c.out);
    evp_free(&p);
    for (int i = 0; i < nbase; ++i)
        free(base[i]);
    free(c.labels);
    strokes_table_free(c.table);
    return rc ? 1 : 0;
}
