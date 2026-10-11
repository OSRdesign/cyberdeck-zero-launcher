/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl/lvgl.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <functional>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include "cp0_font_service.hpp"
#include "settings_fonts.hpp"
#include "cp0_lvgl_app_page_assets.h"
#include "settings_choice_binding.hpp"
#include "settings_tree_types.hpp"

#define lv_event_get_target(e) (lv_obj_t *)lv_event_get_target(e)

// Base component class (LvglComponensBase) shared by the legacy Settings pages and the value pages below.
namespace DComponens {

struct LvglBindContext {
    std::function<void(lv_event_t *)> fun;
    void *user_data;
    lv_event_code_t event_code;
};

static void lvgl_bind_context_free(void *data)
{
    delete static_cast<LvglBindContext *>(data);
}

static void LvglBindCallback(lv_event_t *event)
{
    LvglBindContext *context = static_cast<LvglBindContext *>(lv_event_get_user_data(event));
    if (!context) return;

#if !LV_USE_EXT_DATA
    const lv_event_code_t event_code = lv_event_get_code(event);
    if (event_code == LV_EVENT_DELETE) {
        if (context->event_code == LV_EVENT_DELETE || context->event_code == LV_EVENT_ALL) {
            if (context->fun) context->fun(event);
        }
        delete context;
        return;
    }
    if (context->event_code != LV_EVENT_ALL && context->event_code != event_code) return;
#endif

    if (!context->fun) return;
    context->fun(event);
}

template <typename Fun>
bool lvgl_bind_event(lv_obj_t *obj, lv_event_code_t event_code, void *user_data, Fun &&fun)
{
    if (!obj) return false;

    LvglBindContext *context = new LvglBindContext{
        std::function<void(lv_event_t *)>(std::forward<Fun>(fun)), user_data, event_code};
#if LV_USE_EXT_DATA
    lv_event_dsc_t *dsc = lv_obj_add_event_cb(obj, LvglBindCallback, event_code, context);
#else
    lv_event_dsc_t *dsc = lv_obj_add_event_cb(obj, LvglBindCallback, LV_EVENT_ALL, context);
#endif
    if (!dsc) {
        delete context;
        return false;
    }
#if LV_USE_EXT_DATA
    lv_event_desc_set_external_data(dsc, context, lvgl_bind_context_free);
#endif
    return true;
}
// #endif

class LvglComponensBase {
public:
    enum class AsyncTaskStage {
        Starting,
        Submitted,
        Waiting,
        Completed,
        TimedOut,
        Failed,
        ScheduleFailed,
    };
    struct AsyncTaskContext {
        using Clock = std::chrono::steady_clock;

        AsyncTaskStage stage           = AsyncTaskStage::Starting;
        Clock::time_point started_at   = Clock::now();
        Clock::time_point last_poll_at = started_at;
        std::size_t poll_count         = 0;
    };

    struct AsyncTaskOptions {
        std::chrono::milliseconds timeout{3000};
    };

    template <typename Result>
    struct AsyncTaskCallbacks {
        std::function<Result()> execute;
        std::function<void(AsyncTaskContext &)> on_start;
        std::function<void(AsyncTaskContext &)> on_submitted;
        std::function<void(AsyncTaskContext &)> on_wait;
        std::function<void(AsyncTaskContext &, const Result &)> on_complete;
        std::function<void(AsyncTaskContext &, std::exception_ptr)> on_exception;
        std::function<void(AsyncTaskContext &)> on_timeout;
        std::function<void(AsyncTaskContext &)> on_schedule_failed;
    };

    using AsyncDispatch = SettingsAsync::Dispatch;
    using AsyncToken = AsyncDispatch::Token;
    using LifetimeToken = AsyncToken;
    using AsyncTaskRegistry = SettingsAsync::SettingsAsyncTaskRegistry;

    lv_obj_t *ComponensObj = nullptr;
    bool NextActive = false;

    lv_obj_t *Get() const
    {
        return ComponensObj;
    }
    // Resting x of this page's root object inside its parent, in the parent's
    // coordinate space.  Full-width pages (320px value / info / confirm pages)
    // rest at the left edge; the nested roller page draws its rows in the
    // right-hand column and overrides this.  The page-3 slide animation uses
    // it as the on-screen resting position in both directions.
    virtual int panel_x() const
    {
        return 0;
    }
    void SetPos(int32_t x, int32_t y) const
    {
        lv_obj_set_pos(ComponensObj, x, y);
    }
    void SetSize(int32_t w, int32_t h) const
    {
        lv_obj_set_size(ComponensObj, w, h);
    }
    void OnEvent(lv_event_code_t event_code, std::function<void(lv_event_t *)> &&fun, void *user_data) const
    {
        DComponens::lvgl_bind_event(ComponensObj, event_code, user_data,
                                    std::forward<std::function<void(lv_event_t *)>>(fun));
    }
    void SetWidth(int32_t w) const
    {
        lv_obj_set_width(ComponensObj, w);
    }
    void SetHeight(int32_t h) const
    {
        lv_obj_set_height(ComponensObj, h);
    }

