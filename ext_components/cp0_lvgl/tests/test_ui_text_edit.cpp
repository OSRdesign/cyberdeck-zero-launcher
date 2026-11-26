// SPDX-License-Identifier: MIT
//
// The text and selection model of the shared TextField (cp0_ui_text_edit.hpp): byte offsets on character boundaries
// (multibyte letters, emoji), the character index / byte offset map, moving and extending the selection, typing and
// pasting over it, the maximum length (typing refuses, a paste is cut at a character boundary), what a paste may carry.
#include "cp0_ui_text_edit.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

using namespace cp0::ui::text_edit;

namespace {
const std::string kE = "\xc3\xa9";           // e acute, 2 bytes
const std::string kEuro = "\xe2\x82\xac";    // euro sign, 3 bytes
const std::string kSmile = "\xf0\x9f\x98\x80"; // grinning face, 4 bytes
} // namespace

int main()
{
    // ---- boundaries and the two maps, on "a" + e acute + euro + smile + "z" (bytes 0 1 3 6 10 11)
    const std::string mixed = "a" + kE + kEuro + kSmile + "z";
    CHECK(mixed.size() == 11);
    CHECK(boundary_at_or_before(mixed, 0) == 0 && boundary_at_or_before(mixed, 1) == 1);
    CHECK(boundary_at_or_before(mixed, 2) == 1 && boundary_at_or_before(mixed, 4) == 3 && boundary_at_or_before(mixed, 5) == 3);
    CHECK(boundary_at_or_before(mixed, 7) == 6 && boundary_at_or_before(mixed, 9) == 6 && boundary_at_or_before(mixed, 10) == 10);
    CHECK(boundary_at_or_before(mixed, 11) == 11 && boundary_at_or_before(mixed, 99) == 11);
    CHECK(prev_boundary(mixed, 11) == 10 && prev_boundary(mixed, 10) == 6 && prev_boundary(mixed, 6) == 3);
    CHECK(prev_boundary(mixed, 3) == 1 && prev_boundary(mixed, 1) == 0 && prev_boundary(mixed, 0) == 0);
    CHECK(next_boundary(mixed, 0) == 1 && next_boundary(mixed, 1) == 3 && next_boundary(mixed, 3) == 6);
    CHECK(next_boundary(mixed, 6) == 10 && next_boundary(mixed, 10) == 11 && next_boundary(mixed, 11) == 11);
    const std::size_t at[] = {0, 1, 3, 6, 10, 11};
    for (std::size_t chars = 0; chars < 6; chars++) {
        CHECK(byte_offset(mixed, chars) == at[chars]);
        CHECK(char_index(mixed, at[chars]) == chars);
    }
    CHECK(byte_offset(mixed, 99) == 11 && char_index(mixed, 99) == 5);
    CHECK(byte_offset("", 3) == 0 && char_index("", 3) == 0);

    // ---- clip_bytes: never cuts a character
    CHECK(clip_bytes(mixed, 11) == mixed && clip_bytes(mixed, 99) == mixed);
    CHECK(clip_bytes(mixed, 10) == "a" + kE + kEuro + kSmile);
    CHECK(clip_bytes(mixed, 9) == "a" + kE + kEuro && clip_bytes(mixed, 6) == "a" + kE + kEuro);
    CHECK(clip_bytes(mixed, 5) == "a" + kE && clip_bytes(mixed, 2) == "a" && clip_bytes(mixed, 0).empty());
    CHECK(clip_bytes(kSmile, 3).empty() && clip_bytes(kSmile, 4) == kSmile);

    // ---- moving the caret, one character at a time, and the selection that follows
    {
        Buffer b(64);
        b.set_text(mixed);
        CHECK(b.caret() == 11 && b.anchor() == 11 && !b.selecting());
        b.move(-1, false);
        CHECK(b.caret() == 10 && !b.selecting());
        b.move(-1, false);
        CHECK(b.caret() == 6); // over the 4-byte emoji in one step
        b.move(-1, true);      // Shift+Left: the euro sign is selected
        CHECK(b.anchor() == 6 && b.caret() == 3 && b.selecting() && b.selection() == kEuro);
        b.move(-1, true);
        CHECK(b.selection() == kE + kEuro && b.sel_lo() == 1 && b.sel_hi() == 6);
        b.move(1, true);       // shrinking from the left end
        CHECK(b.selection() == kEuro && b.caret() == 3);
        b.move(1, true);
        b.move(1, true);       // through the anchor: the other way round, the emoji now
        CHECK(b.anchor() == 6 && b.caret() == 10 && b.selection() == kSmile);
        // plain Left / Right collapse a selection to its near end, without moving further
        b.move(-1, false);
        CHECK(!b.selecting() && b.caret() == 6);
        b.move(-1, true);
        b.move(-1, true);
        CHECK(b.selection() == kE + kEuro);
        b.move(1, false);
        CHECK(!b.selecting() && b.caret() == 6);
        // Home / End, with and without Shift
        b.home(true);
        CHECK(b.anchor() == 6 && b.caret() == 0 && b.selection() == "a" + kE + kEuro);
        b.end(true);
        CHECK(b.anchor() == 6 && b.caret() == 11 && b.selection() == kSmile + "z");
        b.home(false);
        CHECK(!b.selecting() && b.caret() == 0);
        b.move(-1, false);
        CHECK(b.caret() == 0); // at the start: stays
        b.end(false);
        b.move(1, false);
        CHECK(b.caret() == 11);
        b.move(1, true);
        CHECK(!b.selecting()); // at the end: Shift+Right changes nothing
        // select all, and the caret to a byte offset in the middle of a character snaps to its start
        b.select_all();
        CHECK(b.selection() == mixed && b.anchor() == 0 && b.caret() == 11);
        b.set_caret(7, false);
        CHECK(b.caret() == 6 && !b.selecting());
        b.set_caret(2, true);
        CHECK(b.anchor() == 6 && b.caret() == 1);
        b.collapse();
        CHECK(!b.selecting() && b.caret() == 1);
    }

    // ---- typing over a selection, Backspace and Delete
    {
        Buffer b(64);
        b.set_text("hello world");
        b.set_caret(6, false);
        b.set_caret(11, true);
        CHECK(b.selection() == "world");
        CHECK(b.insert("W") && b.text() == "hello W" && b.caret() == 7 && !b.selecting());
        b.select_all();
        CHECK(b.erase_before() && b.text().empty() && b.caret() == 0);
        CHECK(!b.erase_before() && !b.erase_after());
        b.set_text("a" + kE + "b");
        b.set_caret(3, false);
        CHECK(b.erase_before() && b.text() == "ab" && b.caret() == 1); // one whole character
        b.set_text("a" + kSmile + "b");
        b.set_caret(1, false);
        CHECK(b.erase_after() && b.text() == "ab" && b.caret() == 1);
        b.set_caret(0, false);
        b.set_caret(2, true);
        CHECK(b.erase_after() && b.text().empty()); // Delete with a selection deletes just the selection
        b.set_text("abc");
        b.set_caret(1, false);
        b.set_caret(2, true);
        CHECK(b.erase_before() && b.text() == "ac" && b.caret() == 1);
        CHECK(!b.insert(""));
        CHECK(b.insert(kEuro) && b.text() == "a" + kEuro + "c" && b.caret() == 4);
        // the selection can be inside the text, replaced by something longer or shorter
        b.set_text("0123456789");
        b.set_caret(2, false);
        b.set_caret(5, true);
        CHECK(b.insert("ABCDE") && b.text() == "01ABCDE56789" && b.caret() == 7);
    }

    // ---- the maximum length: typing refuses, a paste is cut at a character boundary
    {
        Buffer b(8);
        b.set_text("12345678");
        CHECK(!b.insert("9") && b.text() == "12345678");   // full: a typed character is refused
        b.select_all();
        CHECK(b.insert("9") && b.text() == "9");            // ...but replacing the selection frees the room
        b.set_text("1234567");
        CHECK(!b.insert(kE + kE) && b.text() == "1234567"); // two characters (4 bytes) do not fit in 1 byte
        CHECK(!b.insert(kE) && b.text() == "1234567");
        CHECK(b.insert("8") && b.text() == "12345678");
        // paste: the part that fits, never half a character
        b.set_text("abc");
        CHECK(b.paste("0123456789") && b.text() == "abc01234" && b.caret() == 8);
        b.set_text("abc");
        b.set_caret(1, false);
        CHECK(b.paste("0123456789") && b.text() == "a01234bc" && b.caret() == 6); // in the middle: the text after stays
        b.set_text("abcdefg");
        CHECK(b.paste(kE + kE) == false && b.text() == "abcdefg"); // 1 byte left: not even one e acute fits
        b.set_text("abcdef");
        CHECK(b.paste(kE + kE) && b.text() == "abcdef" + kE && b.caret() == 8); // 2 bytes left: one character
        b.set_text("ab");
        CHECK(b.paste(kSmile + kSmile + kSmile) && b.text() == "ab" + kSmile && b.text().size() == 6); // 6 left: one emoji
        b.set_text("abcdefgh");
        b.set_caret(2, false);
        b.set_caret(6, true);
        CHECK(b.paste(kSmile + kSmile) && b.text() == "ab" + kSmile + "gh"); // selection of 4 bytes frees 4: one emoji
        // a long paste over a selection of everything fills the field exactly
        b.select_all();
        CHECK(b.paste("xxxxxxxxxxxxxxxx") && b.text() == "xxxxxxxx");
        CHECK(!b.paste("") && b.text() == "xxxxxxxx");
        // 4 KB class sizes
        Buffer big(4096);
        big.set_text(std::string(4000, 'q'));
        CHECK(big.paste(std::string(500, 'r')) && big.text().size() == 4096);
        // set_text cuts to the maximum too
        Buffer s(4);
        s.set_text("ab" + kSmile);
        CHECK(s.text() == "ab" && s.caret() == 2);
        s.set_text(kSmile + "a");
        CHECK(s.text() == kSmile);
    }

    // ---- wipe
    {
        Buffer b(16);
        b.set_text("secret");
        b.select_all();
        b.wipe();
        CHECK(b.text().empty() && b.caret() == 0 && !b.selecting());
    }

    // ---- what a paste may carry
    CHECK(sanitize_paste("plain", false, false) == "plain");
    CHECK(sanitize_paste("a\nb\r\nc\r", false, false) == "abc");                // single line: newlines dropped
    CHECK(sanitize_paste("https://x.test/a\n", false, false) == "https://x.test/a");
    CHECK(sanitize_paste("a\tb", false, false) == "a b");                       // a tab becomes a space
    CHECK(sanitize_paste("a\nb\r\nc\td", true, false) == "a\nb\nc\td");         // multi-line keeps LF and tab, drops CR
    CHECK(sanitize_paste(std::string("a\0b\x01\x7f""c", 6), false, false) == "abc"); // other controls dropped
    CHECK(sanitize_paste("a\xc2\x85z", false, false) == "az");                  // a C1 control
    CHECK(sanitize_paste(kE + kEuro + kSmile, false, false) == kE + kEuro + kSmile);
    CHECK(sanitize_paste("a\xff" "b\x80" "c", false, false) == "abc");           // invalid bytes dropped
    CHECK(sanitize_paste("a\xc3", false, false) == "a" && sanitize_paste("\xe2\x82", false, false).empty());
    CHECK(sanitize_paste("\xc3\x28", false, false) == "(");                      // a lead without its continuation
    CHECK(sanitize_paste("port 8080\n", false, true) == "8080");                 // Number: digits only
    CHECK(sanitize_paste("1 234,5\xc3\xa9\n", false, true) == "12345");
    CHECK(sanitize_paste("\n\t -", true, true).empty() && sanitize_paste("", false, false).empty());

    std::puts("test_ui_text_edit: PASS");
    return 0;
}
