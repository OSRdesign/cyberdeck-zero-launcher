/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Date & Time > Time zone (see settings_native_timezone_page.hpp).
 */

#include "settings_native_timezone_page.hpp"

#include "lvgl_components.hpp"
#include "settings_hw_profile.hpp"
#include "settings_timezone_model.hpp"

#include "cp0_timedate_client.hpp"
#include "cp0_timezone.hpp"
#include "cp0_ui_widgets.hpp"
#include "hal_lvgl_bsp.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The Pi boards set the clock over D-Bus (no hardware RTC, a PolicyKit rule): the zone goes the same way, with the sudo
// path as the fallback. The other builds take the sudo path only (as their Network Time and clock do).
#if APPLAUNCH_SETTINGS_TIME_NO_SUDO && defined(__linux__) && !defined(HAL_PLATFORM_SDL)
#define APPLAUNCH_TZ_USE_DBUS 1
#endif

namespace {

constexpr std::chrono::milliseconds kListTimeout{15000};
constexpr std::chrono::milliseconds kSetTimeout{40000}; /* cp0_timedate_set_timezone waits up to 30 s */
constexpr int kSudoAuthTimeoutMs = 60000;               /* the Date & Time page's (settings_rtc_page.cpp) */
constexpr int kSudoExecTimeoutMs = 30000;

/* The workers (the legacy component base's async tasks: a worker thread, the result on the LVGL thread, cancelled
 * and joined when the panel goes). */
class Tasks : public DComponens::LvglComponensBase {
public:
    void create_ui(lv_obj_t *) override {}
    void LoadNextPage() override {}
    void LeaveNextPage() override {}
};

struct Loaded {
    std::vector<std::string> zones;
    std::string current;
};

std::string read_current_zone()
{
    char zone[cp0::tz::kMaxNameBytes + 1] = {};
    return cp0_timedate_get_timezone(zone, static_cast<int>(sizeof(zone))) == 0 ? std::string(zone) : std::string();
}

Loaded load_zones()
{
    Loaded loaded;
    if (char *list = cp0_timedate_list_timezones()) {
        loaded.zones = cp0::tz::parse_list(list);
        std::free(list);
    }
    loaded.current = read_current_zone();
    return loaded;
}

class TimezonePanel : public SettingsNativePanel {
public:
    explicit TimezonePanel(const SettingsNativePanelContext &context)
        : page_(context.page), group_(context.group), title_(context.title), back_(context.back),
          alive_(std::make_shared<bool>(true))
    {
        cp0::ui::RollerEvents events;
        events.activate = [this](int index) { activate(index); };
        events.back = back_; /* the host asks back() first: one level up */
        roller_ = std::make_unique<cp0::ui::Roller>(page_.root(), page_.layout(), std::move(events));
        if (group_) {
            lv_group_add_obj(group_, roller_->focus_object());
            lv_group_focus_obj(roller_->focus_object());
        }
        current_ = read_current_zone();
        tasks_ = std::make_unique<Tasks>();
        load();
    }

    ~TimezonePanel() override
    {
        *alive_ = false;
        if (sudo_request_ != 0) cp0_signal_sudo_cancel(sudo_request_, [](int) {});
        sudo_request_ = 0;
        tasks_.reset(); /* cancels and joins the workers before the list goes */
        roller_.reset();
        page_.set_caption("", false);
    }

    bool back() override
    {
        if (closing_) return false;
        if (busy_) return true; /* a change is on its way (the sudo prompt has its own Cancel) */
        if (level_ == Level::Zones) {
            show_regions(region_);
            return true;
        }
        return false;
    }

private:
    enum class Level { Loading, Regions, Zones };

    void caption(const std::string &text, bool error) { page_.set_caption(text, error); }

