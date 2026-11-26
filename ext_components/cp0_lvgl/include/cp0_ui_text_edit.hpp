/*
 * SPDX-License-Identifier: MIT
 *
 * The text and selection model of cp0::ui::TextField (docs/dev/clipboard.md): a UTF-8 string, a caret and a selection
 * anchor, all as byte offsets that always sit on character boundaries. Header only, no LVGL, so the rules are unit
 * tested on the PC (tests/test_ui_text_edit.cpp): multibyte characters, emoji, the maximum length.
 *
 * The selection is the range between the anchor and the caret (either may come first); anchor == caret means no
 * selection. Moving the caret without `extend` drops the selection, with `extend` (Shift) only moves the caret.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

namespace cp0 {
namespace ui {
namespace text_edit {

inline bool continuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

/* `pos` moved back to the start of its character (pos > size: size) */
inline std::size_t boundary_at_or_before(const std::string &text, std::size_t pos)
{
    if (pos >= text.size()) return text.size();
    while (pos > 0 && continuation(text[pos])) --pos;
    return pos;
}

inline std::size_t prev_boundary(const std::string &text, std::size_t pos)
{
    pos = std::min(pos, text.size());
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && continuation(text[pos])) --pos;
    return pos;
}

inline std::size_t next_boundary(const std::string &text, std::size_t pos)
{
    if (pos >= text.size()) return text.size();
    ++pos;
    while (pos < text.size() && continuation(text[pos])) ++pos;
    return pos;
}

/* how many characters lie before byte offset `bytes` (LVGL's character index of that place) */
inline std::size_t char_index(const std::string &text, std::size_t bytes)
{
    std::size_t n = 0;
    for (std::size_t i = 0; i < bytes && i < text.size(); ++i)
        if (!continuation(text[i])) ++n;
    return n;
}

/* the byte offset of character index `chars` (the text's size when there are fewer characters) */
inline std::size_t byte_offset(const std::string &text, std::size_t chars)
{
    std::size_t i = 0;
    while (i < text.size() && chars > 0) {
        i = next_boundary(text, i);
        --chars;
    }
    return i;
}

/* the longest prefix of text that fits `max` bytes without cutting a character */
inline std::string clip_bytes(const std::string &text, std::size_t max)
{
    if (text.size() <= max) return text;
    return text.substr(0, boundary_at_or_before(text, max));
}

/* What a paste may carry into a field. A single-line field drops CR and LF (as an HTML input does), turns a tab into a
 * space and drops other control characters; a multi-line one keeps LF and tab and drops CR; a Number field keeps only
 * ASCII digits. Bytes that are not valid UTF-8 are dropped, so the field never holds a broken character. */
inline std::string sanitize_paste(const std::string &raw, bool multiline, bool digits_only)
{
    std::string out;
    std::size_t i = 0;
    while (i < raw.size()) {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        std::size_t len = 1;
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) len = 3;
        else if (c >= 0xF0 && c <= 0xF4) len = 4;
        else if (c >= 0x80) { /* a stray continuation byte or an invalid lead */
            ++i;
            continue;
        }
        bool valid = i + len <= raw.size();
        for (std::size_t k = 1; valid && k < len; ++k) valid = continuation(raw[i + k]);
        if (!valid) {
            ++i;
            continue;
        }
        if (len == 1) {
            if (c == '\n' && multiline && !digits_only) out += '\n';
            else if (c == '\t') {
                if (multiline && !digits_only) out += '\t';
                else if (!digits_only) out += ' ';
            } else if (c >= 0x20 && c != 0x7F) {
                if (!digits_only || (c >= '0' && c <= '9')) out += static_cast<char>(c);
            }
        } else if (!digits_only && !(c == 0xC2 && static_cast<unsigned char>(raw[i + 1]) < 0xA0)) { /* no C1 controls */
            out.append(raw, i, len);
        }
        i += len;
    }
    return out;
}

class Buffer {
public:
    explicit Buffer(std::size_t max_bytes = 128) : max_(max_bytes) {}

