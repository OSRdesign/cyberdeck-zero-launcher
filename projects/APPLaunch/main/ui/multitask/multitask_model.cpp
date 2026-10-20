/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-b: registry and lifecycle state machine (see multitask_model.hpp).
 */
#include "multitask_model.hpp"

#include <algorithm>

namespace multitask {

namespace {

std::string_view trim(std::string_view v)
{
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.remove_suffix(1);
    return v;
}

bool equals_nocase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

Action act(Op op, AppId id, std::uint32_t n = 0, int value = 0)
{
    Action a;
    a.op = op;
    a.id = id;
    a.n = n;
    a.value = value;
    return a;
}

} // namespace

Background parse_background(std::string_view value)
{
    value = trim(value);
    if (equals_nocase(value, "freeze")) return Background::Freeze;
    if (equals_nocase(value, "run")) return Background::Run;
    return Background::None;
}

const char *background_name(Background background)
{
    switch (background) {
    case Background::Freeze: return "freeze";
    case Background::Run: return "run";
    case Background::None: break;
    }
    return "none";
}

const char *state_name(State state)
{
    switch (state) {
    case State::Foreground: return "foreground";
    case State::Hiding: return "hiding";
    case State::HiddenRunning: return "hidden-running";
    case State::HiddenFrozen: return "hidden-frozen";
    case State::NotResponding: return "not-responding";
    case State::Closing: return "closing";
    case State::Gone: return "gone";
    }
    return "gone";
}

bool is_resident(State state)
{
    return state == State::Foreground || state == State::Hiding || is_hidden(state);
}

bool is_hidden(State state)
{
    return state == State::HiddenRunning || state == State::HiddenFrozen || state == State::NotResponding;
}

bool is_frozen(State state)
{
    return state == State::HiddenFrozen || state == State::NotResponding;
}

const char *op_name(Op op)
{
    switch (op) {
    case Op::LauncherFront: return "LauncherFront";
    case Op::AppFront: return "AppFront";
    case Op::LauncherKeys: return "LauncherKeys";
    case Op::GrabKeys: return "GrabKeys";
    case Op::GrabTouch: return "GrabTouch";
    case Op::RawKeys: return "RawKeys";
    case Op::Repaint: return "Repaint";
    case Op::Spawn: return "Spawn";
    case Op::SendHide: return "SendHide";
    case Op::SendShow: return "SendShow";
    case Op::SendClose: return "SendClose";
    case Op::Stop: return "Stop";
    case Op::Cont: return "Cont";
    case Op::Term: return "Term";
    case Op::Kill: return "Kill";
    case Op::Snapshot: return "Snapshot";
    case Op::Restore: return "Restore";
    case Op::DropSnapshot: return "DropSnapshot";
    case Op::Drain: return "Drain";
    case Op::SetOom: return "SetOom";
    case Op::Notice: return "Notice";
    }
    return "?";
}

const char *close_reason_name(CloseReason reason)
{
    switch (reason) {
    case CloseReason::None: return "none";
    case CloseReason::User: return "user";
    case CloseReason::CloseAll: return "close-all";
    case CloseReason::Cap: return "cap";
    case CloseReason::Memory: return "memory";
    case CloseReason::EscHold: return "esc-hold";
    case CloseReason::Shutdown: return "shutdown";
    }
    return "none";
}

Model::Model(Config config) : config_(config)
{
    if (config_.cap < 1) config_.cap = 1;
}

/* ------------------------------------------------------------------------------------------------ helpers */

Entry *Model::get(AppId id)
{
    for (Entry &e : entries_)
        if (e.id == id && e.state != State::Gone) return &e;
    return nullptr;
}

const Entry *Model::find(AppId id) const
{
    for (const Entry &e : entries_)
        if (e.id == id && e.state != State::Gone) return &e;
    return nullptr;
}

AppId Model::find_key(const std::string &key) const
{
    for (const Entry &e : entries_)
        if (e.state != State::Gone && e.spec.key == key) return e.id;
    return kNoApp;
}

std::vector<AppId> Model::mru() const
{
    std::vector<const Entry *> list;
    for (const Entry &e : entries_)
        if (is_resident(e.state)) list.push_back(&e);
    std::stable_sort(list.begin(), list.end(), [](const Entry *a, const Entry *b) { return a->mru > b->mru; });
    std::vector<AppId> ids;
    for (const Entry *e : list) ids.push_back(e->id);
    return ids;
}

std::vector<AppId> Model::live() const
{
    std::vector<AppId> ids;
    for (const Entry &e : entries_)
        if (e.state != State::Gone) ids.push_back(e.id);
    return ids;
}

std::uint64_t Model::next_deadline() const
{
    std::uint64_t next = 0;
    const auto at = [&next](std::uint64_t t) {
        if (t != 0 && (next == 0 || t < next)) next = t;
    };
    for (const Entry &e : entries_) {
        if (e.state == State::Hiding) at(e.hide_deadline);
        if (e.state != State::Closing) continue;
        if (!e.term_sent) at(e.term_at);
        else if (!e.kill_sent) at(e.kill_at);
    }
    return next;
}

int Model::resident_count() const
{
    int n = 0;
    for (const Entry &e : entries_)
        if (is_resident(e.state)) ++n;
    return n;
}

int Model::hidden_count() const
{
    int n = 0;
    for (const Entry &e : entries_)
        if (is_hidden(e.state)) ++n;
    return n;
}

void Model::set_state(Entry &e, State s, std::uint64_t now)
{
    e.state = s;
    e.since = now;
}

void Model::set_oom(Entry &e, int value, Actions &out)
{
    if (e.oom_now == value) return;
    out.push_back(act(Op::SetOom, e.id, 0, value));
    e.oom_now = value;
}

void Model::notice(AppId id, std::string text, Actions &out)
{
    Action a = act(Op::Notice, id);
    a.text = std::move(text);
    out.push_back(std::move(a));
}

void Model::erase_gone()
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const Entry &e) { return e.state == State::Gone; }),
                   entries_.end());
}