    void load()
    {
        caption("Loading time zones...", false);
        DComponens::LvglComponensBase::AsyncTaskCallbacks<Loaded> callbacks;
        callbacks.execute = [] { return load_zones(); };
        callbacks.on_complete = [this](DComponens::LvglComponensBase::AsyncTaskContext &, const Loaded &loaded) {
            catalog_ = settings_tz::Catalog(loaded.zones);
            if (!loaded.current.empty()) current_ = loaded.current;
            caption(catalog_.empty() ? "Time zone list unavailable" : "", catalog_.empty());
            show_regions(settings_tz::region_of(current_));
        };
        callbacks.on_timeout = [this](DComponens::LvglComponensBase::AsyncTaskContext &) {
            caption("Time zone list unavailable", true);
            show_regions(settings_tz::region_of(current_)); /* UTC stays possible */
        };
        callbacks.on_schedule_failed = callbacks.on_timeout;
        DComponens::LvglComponensBase::AsyncTaskOptions options;
        options.timeout = kListTimeout;
        tasks_->run_async_task(std::move(callbacks), options);
    }

    void show_regions(const std::string &select)
    {
        level_ = Level::Regions;
        regions_ = catalog_.regions();
        const std::string current_region = settings_tz::region_of(current_);
        std::vector<cp0::ui::RowSpec> rows;
        rows.reserve(regions_.size());
        for (const std::string &region : regions_) {
            cp0::ui::RowSpec spec;
            spec.label = region;
            if (region == settings_tz::kUtc) {
                spec.kind = cp0::ui::RowKind::Choice; /* a zone of its own: applied at once */
                spec.on = current_region == settings_tz::kUtc;
            } else {
                spec.chevron = true; /* opens its zones */
                if (region == current_region) spec.value = settings_tz::city_label(current_, region);
            }
            rows.push_back(std::move(spec));
        }
        const int selected = settings_tz::index_of(regions_, select);
        roller_->set_rows(rows, selected < 0 ? 0 : selected, true);
        page_.set_title(title_);
    }

    void show_zones(const std::string &region)
    {
        level_ = Level::Zones;
        region_ = region;
        zones_ = catalog_.zones_of(region);
        std::vector<cp0::ui::RowSpec> rows;
        rows.reserve(zones_.size());
        for (const std::string &zone : zones_) {
            cp0::ui::RowSpec spec;
            spec.kind = cp0::ui::RowKind::Choice;
            spec.label = settings_tz::city_label(zone, region);
            spec.on = zone == current_;
            rows.push_back(std::move(spec));
        }
        const int selected = settings_tz::index_of(zones_, current_);
        roller_->set_rows(rows, selected < 0 ? 0 : selected, false); /* a value list, as Settings' option lists */
        std::vector<std::string> title = title_;
        title.push_back(region);
        page_.set_title(title);
    }

    void activate(int index)
    {
        if (busy_ || closing_ || index < 0) return;
        if (level_ == Level::Regions && index < static_cast<int>(regions_.size())) {
            const std::string region = regions_[static_cast<std::size_t>(index)];
            if (region == settings_tz::kUtc) apply(catalog_.utc_zone());
            else show_zones(region);
        } else if (level_ == Level::Zones && index < static_cast<int>(zones_.size())) {
            apply(zones_[static_cast<std::size_t>(index)]);
        }
    }

    /* Only a zone of the list (or the UTC entry's) and a name cp0::tz::valid_name() accepts ever leaves this page. */
    void apply(const std::string &zone)
    {
        if (!catalog_.contains(zone) || !cp0::tz::valid_name(zone)) {
            caption(settings_tz::outcome_text(settings_tz::Outcome::Invalid), true);
            return;
        }
        if (zone == current_ || (settings_tz::is_utc(zone) && settings_tz::is_utc(current_))) {
            close(); /* nothing to change */
            return;
        }
        busy_ = true;
        pending_ = zone;
        caption("Setting the time zone...", false);
#ifdef APPLAUNCH_TZ_USE_DBUS
        DComponens::LvglComponensBase::AsyncTaskCallbacks<int> callbacks;
        callbacks.execute = [zone] { return cp0_timedate_set_timezone(zone.c_str()); };
        callbacks.on_complete = [this](DComponens::LvglComponensBase::AsyncTaskContext &, const int &code) {
            if (settings_tz::outcome_of_dbus(code) == settings_tz::Outcome::Refused) apply_with_sudo();
            else finish(settings_tz::outcome_of_dbus(code));
        };
        callbacks.on_timeout = [this](DComponens::LvglComponensBase::AsyncTaskContext &) {
            finish(settings_tz::Outcome::TimedOut);
        };
        callbacks.on_schedule_failed = [this](DComponens::LvglComponensBase::AsyncTaskContext &) {
            finish(settings_tz::Outcome::Failed);
        };
        DComponens::LvglComponensBase::AsyncTaskOptions options;
        options.timeout = kSetTimeout;
        tasks_->run_async_task(std::move(callbacks), options);
#else
        apply_with_sudo();
#endif
    }