    const std::string &text() const { return text_; }
    std::size_t caret() const { return caret_; }
    std::size_t anchor() const { return anchor_; }
    std::size_t max_bytes() const { return max_; }
    bool selecting() const { return anchor_ != caret_; }
    std::size_t sel_lo() const { return std::min(anchor_, caret_); }
    std::size_t sel_hi() const { return std::max(anchor_, caret_); }
    std::string selection() const { return text_.substr(sel_lo(), sel_hi() - sel_lo()); }

    /* replaces the text (cut to the maximum), caret at the end, nothing selected */
    void set_text(const std::string &text)
    {
        text_ = clip_bytes(text, max_);
        caret_ = anchor_ = text_.size();
    }

    /* overwrite the bytes in memory (the compiler may not drop the stores) and empty the buffer */
    void wipe()
    {
        volatile char *data = text_.empty() ? nullptr : &text_[0];
        for (std::size_t i = 0; data && i < text_.size(); ++i) data[i] = 0;
        text_.clear();
        caret_ = anchor_ = 0;
    }

    /* caret to a byte offset (snapped back to a character boundary); extend keeps the anchor, else it follows */
    void set_caret(std::size_t pos, bool extend)
    {
        caret_ = boundary_at_or_before(text_, pos);
        if (!extend) anchor_ = caret_;
    }

    /* one character left (-1) or right (+1). Without extend a selection collapses to its near end instead of moving. */
    void move(int delta, bool extend)
    {
        if (!extend && selecting()) {
            set_caret(delta < 0 ? sel_lo() : sel_hi(), false);
            return;
        }
        set_caret(delta < 0 ? prev_boundary(text_, caret_) : next_boundary(text_, caret_), extend);
    }

    void home(bool extend) { set_caret(0, extend); }
    void end(bool extend) { set_caret(text_.size(), extend); }
    void select_all()
    {
        anchor_ = 0;
        caret_ = text_.size();
    }
    void collapse() { anchor_ = caret_; }

    /* typed text (one key's characters): replaces the selection; refused (false, nothing changes) when the result would
     * pass the maximum */
    bool insert(const std::string &s)
    {
        if (s.empty()) return false;
        const std::size_t kept = text_.size() - (sel_hi() - sel_lo());
        if (kept + s.size() > max_) return false;
        replace_selection(s);
        return true;
    }

    /* pasted text: replaces the selection; what does not fit is cut off at a character boundary (not refused).
     * Returns false when nothing changed: nothing to insert, or not even its first character fits (the selection stays). */
    bool paste(const std::string &s)
    {
        const std::size_t kept = text_.size() - (sel_hi() - sel_lo());
        const std::string cut = clip_bytes(s, max_ > kept ? max_ - kept : 0);
        if (cut.empty()) return false;
        replace_selection(cut);
        return true;
    }

    /* deletes the selection (true), or nothing (false) */
    bool erase_selection()
    {
        if (!selecting()) return false;
        replace_selection(std::string());
        return true;
    }

    /* Backspace and Delete: the selection if there is one, else the character before / after the caret */
    bool erase_before()
    {
        if (erase_selection()) return true;
        if (caret_ == 0) return false;
        const std::size_t start = prev_boundary(text_, caret_);
        text_.erase(start, caret_ - start);
        caret_ = anchor_ = start;
        return true;
    }
    bool erase_after()
    {
        if (erase_selection()) return true;
        if (caret_ >= text_.size()) return false;
        text_.erase(caret_, next_boundary(text_, caret_) - caret_);
        anchor_ = caret_;
        return true;
    }

private:
    void replace_selection(const std::string &s)
    {
        const std::size_t lo = sel_lo();
        text_.replace(lo, sel_hi() - lo, s);
        caret_ = anchor_ = lo + s.size();
    }

    std::string text_;
    std::size_t caret_ = 0;
    std::size_t anchor_ = 0;
    std::size_t max_;
};

} // namespace text_edit
} // namespace ui
} // namespace cp0