Entry *Model::lru_hidden(bool frozen_only)
{
    Entry *best = nullptr;
    for (Entry &e : entries_) {
        if (!is_hidden(e.state)) continue;
        if (frozen_only && !is_frozen(e.state)) continue;
        if (!best || e.mru < best->mru) best = &e;
    }
    return best;
}

/* Who is in front and what follows from it. Going to an app: the front change, then the evdev reader on and the
 * grabs released before the launcher's keyboard thread pauses; going to the launcher: its keyboard thread, then the
 * grabs, then the evdev reader off. The supervisor executes them before any restore or thaw that follows. */
void Model::sync_derived(Actions &out)
{
    Derived d;
    d.front = front_;
    const bool launcher = front_ == kNoApp;
    d.launcher_keys = launcher && !raw_session_;
    d.grab_touch = launcher && hidden_count() > 0;
    d.grab_keys = d.grab_touch && !raw_session_;
    d.raw_keys = !launcher || raw_session_;
    if (d.raw_keys && !derived_.raw_keys) out.push_back(act(Op::RawKeys, kNoApp, 0, 1));
    if (!d.grab_keys && derived_.grab_keys) out.push_back(act(Op::GrabKeys, kNoApp, 0, 0));
    if (!d.grab_touch && derived_.grab_touch) out.push_back(act(Op::GrabTouch, kNoApp, 0, 0));
    if (d.launcher_keys != derived_.launcher_keys)
        out.push_back(act(Op::LauncherKeys, kNoApp, 0, d.launcher_keys ? 1 : 0));
    if (d.grab_touch && !derived_.grab_touch) out.push_back(act(Op::GrabTouch, kNoApp, 0, 1));
    if (d.grab_keys && !derived_.grab_keys) out.push_back(act(Op::GrabKeys, kNoApp, 0, 1));
    if (!d.raw_keys && derived_.raw_keys) out.push_back(act(Op::RawKeys, kNoApp, 0, 0));
    derived_ = d;
}

void Model::set_front(AppId id, bool after_exit, Actions &out)
{
    if (front_ != id) {
        front_ = id;
        out.push_back(id == kNoApp ? act(Op::LauncherFront, kNoApp, 0, after_exit ? 1 : 0) : act(Op::AppFront, id));
    }
    sync_derived(out);
}

/* ------------------------------------------------------------------------------------------------ transitions */

void Model::spawn_new(const AppSpec &spec, std::uint64_t now, Actions &out)
{
    Entry e;
    e.id = next_id_++;
    e.spec = spec;
    e.mru = ++mru_clock_;
    set_state(e, State::Foreground, now);
    entries_.push_back(std::move(e));
    const AppId id = entries_.back().id;
    set_front(id, false, out);
    out.push_back(act(Op::Spawn, id));
}

