/*
 * SPDX-License-Identifier: MIT
 *
 * The SSH indicator's launcher side without a service (see ssh_indicator_model.hpp).
 */

#include "ssh_indicator_model.hpp"

namespace ssh_indicator {

Sampler::Sampler(const char *root)
{
    if (root) {
        root_ = root;
        has_root_ = true;
    }
    reset();
}

void Sampler::reset()
{
    cp0_ssh_watch_options_t options{};
    options.root = has_root_ ? root_.c_str() : nullptr;
    cp0_ssh_watch_init(&watch_, &options);
}

Snapshot Sampler::sample(bool enabled, int64_t now_ms)
{
    Snapshot out;
    if (!enabled) {
        if (sampling_) reset(); /* forget the connections: confirmed again when the setting comes back */
        sampling_ = false;
        return out;
    }
    sampling_ = true;
    cp0_ssh_status_t status{};
    cp0_ssh_watch_sample(&watch_, now_ms, &status);
    out.count = status.count > 0 ? status.count : 0;
    out.active = out.count > 0 && status.active ? 1 : 0;
    return out;
}

Snapshot shown(const Snapshot &feed, bool enabled, bool locked)
{
    Snapshot out;
    if (!enabled || feed.count <= 0) return out;
    out.count = feed.count;
    out.active = locked ? 0 : (feed.active ? 1 : 0);
    return out;
}

int parse_mode(const std::string &text)
{
    if (text == "0") return kOff;
    return kOn;
}

} // namespace ssh_indicator
