/* SPDX-License-Identifier: MIT
 *
 * Render harness: tracks the worker threads of the launcher code (see harness.h, "worker threads"), so a scene step can
 * wait until every one of them has reported back. The Makefile links libstdc++ statically with
 * -Wl,--wrap=pthread_create: std::thread, std::async and the C code's pthread_create all pass through the wrapper below.
 */

#include "harness.h"

#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <list>

extern "C" int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);

namespace harness {

namespace {

using Clock = std::chrono::steady_clock; /* real time: the fake wall clock never enters here */

constexpr int kGateTimeoutMs = 500;

struct Entry {
    Clock::time_point start;
    bool ignored = false;
    std::atomic<bool> go{false}; /* the LVGL thread has finished the step that started this thread */
    std::function<bool()> held;  /* set while the thread waits for the scene: true while the scene still holds it */
};

std::mutex &registry_mutex()
{
    static std::mutex *m = new std::mutex(); /* never destroyed: detached workers may end after main */
    return *m;
}

std::list<Entry *> &registry()
{
    static std::list<Entry *> *l = new std::list<Entry *>();
    return *l;
}

std::atomic<int> g_ignore{0};
thread_local Entry *t_entry = nullptr;

struct Start {
    void *(*fn)(void *);
    void *arg;
    Entry *entry;
};

void *trampoline(void *raw)
{
    Start start = *static_cast<Start *>(raw);
    delete static_cast<Start *>(raw);
    t_entry = start.entry;
    /* The thread starts working only after the step that created it (a page's constructor, a key's handler) has ended:
     * wait_workers() opens the gate. A worker that finished early would make the page's first rows depend on the race
     * between the constructor and the worker. The timeout is for a step that waits for the worker itself (a join, the
     * destructor of a std::async future: the Bluetooth session calls): such a thread starts after 500 ms of real time. */
    if (!start.entry->ignored) {
        const Clock::time_point since = Clock::now();
        while (!start.entry->go.load() && Clock::now() - since < std::chrono::milliseconds(kGateTimeoutMs)) ::usleep(50);
        if (!start.entry->go.load() && std::getenv("HARNESS_THREAD_TRACE"))
            std::fprintf(stderr, "[harness] worker gate timed out\n");
    }
    void *result = start.fn(start.arg);
    t_entry = nullptr;
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        registry().remove(start.entry);
    }
    delete start.entry;
    return result;
}

} // namespace

BlockedWhile::BlockedWhile(std::function<bool()> still_held)
{
    if (!t_entry) return;
    std::lock_guard<std::mutex> lock(registry_mutex());
    t_entry->held = std::move(still_held);
}

BlockedWhile::~BlockedWhile()
{
    if (!t_entry) return;
    std::lock_guard<std::mutex> lock(registry_mutex());
    t_entry->held = nullptr;
}

IgnoreNewThreads::IgnoreNewThreads()
{
    ++g_ignore;
}

IgnoreNewThreads::~IgnoreNewThreads()
{
    --g_ignore;
}

void wait_workers()
{
    for (;;) {
        bool at_work = false;
        {
            std::lock_guard<std::mutex> lock(registry_mutex());
            const Clock::time_point now = Clock::now();
            for (Entry *e : registry()) {
                if (e->ignored) continue;
                e->go.store(true);
                if (now - e->start > std::chrono::milliseconds(kWorkerGraceMs)) continue;
                if (e->held && e->held()) continue; /* the scene holds it */
                at_work = true;
            }
        }
        if (!at_work) return;
        ::usleep(200);
    }
}

} // namespace harness

extern "C" int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg)
{
    harness::Entry *entry = new harness::Entry();
    entry->start = harness::Clock::now();
    entry->ignored = harness::g_ignore.load() > 0;
    {
        std::lock_guard<std::mutex> lock(harness::registry_mutex());
        harness::registry().push_back(entry);
    }
    harness::Start *start = new harness::Start{fn, arg, entry};
    const int rc = __real_pthread_create(thread, attr, harness::trampoline, start);
    if (rc != 0) {
        {
            std::lock_guard<std::mutex> lock(harness::registry_mutex());
            harness::registry().remove(entry);
        }
        delete entry;
        delete start;
    }
    return rc;
}