bool Model::launch_into(const AppSpec &spec, std::uint64_t now, Actions &out)
{
    if (resident_count() >= config_.cap) {
        const std::string open = std::to_string(config_.cap) + (config_.cap == 1 ? " app open" : " apps open");
        Entry *victim = lru_hidden(true);
        if (!victim) {
            notice(kNoApp, open + ": close one first", out);
            return false;
        }
        notice(victim->id, "Closed " + victim->spec.name + " (" + open + ")", out);
        close_into(*victim, CloseReason::Cap, now, out);
    }
    spawn_new(spec, now, out);
    return true;
}

void Model::start_hide(Entry &e, std::uint64_t now, Actions &out)
{
    set_state(e, State::Hiding, now);
    e.hide_n = ++e.seq;
    e.hide_deadline = now + config_.hide_ack_ms;
    if (e.can_talk()) out.push_back(act(Op::SendHide, e.id, e.hide_n));
    else hide_done(e.id, false, now, out); /* no socket: frozen at once, as an app that does not answer */
}

void Model::hide_done(AppId id, bool acked, std::uint64_t now, Actions &out)
{
    Entry *e = get(id);
    if (!e || e->state != State::Hiding) return;
    if (acked && e->spec.background == Background::Run) {
        set_state(*e, State::HiddenRunning, now);
        out.push_back(act(Op::Snapshot, id));
        e->has_snapshot = true;
        set_oom(*e, config_.oom_hidden_run, out);
    } else {
        out.push_back(act(Op::Stop, id));
        out.push_back(act(Op::Snapshot, id));
        e->has_snapshot = true;
        set_state(*e, acked ? State::HiddenFrozen : State::NotResponding, now);
        set_oom(*e, config_.oom_frozen, out);
        if (!acked) notice(id, e->spec.name + " is not responding", out);
    }
    if (!run_pending(now, out)) set_front(kNoApp, false, out);
}

bool Model::run_pending(std::uint64_t now, Actions &out)
{
    const Pending p = pending_;
    pending_ = Pending();
    switch (p.kind) {
    case Pending::Kind::App: {
        const Entry *t = find(p.id);
        if (t && is_hidden(t->state)) {
            show(p.id, now, out);
            return true;
        }
        return false;
    }
    case Pending::Kind::Launch: {
        const AppId existing = find_key(p.spec.key);
        if (existing != kNoApp) {
            const Entry *t = find(existing);
            if (t && is_hidden(t->state)) {
                show(existing, now, out);
                return true;
            }
            return false;
        }
        return launch_into(p.spec, now, out);
    }
    case Pending::Kind::Launcher:
    case Pending::Kind::None:
        break;
    }
    return false;
}

void Model::show(AppId id, std::uint64_t now, Actions &out)
{
    Entry *e = get(id);
    if (!e || !is_hidden(e->state)) return;
    const bool frozen = is_frozen(e->state);
    set_front(id, false, out);
    if (e->has_snapshot) out.push_back(act(Op::Restore, id));
    out.push_back(act(Op::Drain, id));
    set_oom(*e, e->oom_base, out);
    if (frozen) out.push_back(act(Op::Cont, id));
    if (e->can_talk()) out.push_back(act(Op::SendShow, id, ++e->seq));
    set_state(*e, State::Foreground, now);
    e->mru = ++mru_clock_;
}

void Model::close_into(Entry &e, CloseReason reason, std::uint64_t now, Actions &out)
{
    e.closed_from = e.state;
    e.close_reason = reason;
    if (is_frozen(e.state)) out.push_back(act(Op::Cont, e.id)); /* a stopped process acts on nothing until then */
    if (e.can_talk()) {
        out.push_back(act(Op::SendClose, e.id, ++e.seq));
        e.term_at = now + config_.close_term_ms;
    } else {
        out.push_back(act(Op::Term, e.id));
        e.term_sent = true;
        e.term_at = 0;
        e.kill_at = now + config_.term_kill_ms;
    }
    set_state(e, State::Closing, now);
}

/* ------------------------------------------------------------------------------------------------ requests */

