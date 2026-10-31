/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates P2: the root step (see settings_update_install.hpp).
 */

#include "settings_update_install.hpp"

#include "cp0_lvgl_app.h"
#include "sample_log.h"

#include <utility>

namespace launcher_update {

namespace {

constexpr int kAuthTimeoutMs = 60 * 1000; /* the Apps page's: the password prompt included */
constexpr int kExecTimeoutMs = 60 * 1000; /* systemd-run returns once the unit runs */

} // namespace

RootJob &RootJob::instance()
{
    static RootJob job;
    return job;
}

std::string root_failure_text(int sudo_result, int exit_code, const std::string &output)
{
    switch (sudo_result) {
    case CP0_SUDO_RESULT_SUCCESS: return std::string();
    case CP0_SUDO_RESULT_AUTH_FAILED: return "Wrong password";
    case CP0_SUDO_RESULT_CANCELLED: return "Cancelled";
    case CP0_SUDO_RESULT_TIMED_OUT: return "Timed out";
    default: break;
    }
    if (output.find("already exists") != std::string::npos || output.find("already loaded") != std::string::npos ||
        output.find("already active") != std::string::npos)
        return "An update is already running";
    if (exit_code == 127 || output.find("No such file") != std::string::npos) return "The update helper is missing";
    return "The update could not start (exit status " + std::to_string(exit_code) + ")";
}

bool RootJob::apply(const ApplyRequest &request)
{
    const std::vector<std::string> argv = apply_argv(request);
    if (argv.empty()) {
        snap_.error = "The update could not start (a value was refused)";
        return false;
    }
    return run(argv);
}

bool RootJob::resume(const std::string &user)
{
    const std::vector<std::string> argv = resume_argv(user);
    if (argv.empty()) {
        snap_.error = "The repair could not start";
        return false;
    }
    return run(argv);
}

bool RootJob::run(const std::vector<std::string> &args)
{
    if (snap_.asking) return false;
    std::vector<const char *> argv;
    for (const std::string &arg : args) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    snap_.asking = true;
    snap_.error.clear();
    output_.clear();
    uint64_t request_id = 0;
    const int rc = cp0_sudo_run_argv_async_ex(argv.data(), CP0_SUDO_CALLBACK_LVGL, &RootJob::output_cb,
                                              [](cp0_sudo_result_t result, int exit_code, void *user) {
                                                  RootJob::complete_cb(static_cast<int>(result), exit_code, user);
                                              },
                                              this, kAuthTimeoutMs, kExecTimeoutMs, &request_id);
    if (rc != 0) {
        snap_.asking = false;
        snap_.error = "Could not ask for the password";
        return false;
    }
    SLOGI("[UPDATE] root step asked: %s", args.size() > 6 ? args[6].c_str() : "?");
    return true;
}

void RootJob::output_cb(const char *data, std::size_t size, void *user)
{
    auto *self = static_cast<RootJob *>(user);
    if (self && data && self->output_.size() < 4096) self->output_.append(data, size);
}

void RootJob::complete_cb(int result, int exit_code, void *user)
{
    auto *self = static_cast<RootJob *>(user);
    if (!self) return;
    self->snap_.asking = false;
    if (result == CP0_SUDO_RESULT_SUCCESS && exit_code == 0) {
        ++self->snap_.started;
        self->snap_.error.clear();
        SLOGI("[UPDATE] the update helper runs (unit cyberdeckzero-update)");
        return;
    }
    self->snap_.error = root_failure_text(result, exit_code, self->output_);
    SLOGW("[UPDATE] root step failed: %s (%d, exit %d): %s", self->snap_.error.c_str(), result, exit_code,
          self->output_.c_str());
}

} // namespace launcher_update
