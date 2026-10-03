/*
 * Native full-screen touch calculator for the Raspberry Pi Zero 2W + Waveshare 2.8" DPI LCD port.
 */

#define APP_PAGE_IMPLEMENTATION_UNIT
#include "ui_app_calculator.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

#include "keyboard_input.h"
#include "native_ui.hpp"
#include "input_keys.h"

namespace {

constexpr size_t kMaxExpressionLength = 64;
constexpr uint32_t kColorBg = 0x000000;
constexpr uint32_t kColorDigit = 0x2A2A2A;
constexpr uint32_t kColorFunction = 0x4A4A4A;
constexpr uint32_t kColorOperator = 0xE8890C;
constexpr uint32_t kColorEquals = 0x2D9CDB;

// Recursive-descent evaluator: + - * / ^ ( ) and unary minus; division by zero is an error.
class Parser
{
public:
    explicit Parser(const std::string &text) : text_(text) {}

    bool parse(double &value)
    {
        return expression(value) && position_ == text_.size();
    }

private:
    bool expression(double &value)
    {
        if (!term(value)) return false;
        while (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
            const char op = text_[position_++];
            double right = 0;
            if (!term(right)) return false;
            value = op == '+' ? value + right : value - right;
        }
        return true;
    }

    bool term(double &value)
    {
        if (!factor(value)) return false;
        while (position_ < text_.size() && (text_[position_] == '*' || text_[position_] == '/')) {
            const char op = text_[position_++];
            double right = 0;
            if (!factor(right)) return false;
            if (op == '/') {
                if (right == 0.0) return false;
                value /= right;
            } else {
                value *= right;
            }
        }
        return true;
    }

    bool factor(double &value)
    {
        if (position_ < text_.size() && text_[position_] == '-') {
            ++position_;
            if (!factor(value)) return false;
            value = -value;
            return true;
        }
        if (!primary(value)) return false;
        if (position_ < text_.size() && text_[position_] == '^') {
            ++position_;
            double exponent = 0;
            if (!factor(exponent)) return false; // right associative
            value = std::pow(value, exponent);
        }
        return true;
    }

    bool primary(double &value)
    {
        if (position_ < text_.size() && text_[position_] == '(') {
            ++position_;
            if (!expression(value)) return false;
            if (position_ >= text_.size() || text_[position_] != ')') return false;
            ++position_;
            return true;
        }
        const size_t start = position_;
        bool digit = false;
        bool dot = false;
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            if (ch >= '0' && ch <= '9') {
                digit = true;
            } else if (ch == '.') {
                if (dot) return false;
                dot = true;
            } else {
                break;
            }
            ++position_;
        }
        if (!digit) {
            position_ = start;
            return false;
        }
        std::istringstream stream(text_.substr(start, position_ - start));
        stream.imbue(std::locale::classic());
        stream >> value;
        return !stream.fail() && stream.eof();
    }

    const std::string &text_;
    size_t position_ = 0;
};

bool is_operator(char ch)
{
    return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '^';
}

// ASCII operators are kept internally; the glyphs shown are the usual math signs.
std::string pretty(const std::string &text)
{
    std::string out;
    for (char ch : text) {
        switch (ch) {
        case '*': out += "\xC3\x97"; break; // ×
        case '/': out += "\xC3\xB7"; break; // ÷
        default: out += ch; break;
        }
    }
    return out;
}

} // namespace

// DejaVu Sans has the multiplication and division signs that the built-in Montserrat lacks.
const lv_font_t *UICalculatorPage::face(int px)
{
    if (const lv_font_t *font = launcher_fonts().get("DejaVuSans.ttf", px, LV_FREETYPE_FONT_STYLE_NORMAL,
                                                     LV_FREETYPE_FONT_RENDER_MODE_BITMAP))
        return font;
    return px >= 48 ? &lv_font_montserrat_48 : px >= 36 ? &lv_font_montserrat_36 : &lv_font_montserrat_28;
}

bool UICalculatorPage::evaluate(const std::string &expression, double &value)
{
    if (expression.empty()) return false;
    Parser parser(expression);
    return parser.parse(value) && std::isfinite(value);
}

std::string UICalculatorPage::format_value(double value)
{
    if (!std::isfinite(value)) return "error";
    if (value == 0.0) return "0";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::digits10) << std::defaultfloat << value;
    return stream.str();
}