Actions Model::launch(const AppSpec &spec, std::uint64_t now)
{
    Actions out;
    const AppId existing = find_key(spec.key);
    if (existing != kNoApp) {
        const Entry *e = find(existing);
        if (e->state == State::Closing) {
            notice(existing, e->spec.name + " is still closing", out);
            return out;
        }
        return resume(existing, now);
    }
    if (front_ != kNoApp) {
        Entry *f = get(front_);
        if (f && f->state == State::Foreground && f->lifecycle()) {
            pending_ = Pending();
            pending_.kind = Pending::Kind::Launch;
            pending_.spec = spec;
            start_hide(*f, now, out);
        } else if (f && (f->state == State::Hiding || f->state == State::Closing)) { /* runs when it ends */
            pending_ = Pending();
            pending_.kind = Pending::Kind::Launch;
            pending_.spec = spec;
        } else {
            notice(kNoApp, (f ? f->spec.name : std::string("The app in front")) + " cannot stay in the background", out);
        }
        sync_derived(out);
        return out;
    }
    launch_into(spec, now, out);
    sync_derived(out);
    return out;
}

Actions Model::resume(AppId id, std::uint64_t now)
{
    Actions out;
    Entry *e = get(id);
    if (!e) return out;
    if (e->state == State::Foreground) return out;
    if (e->state == State::Closing) {
        notice(id, e->spec.name + " is still closing", out);
        return out;
    }
    if (e->state == State::Hiding) {
        pending_ = Pending();
        pending_.kind = Pending::Kind::App;
        pending_.id = id;
        return out;
    }
    if (front_ != kNoApp) {
        Entry *f = get(front_);
        if (f && f->state == State::Foreground && f->lifecycle()) {
            pending_ = Pending();
            pending_.kind = Pending::Kind::App;
            pending_.id = id;
            start_hide(*f, now, out);
        } else if (f && (f->state == State::Hiding || f->state == State::Closing)) { /* runs when it ends */
            pending_ = Pending();
            pending_.kind = Pending::Kind::App;
            pending_.id = id;
        } else if (f) {
            notice(f->id, f->spec.name + " cannot stay in the background", out);
        }
        sync_derived(out);
        return out;
    }
    show(id, now, out);
    sync_derived(out);
    return out;
}

Actions Model::go_launcher(std::uint64_t now)
{
    Actions out;
    if (front_ == kNoApp) return out;
    Entry *f = get(front_);
    if (!f) return out;
    if (f->state == State::Hiding) {
        pending_ = Pending();
        pending_.kind = Pending::Kind::Launcher;
        return out;
    }
    if (f->state != State::Foreground) return out; /* closing: the launcher comes back when it exits */
    if (!f->lifecycle()) {
        notice(f->id, f->spec.name + " cannot stay in the background", out);
        return out;
    }
    pending_ = Pending();
    pending_.kind = Pending::Kind::Launcher;
    start_hide(*f, now, out);
    sync_derived(out);
    return out;
}

Actions Model::close_for(AppId id, CloseReason reason, std::uint64_t now)
{
    Actions out;
    Entry *e = get(id);
    if (!e || e->state == State::Closing) return out;
    close_into(*e, reason, now, out);
    sync_derived(out);
    return out;
}

Actions Model::close_all(std::uint64_t now)
{
    Actions out;
    for (Entry &e : entries_)
        if (is_hidden(e.state)) close_into(e, CloseReason::CloseAll, now, out);
    sync_derived(out);
    return out;
}

Actions Model::force_close_front(std::uint64_t now)
{
    Actions out;
    if (front_ == kNoApp) return out;
    Entry *f = get(front_);
    if (!f || f->term_sent) return out;
    if (f->state != State::Closing) {
        f->closed_from = f->state;
        f->close_reason = CloseReason::EscHold;
    }
    out.push_back(act(Op::Term, f->id));
    f->term_sent = true;
    f->term_at = 0;
    f->kill_at = now + config_.term_kill_ms;
    set_state(*f, State::Closing, now);
    sync_derived(out);
    return out;
}

Actions Model::shutdown(std::uint64_t now)
{
    Actions out;
    pending_ = Pending();
    for (Entry &e : entries_) {
        if (e.state == State::Gone || e.term_sent) continue;
        if (e.state != State::Closing) {
            e.closed_from = e.state;
            e.close_reason = CloseReason::Shutdown;
        }
        out.push_back(act(Op::Cont, e.id));
        out.push_back(act(Op::Term, e.id));
        e.term_sent = true;
        e.term_at = 0;
        e.kill_at = now + config_.term_kill_ms;
        set_state(e, State::Closing, now);
    }
    sync_derived(out);
    return out;
}