    virtual void AnimateNextIn(std::function<void()> AnimateOverFunc)
    {
        if (AnimateOverFunc) AnimateOverFunc();
    };
    virtual void AnimateNextOut(std::function<void()> AnimateOverFunc)
    {
        if (AnimateOverFunc) AnimateOverFunc();
    };

    virtual void LoadNextPage()  = 0;
    virtual void LeaveNextPage() = 0;

    virtual void SetSelfUiMode(PageType) {}

    std::function<void()> LeaveSelfPage = nullptr;

    LvglComponensBase() = default;
    virtual ~LvglComponensBase()
    {
        cancel_async_tasks();
    }

    bool ensure_async_dispatch()
    {
        if (async_dispatch_timer_) return true;
        if (async_dispatch_unavailable_) return false;

        async_dispatch_timer_ = lv_timer_create(&LvglComponensBase::async_dispatch_timer_cb, 20, this);
        if (!async_dispatch_timer_) {
            async_dispatch_unavailable_ = true;
            async_dispatch_ready_.store(false, std::memory_order_release);
            async_dispatch_.cancel();
            return false;
        }
        async_dispatch_ready_.store(true, std::memory_order_release);
        return true;
    }

    AsyncToken async_token() const noexcept
    {
        return async_dispatch_.token();
    }

    LifetimeToken lifetime_token() const noexcept
    {
        return async_token();
    }

    bool enqueue_async(AsyncToken token, std::function<void()> callback) noexcept
    {
        if (!async_dispatch_ready_.load(std::memory_order_acquire)) return false;
        return AsyncDispatch::enqueue_from_callback(token, std::move(callback));
    }

    bool enqueue_async(std::function<void()> callback) noexcept
    {
        return enqueue_async(async_token(), std::move(callback));
    }

    uint64_t advance_async_generation() noexcept
    {
        return async_dispatch_.advance_generation();
    }

    void cancel_async_tasks() noexcept
    {
        async_dispatch_ready_.store(false, std::memory_order_release);
        async_dispatch_.cancel();
        if (async_dispatch_timer_) {
            lv_timer_delete(async_dispatch_timer_);
            async_dispatch_timer_ = nullptr;
        }
        async_task_registry_.cancel();
        async_task_registry_.join_all();
    }

    void cancel() noexcept
    {
        cancel_async_tasks();
    }

    void reap_finished()
    {
        async_task_registry_.reap_finished();
    }

    void join_all() noexcept
    {
        async_task_registry_.join_all();
    }

    AsyncTaskRegistry &async_tasks() noexcept
    {
        return async_task_registry_;
    }

    template <typename Result>
    bool run_async_task(AsyncTaskCallbacks<Result> callbacks)
    {
        return run_async_task(std::move(callbacks), AsyncTaskOptions{});
    }

    template <typename Result>
    bool run_async_task(AsyncTaskCallbacks<Result> callbacks, AsyncTaskOptions options)
    {
        AsyncTaskContext failed_context;
        if (!callbacks.execute || !ensure_async_dispatch()) {
            failed_context.stage = AsyncTaskStage::ScheduleFailed;
            invoke_async_callback(callbacks.on_schedule_failed, failed_context);
            return false;
        }

        const auto on_schedule_failed = callbacks.on_schedule_failed;
        std::shared_ptr<AsyncTaskState<Result>> state;
        try {
            state = std::make_shared<AsyncTaskState<Result>>(std::move(callbacks), options, async_token());
        } catch (...) {
            failed_context.stage = AsyncTaskStage::ScheduleFailed;
            invoke_async_callback(on_schedule_failed, failed_context);
            return false;
        }
        if (!state->token.valid()) {
            state->context.stage = AsyncTaskStage::ScheduleFailed;
            invoke_async_callback(state->callbacks.on_schedule_failed, state->context);
            return false;
        }

        invoke_async_callback(state->callbacks.on_start, state->context);
        if (!state->token.valid()) return false;

        if (!enqueue_async(state->token, [this, state] { process_async_task(state); })) {
            state->context.stage = AsyncTaskStage::ScheduleFailed;
            invoke_async_callback(state->callbacks.on_schedule_failed, state->context);
            return false;
        }
        return true;
    }