    /* `timedatectl set-timezone ZONE` as root after the shared sudo password prompt (cp0_sudo_async.cpp: TimezoneSet
     * validates the name again and passes it as one argument, no shell). Both callbacks come on the LVGL thread. */
    void apply_with_sudo()
    {
        std::weak_ptr<bool> alive = alive_;
        auto answered = std::make_shared<bool>(false);
        cp0_signal_system_admin_async(
            std::list<std::string>{"TimezoneSet", pending_}, kSudoAuthTimeoutMs, kSudoExecTimeoutMs,
            [this, alive](int result, int exit_code) {
                const auto live = alive.lock();
                if (!live || !*live) return;
                sudo_request_ = 0;
                finish(settings_tz::outcome_of_sudo(result, exit_code));
            },
            [this, alive, answered](int start_code, std::uint64_t request_id) {
                *answered = true;
                const auto live = alive.lock();
                if (!live || !*live) return;
                if (start_code != 0) finish(settings_tz::Outcome::Failed);
                else sudo_request_ = request_id;
            });
        if (!*answered) finish(settings_tz::Outcome::Refused); /* no sudo service in this process */
    }

    void finish(settings_tz::Outcome outcome)
    {
        if (!busy_) return;
        busy_ = false;
        if (outcome != settings_tz::Outcome::Done) {
            caption(settings_tz::outcome_text(outcome), true);
            return;
        }
        current_ = pending_;
        cp0_timedate_zone_changed(); /* the status clock, Info, Set Manually: the new local time at once */
        close();
    }

    /* Back to Date & Time (its row reads the zone again) */
    void close()
    {
        closing_ = true;
        if (back_) back_();
    }

    cp0::ui::Page &page_;
    lv_group_t *group_ = nullptr;
    std::vector<std::string> title_;
    std::function<void()> back_;
    std::shared_ptr<bool> alive_; /* false once the panel goes: late sudo callbacks do nothing */
    std::unique_ptr<cp0::ui::Roller> roller_;
    std::unique_ptr<Tasks> tasks_;
    settings_tz::Catalog catalog_;
    Level level_ = Level::Loading;
    std::vector<std::string> regions_; /* level 1 rows */
    std::vector<std::string> zones_;   /* level 2 rows (full names) */
    std::string region_;               /* level 2's region */
    std::string current_;              /* the zone in use */
    std::string pending_;              /* the zone being applied */
    std::uint64_t sudo_request_ = 0;   /* the sudo path's request (cancelled when the panel goes) */
    bool busy_ = false;
    bool closing_ = false;
};

} // namespace

std::unique_ptr<DComponens::LvglComponensBase> settings_timezone_page_factory(lv_obj_t *, const NodeIter &,
                                                                              std::function<void()>)
{
    return nullptr; /* native only: settings_native_panel_create() */
}

std::unique_ptr<SettingsNativePanel> settings_timezone_panel_create(const SettingsNativePanelContext &context)
{
    return std::make_unique<TimezonePanel>(context);
}

std::string settings_timezone_row_value()
{
    return settings_tz::row_value(read_current_zone());
}

namespace settings_timezone {

void append_entry(Tree &tree, const NodeIter &date_time)
{
    tree.append_child(date_time, SettingEntry{"Time zone", settings_timezone_page_factory, PageType::Normal});
}

} // namespace settings_timezone
