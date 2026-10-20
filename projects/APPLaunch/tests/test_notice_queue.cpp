// SPDX-License-Identifier: MIT
//
// Notices on the launcher's toast (task 015, deck findings 2026-10-20): with APPLAUNCH_MT_CAP=2 the deck logged
// "Closed MT Freeze (2 apps open)" then at once "MT Slow is not responding", and only the second was seen. Each
// notice must stay readable (2 s) before the next one replaces it, and one covered by an app comes back. Table-driven:
// each row is one call and what the toast must do.
#include "multitask/notice_queue.hpp"

#include <cstdio>
#include <string>
#include <vector>

using multitask::NoticeQueue;

namespace {

int g_failures = 0;
int g_checks = 0;

struct Row {
    const char *push;   /* nullptr: no push, a tick */
    std::uint64_t now;
    bool can_show;
    const char *want;   /* "-" nothing, "hide", or "show:<text>" */
};

std::string render(const NoticeQueue::Step &s)
{
    switch (s.kind) {
    case NoticeQueue::Step::Kind::None: return "-";
    case NoticeQueue::Step::Kind::Hide: return "hide";
    case NoticeQueue::Step::Kind::Show: return "show:" + s.text;
    }
    return "?";
}

void run(const char *name, const std::vector<Row> &rows, NoticeQueue q = NoticeQueue())
{
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row &r = rows[i];
        if (r.push) {
            q.push(r.push);
            continue;
        }
        const std::string got = render(q.tick(r.now, r.can_show));
        ++g_checks;
        if (got != r.want) {
            ++g_failures;
            std::printf("FAIL %s row %zu (t=%llu): want %s got %s\n", name, i, static_cast<unsigned long long>(r.now),
                        r.want, got.c_str());
        }
    }
}

} // namespace

int main()
{
    run("two notices at once: the first stays 2 s, then the second", {
        {"Closed MT Freeze (2 apps open)", 0, true, nullptr},
        {"MT Slow is not responding", 0, true, nullptr},
        {nullptr, 0, true, "show:Closed MT Freeze (2 apps open)"},
        {nullptr, 100, true, "-"},
        {nullptr, 1999, true, "-"},
        {nullptr, 2000, true, "show:MT Slow is not responding"},
        {nullptr, 3000, true, "-"},
        {nullptr, 4999, true, "-"},
        {nullptr, 5000, true, "hide"},
        {nullptr, 6000, true, "-"},
    });
    run("a single notice goes away after 3 s", {
        {"Cannot start X", 0, true, nullptr},
        {nullptr, 10, true, "show:Cannot start X"},
        {nullptr, 2500, true, "-"},
        {nullptr, 3010, true, "hide"},
    });
    run("queued while an app is in front: shown when the launcher is back", {
        {"X exited", 0, true, nullptr},
        {nullptr, 0, false, "-"},
        {nullptr, 5000, false, "-"},
        {nullptr, 6000, true, "show:X exited"},
    });
    run("covered before it could be read: shown again, for its full time", {
        {"Closed MT Freeze (2 apps open)", 0, true, nullptr},
        {nullptr, 0, true, "show:Closed MT Freeze (2 apps open)"},
        {nullptr, 50, false, "hide"},     /* the new app took the panel at once */
        {nullptr, 4000, false, "-"},
        {"MT Slow is not responding", 9000, true, nullptr},
        {nullptr, 9000, true, "show:Closed MT Freeze (2 apps open)"},
        {nullptr, 10999, true, "-"},
        {nullptr, 11000, true, "show:MT Slow is not responding"},
    });
    run("covered after it was read: not repeated", {
        {"A", 0, true, nullptr},
        {nullptr, 0, true, "show:A"},
        {nullptr, 2500, false, "hide"},
        {nullptr, 3000, true, "-"},
    });
    run("the same text twice in a row is shown once", {
        {"X is not responding", 0, true, nullptr},
        {"X is not responding", 0, true, nullptr},
        {nullptr, 0, true, "show:X is not responding"},
        {"X is not responding", 100, true, nullptr},
        {nullptr, 3000, true, "hide"},
    });
    {
        NoticeQueue::Config c;
        c.max_waiting = 2;
        run("too many: the oldest waiting is dropped", {
            {"1", 0, true, nullptr},
            {"2", 0, true, nullptr},
            {"3", 0, true, nullptr},
            {nullptr, 0, true, "show:2"},
            {nullptr, 2000, true, "show:3"},
            {nullptr, 5000, true, "hide"},
        }, NoticeQueue(c));
    }
    NoticeQueue empty;
    ++g_checks;
    if (empty.active() || render(empty.tick(0, true)) != "-") {
        ++g_failures;
        std::printf("FAIL empty queue\n");
    }
    if (g_failures) {
        std::printf("test_notice_queue: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("test_notice_queue: %d checks passed\n", g_checks);
    return 0;
}