    virtual void create_ui(lv_obj_t *parent) = 0;

private:
    template <typename Result>
    struct AsyncTaskState {
        AsyncTaskCallbacks<Result> callbacks;
        AsyncTaskOptions options;
        AsyncTaskContext context;
        std::promise<Result> promise;
        std::shared_future<Result> future;
        AsyncToken token;
        bool submitted = false;

        AsyncTaskState(AsyncTaskCallbacks<Result> task_callbacks, AsyncTaskOptions task_options,
                       AsyncToken task_token)
            : callbacks(std::move(task_callbacks)), options(task_options), token(std::move(task_token))
        {
        }
    };

    template <typename Callback, typename... Args>
    static void invoke_async_callback(const Callback &callback, Args &&...args) noexcept
    {
        if (!callback) return;
        try {
            callback(std::forward<Args>(args)...);
        } catch (...) {
        }
    }

    static void async_dispatch_timer_cb(lv_timer_t *timer)
    {
        auto *self = timer ? static_cast<LvglComponensBase *>(lv_timer_get_user_data(timer)) : nullptr;
        if (!self) return;
        self->async_task_registry_.reap_finished();
        self->async_dispatch_.drain();
    }

    template <typename Result>
    void schedule_async_task(const std::shared_ptr<AsyncTaskState<Result>> &state)
    {
        if (!state || !state->token.valid()) return;
        if (enqueue_async(state->token, [this, state] { process_async_task(state); })) return;

        state->context.stage = AsyncTaskStage::ScheduleFailed;
        invoke_async_callback(state->callbacks.on_schedule_failed, state->context);
    }

    template <typename Result>
    void process_async_task(const std::shared_ptr<AsyncTaskState<Result>> &state)
    {
        if (!state || !AsyncDispatch::token_is_current(state->token)) return;

        if (!state->submitted) {
            state->submitted            = true;
            state->context.stage        = AsyncTaskStage::Submitted;
            state->context.started_at   = AsyncTaskContext::Clock::now();
            state->context.last_poll_at = state->context.started_at;

            try {
                auto execute = std::move(state->callbacks.execute);
                state->future = state->promise.get_future().share();
                if (!async_task_registry_.start(
                        [state, execute = std::move(execute)]() mutable {
                            try {
                                state->promise.set_value(execute());
                            } catch (...) {
                                try {
                                    state->promise.set_exception(std::current_exception());
                                } catch (...) {
                                }
                            }
                        })) {
                    state->context.stage = AsyncTaskStage::Failed;
                    invoke_async_callback(state->callbacks.on_exception, state->context, std::exception_ptr{});
                    return;
                }
            } catch (...) {
                state->context.stage = AsyncTaskStage::Failed;
                invoke_async_callback(state->callbacks.on_exception, state->context, std::current_exception());
                return;
            }

            invoke_async_callback(state->callbacks.on_submitted, state->context);
            if (!state->token.valid()) return;
            schedule_async_task(state);
            return;
        }

        const auto now   = AsyncTaskContext::Clock::now();
        const bool ready = state->future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        if (!ready && state->options.timeout.count() >= 0 &&
            now - state->context.started_at >= state->options.timeout) {
            state->context.stage = AsyncTaskStage::TimedOut;
            invoke_async_callback(state->callbacks.on_timeout, state->context);
            return;
        }

        if (!ready) {
            state->context.stage        = AsyncTaskStage::Waiting;
            state->context.last_poll_at = now;
            ++state->context.poll_count;
            invoke_async_callback(state->callbacks.on_wait, state->context);
            if (!state->token.valid()) return;
            schedule_async_task(state);
            return;
        }

        try {
            const Result &result = state->future.get();
            state->context.stage = AsyncTaskStage::Completed;
            invoke_async_callback(state->callbacks.on_complete, state->context, result);
        } catch (...) {
            state->context.stage = AsyncTaskStage::Failed;
            invoke_async_callback(state->callbacks.on_exception, state->context, std::current_exception());
        }
    }

