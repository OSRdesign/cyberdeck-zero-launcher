/*
 * Native full-screen touch calculator for the Raspberry Pi Zero 2W + Waveshare 2.8" DPI LCD port.
 *
 * The only Calculator since report 029: the external 320x170 framebuffer calculator program the
 * inherited code could start instead is gone. Input: touch keypad or the keyboard.
 */

#pragma once

#include "../launcher_ui_app_page.hpp"
#include "calculator_layout.hpp"

#include <array>
#include <memory>
#include <string>

namespace cp0 {
namespace ui {
class Page;
}
} // namespace cp0

class UICalculatorPage : public AppPage
{
public:
    // Lays itself out on the native (full-panel) display.
    static constexpr bool kNativeDisplay = true;

    UICalculatorPage();
    ~UICalculatorPage();
    UICalculatorPage(const UICalculatorPage &) = delete;
    UICalculatorPage &operator=(const UICalculatorPage &) = delete;

private:
    enum class Key : uint8_t {
        Clear, Backspace, LParen, RParen,
        N7, N8, N9, Divide,
        N4, N5, N6, Multiply,
        N1, N2, N3, Minus,
        N0, Dot, Power, Plus,
        Esc, Equals,
        Count,
    };

    struct Button {
        UICalculatorPage *page = nullptr;
        Key key = Key::Count;
    };

    // expression/engine
    static bool evaluate(const std::string &expression, double &value);
    static std::string format_value(double value);

    // editing
    void press(Key key);
    void append(char ch);
    void backspace();
    void clear();
    void calculate();
    void refresh();

    // ui
    void build_ui(int width, int height);
    void place_main_label(const lv_font_t *font);
    static void screen_loaded_cb(lv_event_t *event);
    static void button_cb(lv_event_t *event);
    static void keyboard_cb(lv_event_t *event);
    static void esc_cb(lv_event_t *event);
    static const lv_font_t *face(int px);

    std::string expression_;    // what is being typed (ASCII operators)
    std::string source_;        // the finished expression shown small above the result
    std::string result_;        // the evaluated result, shown when result_shown_
    calculator_layout::Layout layout_; // pinned deck / compact presets, else computed from the layout tokens
    bool result_shown_ = false;
    bool error_ = false;
    bool strip_added_ = false;

    std::unique_ptr<cp0::ui::Page> header_; // computed layouts: the Settings header (title, Esc, status strip)
    lv_obj_t *source_label_ = nullptr;
    lv_obj_t *main_label_ = nullptr;
    std::array<Button, static_cast<size_t>(Key::Count)> buttons_{};
};
