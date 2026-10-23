/*
 * SPDX-License-Identifier: MIT
 *
 * board.state publisher and launcher.sock service (see app_requests.hpp).
 */
#include "app_requests.hpp"

std::string app_requests::hello_reply(const std::string &launcher_version, const std::string &app,
                                      const std::string &verbs)
{
    std::string name;
    for (char c : app) name += (c == ' ' || static_cast<unsigned char>(c) < 0x20) ? '_' : c; /* one word */
    if (name.empty()) name = "unknown";
    return "ok 1 launcher=" + (launcher_version.empty() ? std::string("unknown") : launcher_version) + " app=" + name +
           " verbs=" + verbs;
}

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "app_media.hpp"
#include "app_request.hpp"
#include "app_request_server.hpp"
#include "board_facts.hpp"
#include "cp0_board_state.h"
#include "sample_log.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include <unistd.h>

#define APP_REQ_STRINGIFY2(x) #x
#define APP_REQ_STRINGIFY(x) APP_REQ_STRINGIFY2(x)

namespace {

const char *launcher_version()
{
#ifdef LAUNCHER_VERSION_RAW
    return APP_REQ_STRINGIFY(LAUNCHER_VERSION_RAW);
#else
    return "";
#endif
}

void log_line(const std::string &text)
{
    SLOGI("%s", text.c_str());
}

/* ---- the verbs */

std::string num(int v)
{
    return std::to_string(v);
}

class LauncherBackend : public app_request::Backend {
public:
    std::string verbs() const
    {
        std::string v = "hello,caps,toast,beep";
        if (app_media::sound_present()) v += ",volume";
        if (app_media::backlight_present()) v += ",brightness";
        if (app_media::dim_eligible()) v += ",dim";
        return v;
    }

    std::string hello(const app_request::Peer &peer) override
    {
        return app_requests::hello_reply(launcher_version(), peer.name, verbs());
    }

    std::string caps() override
    {
        return std::string("ok sound=") + (app_media::sound_present() ? "1" : "0") +
               " backlight=" + app_media::backlight_kind() + " dim=" + (app_media::dim_eligible() ? "1" : "0") +
               " verbs=" + verbs();
    }

    std::string beep(const std::string &name) override
    {
        const int r = app_media::beep(name);
        return r == 0 ? "ok" : r == 1 ? "err nosound" : "err bad";
    }

    std::string toast(const std::string &text) override
    {
        app_media::toast_post(text);
        return "ok queued";
    }

    std::string volume(const std::string &action, int value) override
    {
        if (!app_media::sound_present()) return "err nosound";
        int v;
        bool muted = false;
        if (action == "set") v = app_media::volume_set(value);
        else if (action == "up") v = app_media::volume_step(1);
        else if (action == "down") v = app_media::volume_step(-1);
        else if (action == "mute") v = app_media::volume_mute(true);
        else if (action == "unmute") v = app_media::volume_mute(false);
        else v = -2;
        int now = app_media::volume_get(muted);
        if (v == -1 || now < 0) return "err busy";
        return "ok " + num(now) + " muted=" + (muted ? "1" : "0");
    }

    std::string brightness(const std::string &action, int value) override
    {
        if (!app_media::backlight_present()) return "err absent";
        if (action == "set") {
            if (std::string(app_media::backlight_kind()) != "levels") return "err absent";
            if (app_media::brightness_set(value) < 0) return "err busy";
        }
        const int now = app_media::brightness_get();
        if (now < 0) return "err busy";
        return "ok " + num(now) + " kind=" + app_media::backlight_kind();
    }

    std::string dim(bool allow, const app_request::Peer &) override
    {
        return app_media::set_dim_allowed(allow) ? "ok" : "err absent";
    }
};

/* ---- the service */

struct Service {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread publisher;
    bool running = false;
    bool stopping = false;
    LauncherBackend backend;
    app_request::RateLimiter limiter;
};

Service &service()
{
    static Service instance;
    return instance;
}

long long monotonic_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void publish_loop()
{
    char path[512];
    if (cp0_board_state_path(std::getenv("XDG_RUNTIME_DIR"), path, sizeof(path)) != 0) {
        SLOGW("[BOARD] XDG_RUNTIME_DIR is too long: board.state not published");
        return;
    }
    cp0_app_board_t last;
    (void)cp0_app_board_load(path, &last); /* a restarted launcher continues gen from the file of its run before */
    unsigned gen = last.gen;
    cp0_app_board_t written;
    std::memset(&written, 0, sizeof(written));
    bool have_written = false;
    Service &s = service();
    bool first = true;
    for (;;) {
        cp0_app_board_t now;
        std::memset(&now, 0, sizeof(now));
        board_facts::collect(now, launcher_version(), !first); /* the first write must not wait for pactl */
        if (!have_written || cp0_board_state_differs(&now, &written)) {
            now.pid = static_cast<int>(::getpid());
            now.gen = ++gen;
            if (cp0_board_state_write(path, &now) == 0) {
                written = now;
                have_written = true;
            } else {
                SLOGW("[BOARD] cannot write %s", path);
            }
        }
        const bool was_first = first;
        first = false;
        std::unique_lock<std::mutex> lock(s.mutex);
        if (s.wake.wait_for(lock, was_first ? std::chrono::seconds(0) : std::chrono::seconds(30),
                            [&] { return s.stopping; }))
            return;
    }
}

} // namespace

namespace app_requests {

void start()
{
    Service &s = service();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.running) return;
    s.running = true;
    s.stopping = false;
    app_media::start();
    app_media::toast_install_pump();
    s.publisher = std::thread(publish_loop);

    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime || !runtime[0]) {
        SLOGW("[REQ] XDG_RUNTIME_DIR is not set: launcher.sock is not served (no /tmp fallback)");
        return;
    }
    app_request::ServerConfig config;
    config.path = std::string(runtime) + "/applaunch/launcher.sock";
    config.log = log_line;
    config.handler = [](const std::string &line, const app_request::Peer &peer) {
        Service &sv = service();
        return app_request::handle_line(line, peer, sv.backend, sv.limiter, monotonic_ms(), log_line);
    };
    (void)app_request::start_server(std::move(config));
}

void stop()
{
    Service &s = service();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.running) return;
        s.stopping = true;
    }
    s.wake.notify_all();
    app_request::stop_server();
    if (s.publisher.joinable()) s.publisher.join();
    app_media::stop();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.running = false;
}

} // namespace app_requests

#else

namespace app_requests {

void start() {}
void stop() {}

} // namespace app_requests

#endif