    AsyncDispatch async_dispatch_;
    AsyncTaskRegistry async_task_registry_;
    lv_timer_t *async_dispatch_timer_ = nullptr;
    std::atomic_bool async_dispatch_ready_{false};
    bool async_dispatch_unavailable_ = false;
};

// A value page: the 320x150 value roller of the legacy Settings, and the ChoiceBinding the native Settings host
// drives (settings_choice_binding.hpp). Created with a binding host as the parent it builds no view, only the
// logic of its subclass.
class LvSettingValuePage3Base : public DComponens::LvglComponensBase, public SettingsChoiceBinding {
public:
    struct ActivationSink {
        AsyncToken dispatch_token;
        std::weak_ptr<SettingRequestState> request_state;
        uint64_t generation = 0;
        int32_t index = -1;
        LvSettingValuePage3Base *owner = nullptr;

        bool valid() const noexcept
        {
            auto state = request_state.lock();
            return owner != nullptr && state && state->is_current(generation) && state->pending() &&
                   dispatch_token.valid();
        }
    };

    enum class LayoutMetric : int {
        ScreenW         = 320,
        ScreenH         = 150,
        RowH            = 21,
        CenterRow       = 3,
        EdgePadding     = RowH * CenterRow,
        BarX            = 4,
        BarY            = 66,
        BarW            = 312,
        BarH            = 22,
        TitleCenterX    = 60,
        TitleBoxW       = 84,
        ValueListX      = 100,
        ValueListW      = 120,
        ValueCenterX    = ValueListW / 2,
        ValueBoxX       = 16,
        ValueBoxW       = 88,
        RightArrowScale = 224,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    int32_t selected_index = 0;

    LvSettingValuePage3Base() = default;

    // SettingsChoiceBinding: the same Enter as on the legacy page, on option `index`.
    int choice_count() const override
    {
        return static_cast<int>(item_count_);
    }
    int choice_selection() const override
    {
        return selected_index;
    }
    SettingApiResult choice_activate(int index) override
    {
        if (index < 0 || index >= static_cast<int>(item_count_)) return SettingApiResult::NotHandled;
        selected_index = index;
        return activate_selected();
    }
    void choice_set_observer(SettingsChoiceObserver *observer) override
    {
        choice_observer_ = observer;
    }

    bool activation_pending() const noexcept
    {
        return activation_pending_;
    }

    SettingComponentState activation_state() const noexcept
    {
        auto state = activation_state_.lock();
        return state ? state->state() : SettingComponentState::Read;
    }

    ActivationSink activation_sink() const noexcept
    {
        return activation_sink_;
    }

    static bool enqueue_activation_result(const ActivationSink &sink, SettingApiResult result) noexcept
    {
        if (!sink.valid()) return false;

        return SettingsAsync::Dispatch::enqueue_from_callback(
            sink.dispatch_token,
            [sink, result] {
                auto state = sink.request_state.lock();
                if (!state || !sink.owner || !state->is_current(sink.generation) ||
                    !state->pending() || sink.owner->activation_index_ != sink.index)
                    return;
                sink.owner->finish_activation(sink, result);
            });
    }

    void AnimateNextIn(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }
    void AnimateNextOut(std::function<void()> animate_over_func) override
    {
        if (animate_over_func) animate_over_func();
    }
    void LoadNextPage() override
    {
    }
    void LeaveNextPage() override
    {
        if (LeaveSelfPage) LeaveSelfPage();
    }

    ~LvSettingValuePage3Base() override
    {
        cancel_activation(false);
        cancel_async_tasks();
        if (ComponensObj) {
            lv_anim_del(ComponensObj, nullptr);
            lv_obj_delete(ComponensObj);
            ComponensObj = nullptr;
        }
        selection_bg_ = nullptr;
        value_list_   = nullptr;
        title_label_  = nullptr;
        right_arrow_  = nullptr;
        arrow_up_     = nullptr;
        arrow_down_   = nullptr;
        hint_         = nullptr;
    }