Actions Model::key_session(bool raw_active, std::uint64_t now)
{
    (void)now;
    Actions out;
    raw_session_ = raw_active;
    sync_derived(out);
    return out;
}

/* ------------------------------------------------------------------------------------------------ reports */

Actions Model::spawned(AppId id, int pgid, bool has_control, int oom_base, std::uint64_t now)
{
    (void)now;
    Actions out;
    Entry *e = get(id);
    if (!e) return out;
    e->pgid = pgid;
    e->has_control = has_control;
    e->oom_base = oom_base;
    e->oom_now = oom_base;
    return out;
}

Actions Model::spawn_failed(AppId id, std::uint64_t now)
{
    Actions out;
    Entry *e = get(id);
    if (!e) return out;
    notice(id, "Cannot start " + e->spec.name, out);
    set_state(*e, State::Gone, now);
    if (front_ == id) set_front(kNoApp, true, out);
    erase_gone();
    sync_derived(out);
    return out;
}

Actions Model::ack(AppId id, std::uint32_t n, std::uint64_t now)
{
    Actions out;
    const Entry *e = find(id);
    if (!e) return out;
    if (e->state == State::Hiding && n == e->hide_n) {
        hide_done(id, true, now, out);
        sync_derived(out);
    }
    return out; /* an ack to "show", or a late one: the supervisor only logs it */
}

Actions Model::control_closed(AppId id, std::uint64_t now)
{
    Actions out;
    Entry *e = get(id);
    if (!e) return out;
    e->control_lost = true;
    if (e->state == State::Hiding) {
        hide_done(id, false, now, out);
        sync_derived(out);
    }
    return out;
}

Actions Model::exited(AppId id, std::uint64_t now)
{
    Actions out;
    Entry *e = get(id);
    if (!e) return out;
    const State prev = e->state;
    const State closed_from = e->closed_from;
    const bool was_front = front_ == id;
    const std::string name = e->spec.name;
    if (e->has_snapshot) out.push_back(act(Op::DropSnapshot, id));
    set_state(*e, State::Gone, now);
    if (prev != State::Closing && !was_front) notice(id, name + " exited", out);
    if (was_front) {
        /* the app in front (or being hidden) is gone: a pending target, else the launcher (republish) */
        if (!run_pending(now, out)) set_front(kNoApp, true, out);
    } else if (front_ == kNoApp && prev == State::Closing && closed_from == State::NotResponding) {
        out.push_back(act(Op::Repaint, kNoApp)); /* it never acked hide: it may have drawn once continued */
    }
    erase_gone();
    sync_derived(out);
    return out;
}

Actions Model::tick(std::uint64_t now)
{
    Actions out;
    std::vector<AppId> ids;
    for (const Entry &e : entries_) ids.push_back(e.id);
    for (const AppId id : ids) {
        Entry *e = get(id);
        if (!e) continue;
        if (e->state == State::Hiding && now >= e->hide_deadline) {
            hide_done(id, false, now, out);
            continue;
        }
        if (e->state != State::Closing) continue;
        if (!e->term_sent && e->term_at != 0 && now >= e->term_at) {
            out.push_back(act(Op::Cont, id));
            out.push_back(act(Op::Term, id));
            e->term_sent = true;
            e->term_at = 0;
            e->kill_at = now + config_.term_kill_ms;
        } else if (e->term_sent && !e->kill_sent && e->kill_at != 0 && now >= e->kill_at) {
            out.push_back(act(Op::Kill, id));
            e->kill_sent = true;
        }
    }
    sync_derived(out);
    return out;
}

Actions Model::memory(long available_kb, std::uint64_t now)
{
    Actions out;
    last_memory_kb_ = available_kb;
    if (available_kb < 0 || hidden_count() == 0) return out;
    for (const Entry &e : entries_)
        if (e.state == State::Closing && e.close_reason == CloseReason::Memory) return out; /* one at a time */
    if (available_kb >= config_.floor_freeze_kb) return out;
    Entry *victim = lru_hidden(true);
    if (!victim && available_kb < config_.floor_run_kb) victim = lru_hidden(false);
    if (!victim) return out;
    notice(victim->id,
           "Closed " + victim->spec.name + ": low memory (" + std::to_string(available_kb / 1024) + " MB free)", out);
    close_into(*victim, CloseReason::Memory, now, out);
    sync_derived(out);
    return out;
}

} // namespace multitask