UICalculatorPage::UICalculatorPage() : AppPage()
{
    set_page_title("Calculator");

    lv_display_t *display = root_screen_ ? lv_obj_get_display(root_screen_) : lv_display_get_default();
    const int width = static_cast<int>(display ? lv_display_get_horizontal_resolution(display) : 640);
    const int height = static_cast<int>(display ? lv_display_get_vertical_resolution(display) : 480);

    // Full screen, no standard top bar.
    disable_top_bar();
    if (ui_APP_Container) {
        lv_obj_set_size(ui_APP_Container, width, height);
        lv_obj_set_pos(ui_APP_Container, 0, 0);
    }
    build_ui(width, height);
    if (root_screen_)
        lv_obj_add_event_cb(root_screen_, UICalculatorPage::keyboard_cb,
                            static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
    refresh();
}

UICalculatorPage::~UICalculatorPage()
{
    if (root_screen_)
        lv_obj_remove_event_cb_with_user_data(root_screen_, UICalculatorPage::keyboard_cb, this);
}

void UICalculatorPage::build_ui(int width, int height)
{
    lv_obj_t *parent = ui_APP_Container ? ui_APP_Container : root_screen_;
    if (!parent) return;
    lv_obj_set_style_bg_color(root_screen_, lv_color_hex(kColorBg), 0);

    constexpr int kPad = 10;
    constexpr int kTopBar = 44;
    constexpr int kDisplayH = 104;
    constexpr int kGap = 6;
    constexpr int kRows = 6;
    constexpr int kCols = 4;

    // title + close
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Calculator");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF0B400), 0);
    lv_obj_set_pos(title, 16, 6);
    native_ui::add_status_icons(parent);

    // display: small source line above the big result line
    source_label_ = lv_label_create(parent);
    lv_obj_set_width(source_label_, width - 2 * kPad - 8);
    lv_obj_set_pos(source_label_, kPad, kTopBar + 2);
    lv_label_set_long_mode(source_label_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(source_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(source_label_, face(28), 0);
    lv_obj_set_style_text_color(source_label_, lv_color_hex(0x8A8A8A), 0);
    lv_label_set_text(source_label_, "");

    main_label_ = lv_label_create(parent);
    lv_obj_set_width(main_label_, width - 2 * kPad - 8);
    lv_obj_set_pos(main_label_, kPad, kTopBar + 38);
    lv_label_set_long_mode(main_label_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(main_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(main_label_, face(48), 0);
    lv_obj_set_style_text_color(main_label_, lv_color_hex(0xFFFFFF), 0);

    // keypad
    struct Cell {
        Key key;
        const char *text;
        uint32_t color;
        int col, row, span;
    };
    static const Cell cells[] = {
        {Key::Clear, "C", kColorFunction, 0, 0, 1},   {Key::Backspace, LV_SYMBOL_BACKSPACE, kColorFunction, 1, 0, 1},
        {Key::LParen, "(", kColorFunction, 2, 0, 1},  {Key::RParen, ")", kColorFunction, 3, 0, 1},
        {Key::N7, "7", kColorDigit, 0, 1, 1},         {Key::N8, "8", kColorDigit, 1, 1, 1},
        {Key::N9, "9", kColorDigit, 2, 1, 1},         {Key::Divide, "\xC3\xB7", kColorOperator, 3, 1, 1},
        {Key::N4, "4", kColorDigit, 0, 2, 1},         {Key::N5, "5", kColorDigit, 1, 2, 1},
        {Key::N6, "6", kColorDigit, 2, 2, 1},         {Key::Multiply, "\xC3\x97", kColorOperator, 3, 2, 1},
        {Key::N1, "1", kColorDigit, 0, 3, 1},         {Key::N2, "2", kColorDigit, 1, 3, 1},
        {Key::N3, "3", kColorDigit, 2, 3, 1},         {Key::Minus, "-", kColorOperator, 3, 3, 1},
        {Key::N0, "0", kColorDigit, 0, 4, 1},         {Key::Dot, ".", kColorDigit, 1, 4, 1},
        {Key::Power, "^", kColorOperator, 2, 4, 1},   {Key::Plus, "+", kColorOperator, 3, 4, 1},
        {Key::Esc, "Esc", kColorFunction, 0, 5, 1},   {Key::Equals, "=", kColorEquals, 1, 5, 3},
    };

    const int pad_top = kTopBar + kDisplayH;
    const int cell_w = (width - 2 * kPad - (kCols - 1) * kGap) / kCols;
    const int cell_h = (height - pad_top - kPad - (kRows - 1) * kGap) / kRows;
    for (const Cell &cell : cells) {
        Button &slot = buttons_[static_cast<size_t>(cell.key)];
        slot.page = this;
        slot.key = cell.key;
        lv_obj_t *button = lv_button_create(parent);
        const int w = cell.span * cell_w + (cell.span - 1) * kGap;
        lv_obj_set_size(button, w, cell_h);
        lv_obj_set_pos(button, kPad + cell.col * (cell_w + kGap), pad_top + cell.row * (cell_h + kGap));
        lv_obj_set_style_bg_color(button, lv_color_hex(cell.color), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x707070), LV_STATE_PRESSED);
        if (cell.key == Key::Esc)
            // behaves like the keyboard's Esc: hold it for 3 s to return home
            lv_obj_add_event_cb(button, UICalculatorPage::esc_cb, LV_EVENT_ALL, &slot);
        else
            lv_obj_add_event_cb(button, UICalculatorPage::button_cb, LV_EVENT_CLICKED, &slot);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, cell.text);
        const bool sign = cell.key == Key::Multiply || cell.key == Key::Divide;
        lv_obj_set_style_text_font(label, sign ? face(32) : cell.key == Key::Esc ? &lv_font_montserrat_28
                                                                               : &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
    }
}

void UICalculatorPage::button_cb(lv_event_t *event)
{
    auto *button = static_cast<Button *>(lv_event_get_user_data(event));
    if (button && button->page) button->page->press(button->key);
}

void UICalculatorPage::esc_cb(lv_event_t *event)
{
    // Inject the real Esc key so the launcher's hold-to-go-home hint and timer apply as usual.
    static bool down = false;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        down = true;
        cp0_keyboard_inject(KEY_ESC, KBD_KEY_PRESSED, 0);
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_DELETE:
        // DELETE matters: holding Esc returns home and destroys this page while the finger is
        // still down, so the button never sees its release. Without this the launcher believes
        // Esc is stuck down and its watchdog restarts the launcher the next time an app opens.
        if (down) {
            down = false;
            cp0_keyboard_inject(KEY_ESC, KBD_KEY_RELEASED, 0);
        }
        break;
    default:
        break;
    }
}

void UICalculatorPage::keyboard_cb(lv_event_t *event)
{
    auto *self = static_cast<UICalculatorPage *>(lv_event_get_user_data(event));
    auto *key = static_cast<key_item *>(lv_event_get_param(event));
    if (!self || !key || key->key_state == 0) return; // act on press / repeat, not release

    switch (key->key_code) {
    case KEY_ENTER:
    case KEY_KPENTER:
        if (key->key_state == 1) self->press(Key::Equals);
        return;
    case KEY_BACKSPACE:
        self->press(Key::Backspace);
        return;
    case KEY_DELETE:
        if (key->key_state == 1) self->press(Key::Clear);
        return;
    default:
        break;
    }
    if (key->key_state != 1 || key->utf8[0] == '\0' || key->utf8[1] != '\0') return;
    const char ch = key->utf8[0];
    if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '(' || ch == ')' || is_operator(ch)) {
        self->append(ch);
        self->refresh(); // typed characters must show immediately, like the on-screen keys
    } else if (ch == '=') {
        self->press(Key::Equals);
    } else if (ch == 'x' || ch == 'X') {
        self->append('*');
        self->refresh();
    }
}

void UICalculatorPage::press(Key key)
{
    switch (key) {
    case Key::Clear: clear(); break;
    case Key::Backspace: backspace(); break;
    case Key::LParen: append('('); break;
    case Key::RParen: append(')'); break;
    case Key::Divide: append('/'); break;
    case Key::Multiply: append('*'); break;
    case Key::Minus: append('-'); break;
    case Key::Plus: append('+'); break;
    case Key::Power: append('^'); break;
    case Key::Dot: append('.'); break;
    case Key::Equals: calculate(); break;
    case Key::Esc: break; // handled by esc_cb (press/release), not as a click
    case Key::N0: append('0'); break;
    case Key::N1: append('1'); break;
    case Key::N2: append('2'); break;
    case Key::N3: append('3'); break;
    case Key::N4: append('4'); break;
    case Key::N5: append('5'); break;
    case Key::N6: append('6'); break;
    case Key::N7: append('7'); break;
    case Key::N8: append('8'); break;
    case Key::N9: append('9'); break;
    case Key::Count: break;
    }
    refresh();
}

void UICalculatorPage::append(char ch)
{
    if (result_shown_) {
        // After an evaluation, an operator continues from the result; anything else starts over.
        if (!error_ && (is_operator(ch))) expression_ = result_;
        else expression_.clear();
        source_.clear();
        result_shown_ = false;
        error_ = false;
    }
    if (expression_.size() >= kMaxExpressionLength) return;
    expression_.push_back(ch);
}

void UICalculatorPage::backspace()
{
    if (result_shown_) {
        expression_ = error_ ? std::string() : result_;
        source_.clear();
        result_shown_ = false;
        error_ = false;
    }
    if (!expression_.empty()) expression_.pop_back();
}

void UICalculatorPage::clear()
{
    expression_.clear();
    source_.clear();
    result_.clear();
    result_shown_ = false;
    error_ = false;
}

void UICalculatorPage::calculate()
{
    if (result_shown_ || expression_.empty()) return;
    double value = 0;
    source_ = expression_;
    if (evaluate(expression_, value)) {
        result_ = format_value(value);
        error_ = false;
    } else {
        result_ = "error";
        error_ = true;
    }
    result_shown_ = true;
}

void UICalculatorPage::refresh()
{
    if (!main_label_ || !source_label_) return;
    const std::string main_text = result_shown_ ? result_ : (expression_.empty() ? "0" : pretty(expression_));
    // long inputs get a smaller font so they still fit
    const lv_font_t *font = face(main_text.size() <= 12 ? 48 : main_text.size() <= 18 ? 36 : 28);
    lv_obj_set_style_text_font(main_label_, font, 0);
    lv_obj_set_style_text_color(main_label_, lv_color_hex(error_ ? 0xFF5F56 : 0xFFFFFF), 0);
    lv_label_set_text(main_label_, main_text.c_str());
    lv_label_set_text(source_label_, source_.empty() ? "" : (pretty(source_) + " =").c_str());
}