    static void style_value_label(lv_obj_t *label, int distance)
    {
        if (!label) return;

        int font_size  = 10;
        int opa        = 130;
        uint32_t color = 0x555555;
        if (distance == 0) {
            font_size = 16;
            opa       = 255;
            color     = 0xFFFFFF;
        } else if (distance == 1) {
            font_size = 12;
            opa       = 220;
            color     = 0xAAAAAA;
        } else if (distance == 2) {
            font_size = 12;
            opa       = 170;
            color     = 0x777777;
        }

        lv_obj_set_style_text_font(
            label, settings_fonts::cjk_sans(font_size), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_opa(label, opa, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(label);

        const bool focused      = distance == 0;
        const int natural_width = lv_obj_get_width(label);
        if (natural_width > metric(LayoutMetric::ValueBoxW)) {
            lv_obj_set_width(label, metric(LayoutMetric::ValueBoxW));
            lv_label_set_long_mode(label, focused ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            lv_obj_set_x(label, metric(LayoutMetric::ValueBoxX));
        } else {
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_update_layout(label);
            lv_obj_set_x(label, metric(LayoutMetric::ValueCenterX) - lv_obj_get_width(label) / 2);
        }

        const int label_y = (metric(LayoutMetric::RowH) - lv_obj_get_height(label)) / 2;
        lv_obj_set_y(label, std::max(0, label_y));
    }

    void update_value_styles()
    {
        int index = 0;
        for (lv_obj_t *row : value_rows_) {
            if (!row) {
                ++index;
                continue;
            }
            lv_obj_t *label = lv_obj_get_child(row, 0);
            style_value_label(label, std::abs(index - selected_index));
            ++index;
        }
        update_right_arrow_position();
    }

    void update_right_arrow_position()
    {
        if (!right_arrow_ || !title_label_ || item_count_ == 0) return;

        lv_obj_t *row   = row_at(selected_index);
        lv_obj_t *label = row ? lv_obj_get_child(row, 0) : nullptr;
        if (!label) return;

        lv_obj_update_layout(label);
        lv_obj_update_layout(right_arrow_);

        const int title_right = lv_obj_get_x(title_label_) + lv_obj_get_width(title_label_);
        const int value_left  = metric(LayoutMetric::ValueListX) + lv_obj_get_x(label);
        const int arrow_width = lv_obj_get_width(right_arrow_);
        const int arrow_x     = std::max(title_right + 4, value_left - 4 - arrow_width);
        const int arrow_y =
            metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(right_arrow_)) / 2;
        lv_obj_set_pos(right_arrow_, arrow_x, std::max(0, arrow_y));
        lv_obj_move_to_index(right_arrow_, 1);
    }

    void update_title_position()
    {
        if (!title_label_) return;

        lv_obj_set_width(title_label_, LV_SIZE_CONTENT);
        lv_label_set_long_mode(title_label_, LV_LABEL_LONG_CLIP);
        lv_obj_update_layout(title_label_);

        if (lv_obj_get_width(title_label_) > metric(LayoutMetric::TitleBoxW)) {
            lv_obj_set_width(title_label_, metric(LayoutMetric::TitleBoxW));
            lv_obj_set_x(title_label_,
                         metric(LayoutMetric::TitleCenterX) - metric(LayoutMetric::TitleBoxW) / 2);
        } else {
            lv_obj_set_x(title_label_,
                         metric(LayoutMetric::TitleCenterX) - lv_obj_get_width(title_label_) / 2);
        }

        const int label_y = metric(LayoutMetric::BarY) +
                            (metric(LayoutMetric::BarH) - lv_obj_get_height(title_label_)) / 2;
        lv_obj_set_y(title_label_, std::max(0, label_y));
    }

    void update_arrow_visibility()
    {
        if (arrow_up_) {
            if (selected_index > 0)
                lv_obj_remove_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_up_, LV_OBJ_FLAG_HIDDEN);
        }
        if (arrow_down_) {
            if (selected_index + 1 < static_cast<int32_t>(item_count_))
                lv_obj_remove_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(arrow_down_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void scroll_to_selected(bool animated)
    {
        if (!value_list_ || item_count_ == 0) return;

        lv_obj_t *row = row_at(selected_index);
        if (!row) return;

        lv_obj_scroll_to_view(row, animated ? LV_ANIM_ON : LV_ANIM_OFF);
        update_value_styles();
        update_arrow_visibility();
    }

    // Reserve a fixed footer below the scrollable value list for page-specific
    // status content without changing the selection bar or arrow positions.
    void reserve_footer(int height)
    {
        if (!value_list_) return;
        const int footer_height = std::clamp(height, 0, metric(LayoutMetric::ScreenH));
        const int footer_start = metric(LayoutMetric::ScreenH) - footer_height;
        const int bar_center = metric(LayoutMetric::BarY) + metric(LayoutMetric::BarH) / 2;
        const int list_y = std::clamp(2 * bar_center - footer_start, 0, footer_start);
        lv_obj_set_y(value_list_, list_y);
        lv_obj_set_height(value_list_, footer_start - list_y);
        lv_obj_update_layout(value_list_);
        scroll_to_selected(false);
    }

    void select(int index)
    {
        if (item_count_ == 0) return;
        selected_index = std::clamp(index, 0, static_cast<int>(item_count_ - 1));
        scroll_to_selected(false);
        if (choice_observer_) choice_observer_->choice_selected(selected_index);
    }

    lv_obj_t *row_at(int index) const
    {
        if (index < 0 || index >= static_cast<int>(value_rows_.size())) return nullptr;
        auto row = std::next(value_rows_.begin(), index);
        return row == value_rows_.end() ? nullptr : *row;
    }

    void restore_activation_focus()
    {
        if (!ComponensObj) return;
        lv_group_t *group = lv_obj_get_group(ComponensObj);
        if (group) lv_group_focus_obj(ComponensObj);
    }

    void finish_activation(const ActivationSink &sink, SettingApiResult result)
    {
        if (sink.owner != this || sink.index != activation_index_) return;
        auto state = activation_state_.lock();
        if (!state || state != sink.request_state.lock() || !state->is_current(sink.generation) ||
            !state->pending())
            return;
        apply_activation_result(sink, result);
    }

    void apply_activation_result(const ActivationSink &sink, SettingApiResult result)
    {
        auto state = activation_state_.lock();
        if (!state || state != sink.request_state.lock() || !state->is_current(sink.generation)) return;

        if (result == SettingApiResult::Pending) {
            if (state->mark_pending(sink.generation)) {
                activation_pending_ = true;
            } else {
                state->mark_failure(sink.generation);
                activation_pending_    = false;
                activation_index_       = -1;
                activation_generation_  = 0;
                restore_activation_focus();
            }
            return;
        }

        if (result == SettingApiResult::Success) {
            if (!state->mark_success(sink.generation)) return;
            activation_pending_    = false;
            activation_index_      = -1;
            activation_generation_ = 0;
            restore_activation_focus();
            if (LeaveSelfPage) LeaveSelfPage();
            return;
        }

        if (result == SettingApiResult::Cancelled)
            state->mark_cancelled(sink.generation);
        else if (result == SettingApiResult::Failure)
            state->mark_failure(sink.generation);
        else
            state->mark_read();

        activation_pending_    = false;
        activation_index_      = -1;
        activation_generation_ = 0;
        restore_activation_focus();
    }

    void cancel_activation(bool restore_focus = true)
    {
        auto state = activation_state_.lock();
        if (state && activation_index_ >= 0 && state->is_current(activation_generation_))
            state->mark_cancelled(activation_generation_);
        activation_pending_    = false;
        activation_index_      = -1;
        activation_generation_ = 0;
        if (restore_focus) restore_activation_focus();
    }

protected:
    LvSettingValuePage3Base(const NodeIter &parent_node, std::function<void()> back_callback)
        : parent_node_(parent_node)
    {
        LeaveSelfPage = std::move(back_callback);
    }

    void initialize(lv_obj_t *parent)
    {
        create_ui(parent);
    }

    const NodeIter &parent_node() const
    {
        return parent_node_;
    }

    // Subclasses pass their status line here too (the native host shows it; the legacy view keeps its label).
    void report_status(const std::string &text, bool error)
    {
        if (choice_observer_) choice_observer_->choice_status(text, error);
    }

    // True when the page was created on a binding host: logic only, no view.
    bool headless() const noexcept
    {
        return headless_;
    }

    virtual int initial_selection() const = 0;

    virtual SettingApiResult activate_selected()
    {
        if (item_count_ == 0 || activation_pending_ || selected_index < 0 ||
            selected_index >= static_cast<int32_t>(item_count_))
            return SettingApiResult::NotHandled;

        // The page node outlives this short-lived page3 object.  Store only
        // after an explicit OK/ENTER so ESC/LEFT keeps the previous value.
        parent_node_->selected_index = selected_index;
        auto selected_node = std::next(parent_node_.begin(), selected_index);
        if (!selected_node->has_api()) return SettingApiResult::NotHandled;

        const auto request_state = selected_node->request_state;
        const uint64_t generation = request_state->begin_activation();
        if (generation == 0) {
            activation_state_ = request_state;
            restore_activation_focus();
            return SettingApiResult::Pending;
        }

        activation_state_      = request_state;
        activation_generation_ = generation;
        activation_index_      = selected_index;

        if (!ensure_async_dispatch()) {
            request_state->mark_failure(generation);
            activation_generation_ = 0;
            activation_index_      = -1;
            restore_activation_focus();
            return SettingApiResult::Failure;
        }

        activation_pending_     = true;
        activation_sink_        = ActivationSink{async_token(), activation_state_, generation, selected_index, this};
        if (!activation_sink_.valid()) {
            request_state->mark_failure(generation);
            activation_pending_     = false;
            activation_generation_ = 0;
            activation_index_      = -1;
            restore_activation_focus();
            return SettingApiResult::Failure;
        }

        SettingApiResult result = SettingApiResult::NotHandled;
        try {
            if (selected_node->Async_api) {
                result = selected_node->Async_api(SettingApiActivate, this);
            } else if (selected_node->Componens_api) {
                selected_node->Componens_api(SettingApiActivate, this);
                result = selected_node->activation_policy == SettingActivationPolicy::WaitForResult
                             ? SettingApiResult::Pending
                             : SettingApiResult::Success;
            }
        } catch (...) {
            result = SettingApiResult::Failure;
        }

        apply_activation_result(activation_sink_, result);
        return result;
    }

    void handle_key_event(lv_event_t *event)
    {
        if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return;

        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (activation_pending_) cancel_activation();
            if (LeaveSelfPage) LeaveSelfPage();
            lv_event_stop_processing(event);
            return;
        }

        if (key == LV_KEY_UP) {
            if (selected_index > 0) {
                --selected_index;
                scroll_to_selected(true);
            }
        } else if (key == LV_KEY_DOWN) {
            if (selected_index + 1 < static_cast<int32_t>(item_count_)) {
                ++selected_index;
                scroll_to_selected(true);
            }
        } else if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            activate_selected();
        }

        lv_event_stop_processing(event);
    }

    void create_ui(lv_obj_t *parent) override
    {
        if (!parent) return;

        ensure_async_dispatch();

        if (settings_is_binding_host(parent)) {
            // Logic only (native Settings host): a hidden root for the subclass' own objects (status label), the
            // option count and the initial selection; no rows, bar, arrows or key handler.
            headless_ = true;
            ComponensObj = lv_obj_create(parent);
            if (!ComponensObj) return;
            lv_obj_remove_style_all(ComponensObj);
            lv_obj_add_flag(ComponensObj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
            item_count_ = 0;
            for (auto it = parent_node_.begin(); it != parent_node_.end(); ++it) ++item_count_;
            if (item_count_ > 0) {
                const int saved_selection = parent_node_->selected_index;
                select(saved_selection >= 0 ? saved_selection : initial_selection());
            }
            return;
        }

        ComponensObj = lv_obj_create(parent);
        if (!ComponensObj) return;
        lv_obj_set_size(ComponensObj, metric(LayoutMetric::ScreenW), metric(LayoutMetric::ScreenH));
        lv_obj_set_pos(ComponensObj, 0, 0);
        lv_obj_set_style_bg_color(ComponensObj, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ComponensObj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ComponensObj, 0, LV_PART_MAIN);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(ComponensObj, LV_OBJ_FLAG_SCROLLABLE);
        DComponens::lvgl_bind_event(ComponensObj, LV_EVENT_KEY, nullptr,
                                    std::bind(&LvSettingValuePage3Base::handle_key_event, this, std::placeholders::_1));

        selection_bg_ = lv_obj_create(ComponensObj);
        if (selection_bg_) {
            lv_obj_set_size(selection_bg_, metric(LayoutMetric::BarW), metric(LayoutMetric::BarH));
            lv_obj_set_pos(selection_bg_, metric(LayoutMetric::BarX), metric(LayoutMetric::BarY));
            lv_obj_set_style_bg_color(selection_bg_, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(selection_bg_, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(selection_bg_, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(selection_bg_, 0, LV_PART_MAIN);
            lv_obj_remove_flag(selection_bg_, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(selection_bg_, LV_OBJ_FLAG_SCROLLABLE);
        }

        value_list_ = lv_obj_create(ComponensObj);
        if (value_list_) {
            lv_obj_set_size(value_list_, metric(LayoutMetric::ValueListW), metric(LayoutMetric::ScreenH));
            lv_obj_set_pos(value_list_, metric(LayoutMetric::ValueListX), 0);
            lv_obj_set_style_bg_opa(value_list_, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_top(value_list_, metric(LayoutMetric::EdgePadding), LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(value_list_, metric(LayoutMetric::EdgePadding), LV_PART_MAIN);
            lv_obj_set_style_pad_row(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(value_list_, 0, LV_PART_MAIN);
            lv_obj_set_style_clip_corner(value_list_, true, LV_PART_MAIN);
            lv_obj_set_flex_flow(value_list_, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_scroll_dir(value_list_, LV_DIR_VER);
            lv_obj_set_scroll_snap_y(value_list_, LV_SCROLL_SNAP_CENTER);
            lv_obj_set_scrollbar_mode(value_list_, LV_SCROLLBAR_MODE_OFF);
            lv_obj_remove_flag(value_list_, LV_OBJ_FLAG_SCROLL_WITH_ARROW);

            for (auto it = parent_node_.begin(); it != parent_node_.end(); ++it) {
                lv_obj_t *row = lv_obj_create(value_list_);
                if (!row) continue;
                value_rows_.push_back(row);
                lv_obj_set_size(row, metric(LayoutMetric::ValueListW), metric(LayoutMetric::RowH));
                lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                lv_obj_t *label = lv_label_create(row);
                if (label) lv_label_set_text(label, it->label.c_str());
            }
            item_count_ = static_cast<uint32_t>(value_rows_.size());
        }

        title_label_ = lv_label_create(ComponensObj);
        if (title_label_) {
            std::string title = parent_node_->label;
            if (title.rfind("BQ ", 0) == 0) title.erase(0, 3);
            lv_label_set_text(title_label_, title.c_str());
            lv_obj_set_style_text_font(
                title_label_,
                settings_fonts::cjk_sans(16),
                LV_PART_MAIN);
            lv_obj_set_style_text_color(title_label_, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            update_title_position();
        }

        right_arrow_ = lv_img_create(ComponensObj);
        if (right_arrow_) {
            lv_img_set_src(right_arrow_, &setting_right_arrow);
            lv_image_set_pivot(right_arrow_, 0, 0);
            lv_image_set_scale(right_arrow_, metric(LayoutMetric::RightArrowScale));
            lv_obj_update_layout(right_arrow_);
            lv_obj_set_pos(
                right_arrow_, metric(LayoutMetric::ValueListX) - lv_obj_get_width(right_arrow_) - 4,
                metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(right_arrow_)) / 2);
        }

        arrow_up_ = lv_img_create(ComponensObj);
        if (arrow_up_) {
            lv_img_set_src(arrow_up_, &setting_red_up);
            lv_obj_update_layout(arrow_up_);
            lv_obj_set_pos(
                arrow_up_,
                metric(LayoutMetric::ValueListX) + metric(LayoutMetric::ValueCenterX) - lv_obj_get_width(arrow_up_) / 2,
                2);
        }

        arrow_down_ = lv_img_create(ComponensObj);
        if (arrow_down_) {
            lv_img_set_src(arrow_down_, &setting_red_down);
            lv_obj_update_layout(arrow_down_);
            lv_obj_set_pos(arrow_down_,
                           metric(LayoutMetric::ValueListX) + metric(LayoutMetric::ValueCenterX) -
                               lv_obj_get_width(arrow_down_) / 2,
                           metric(LayoutMetric::ScreenH) - lv_obj_get_height(arrow_down_) - 4);
        }

        hint_ = lv_label_create(ComponensObj);
        if (hint_) {
            lv_label_set_text(hint_, "ok:set");
            lv_obj_set_style_text_color(hint_, lv_color_hex(0x00CC66), LV_PART_MAIN);
            lv_obj_set_style_text_font(hint_, settings_fonts::sans(16, LV_FREETYPE_FONT_STYLE_BOLD),
                                       LV_PART_MAIN);
            lv_obj_update_layout(hint_);
            lv_obj_set_pos(hint_, metric(LayoutMetric::ScreenW) - 6 - lv_obj_get_width(hint_),
                           metric(LayoutMetric::BarY) + (metric(LayoutMetric::BarH) - lv_obj_get_height(hint_)) / 2);
        }

        if (item_count_ > 0) {
            lv_obj_update_layout(value_list_);
            const int saved_selection = parent_node_->selected_index;
            select(saved_selection >= 0 ? saved_selection : initial_selection());
        } else
            update_arrow_visibility();
    }

private:
    NodeIter parent_node_;
    uint32_t item_count_ = 0;
    std::list<lv_obj_t *> value_rows_;
    std::weak_ptr<SettingRequestState> activation_state_;
    ActivationSink activation_sink_;
    uint64_t activation_generation_ = 0;
    int32_t activation_index_       = -1;
    bool activation_pending_        = false;
    bool headless_                  = false;
    SettingsChoiceObserver *choice_observer_ = nullptr;
    lv_obj_t *selection_bg_ = nullptr;
    lv_obj_t *value_list_   = nullptr;
    lv_obj_t *title_label_  = nullptr;
    lv_obj_t *right_arrow_  = nullptr;
    lv_obj_t *arrow_up_     = nullptr;
    lv_obj_t *arrow_down_   = nullptr;
    lv_obj_t *hint_         = nullptr;
};

}  // namespace DComponens

using DComponens::LvSettingValuePage3Base;
