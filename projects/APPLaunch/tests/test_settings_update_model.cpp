// SPDX-License-Identifier: MIT
//
// Settings > Updates (report 047 P1, section 11): semantic version order, the GitHub release list (real-shaped
// answers: the real list of 2026-10-31, a newer release with a pre-release and a draft on top, a release without an
// amd64 bundle, the 403 rate-limit body), the headers and the outcome map, the asset choice, the notes converter on
// every release-notes file of docs/dev plus edge cases (CRLF, comments, tables, nested bullets, the end marker), the
// layout with a hanging indent, What's new, and the state file.
//
//   test_settings_update_model FIXTURE_DIR DOCS_DEV_DIR

#include "settings_update_model.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace launcher_update;

namespace {

std::string g_fixtures;
std::string g_docs;

std::string load(const std::string &path)
{
    std::string text;
    if (!read_file(path, text)) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::abort();
    }
    return text;
}

std::vector<Release> releases_of(const char *name)
{
    std::vector<Release> releases;
    std::string error;
    const bool ok = parse_releases(load(g_fixtures + "/" + name), releases, &error);
    if (!ok) std::fprintf(stderr, "%s: %s\n", name, error.c_str());
    assert(ok);
    return releases;
}

Build build(const char *version, const char *channel = "release", const char *arch = "arm64")
{
    return Build{version, channel, arch};
}

bool contains(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

void test_versions()
{
    const char *ascending[] = {"0.0.0-dev",       "0.0.1",         "0.8.9-pda.1",   "0.8.9",
                               "0.8.10",          "0.8.13-alpha",  "0.8.13-alpha.1", "0.8.13-alpha.beta",
                               "0.8.13-beta",     "0.8.13-beta.2", "0.8.13-beta.11", "0.8.13-rc.1",
                               "0.8.13",          "0.9.0",         "1.0.0",         "10.0.0"};
    const std::size_t n = sizeof(ascending) / sizeof(ascending[0]);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            const int c = compare_versions(ascending[i], ascending[j]);
            assert((i < j && c < 0) || (i == j && c == 0) || (i > j && c > 0));
        }
    assert(compare_versions("v0.8.12", "0.8.12") == 0);
    assert(compare_versions("0.8.12+build.5", "0.8.12") == 0);
    Version v;
    assert(parse_version("v0.8.13-rc.1", v) && v.major == 0 && v.minor == 8 && v.patch == 13 && v.pre.size() == 2);
    for (const char *bad : {"", "0.8", "0.8.x", "0.8.12-", "0.8.12-a..b", "v", "0.8.12.1", "x.y.z", "-0.8.1"})
        assert(!parse_version(bad, v));
    assert(compare_versions("garbage", "0.0.1") < 0);
    assert(compare_versions("0.0.1", "garbage") > 0);
    assert(version_of_tag("v0.8.12") == "0.8.12" && version_of_tag("0.8.12") == "0.8.12");
}

void test_parse_real()
{
    const std::vector<Release> releases = releases_of("releases-real.json");
    assert(releases.size() == 4);
    assert(releases[0].tag == "v0.8.12" && releases[0].version == "0.8.12");
    assert(releases[3].tag == "v0.8.9");
    assert(!releases[0].draft && !releases[0].prerelease);
    assert(releases[0].published_at == "2026-10-30T22:02:49Z" && releases[0].date() == "2026-10-30");
    assert(releases[0].assets.size() == 4);
    assert(contains(releases[0].body, "\r\n")); /* GitHub serves the notes with CRLF */
    const Asset *arm = find_asset(releases[0], "arm64");
    assert(arm && arm->name == "cyberdeckzero-launcher-0.8.12-arm64.tar.gz" && arm->size == 57332103);
    assert(arm->digest == "sha256:ffd7c4972546fe32fbd480e39054dcd772f53b5cbd30e5fd8990e6700b133202");
    assert(contains(arm->url, "/releases/download/v0.8.12/cyberdeckzero-launcher-0.8.12-arm64.tar.gz"));
    const Asset *amd = find_asset(releases[0], "amd64");
    assert(amd && amd->size == 57311828);
    assert(!find_asset(releases[0], "riscv64"));
    assert(asset_name("0.8.13", "amd64") == "cyberdeckzero-launcher-0.8.13-amd64.tar.gz");

    // up to date / one behind
    Offer offer = choose(releases, build("0.8.12"));
    assert(offer.newer.empty() && offer.available.empty() && offer.latest == "0.8.12" && offer.missing_asset.empty());
    offer = choose(releases, build("0.8.11"));
    assert(offer.newer.size() == 1 && offer.available == "0.8.12");
}

void test_parse_broken()
{
    std::vector<Release> releases;
    std::string error;
    assert(!parse_releases(load(g_fixtures + "/rate-limit.json"), releases, &error));
    assert(contains(error, "API rate limit exceeded"));
    assert(!parse_releases("<html>captive portal</html>", releases, &error) && error == "not JSON");
    assert(!parse_releases("{}", releases, &error) && error == "not a release list");
    assert(parse_releases("[]", releases) && releases.empty());
    // unknown fields, nulls, entries without a tag, odd types: ignored
    assert(parse_releases(R"([{"tag_name":null},{"tag_name":"v1.0.0","body":null,"draft":"no","assets":{},
        "extra":[1,2,{"x":true}],"published_at":null},5,"x"])",
                          releases));
    assert(releases.size() == 1 && releases[0].version == "1.0.0" && releases[0].body.empty() &&
           !releases[0].draft && releases[0].assets.empty() && releases[0].date().empty());
}

void test_choice()
{
    // a newer release with a pre-release and a draft above it: both ignored
    const std::vector<Release> releases = releases_of("releases-0.8.13.json");
    assert(releases.size() == 7);
    assert(releases[0].draft && releases[1].prerelease);
    assert(!eligible(releases[0]) && !eligible(releases[1]) && eligible(releases[2]));
    Offer offer = choose(releases, build("0.8.12", "release", "amd64"));
    assert(offer.latest == "0.8.13" && offer.available == "0.8.13" && offer.newer.size() == 1);
    assert(offer.newer[0].date() == "2026-11-04");
    // three versions behind: newest first, the notes of each
    offer = choose(releases, build("0.8.10"));
    assert(offer.newer.size() == 3 && offer.newer[0].version == "0.8.13" && offer.newer[1].version == "0.8.12" &&
           offer.newer[2].version == "0.8.11" && offer.available == "0.8.13");
    // a release tagged with a suffix but not marked pre-release: not eligible either
    Release odd;
    odd.tag = "v0.9.0-test.1";
    odd.version = "0.9.0-test.1";
    assert(!eligible(odd));

    // no amd64 bundle in 0.8.13
    const std::vector<Release> arm_only = releases_of("releases-0.8.13-arm64-only.json");
    offer = choose(arm_only, build("0.8.12", "release", "amd64"));
    assert(offer.available.empty() && offer.missing_asset == "0.8.13" && offer.newer.size() == 1);
    offer = choose(arm_only, build("0.8.11", "release", "amd64"));
    assert(offer.available == "0.8.12" && offer.missing_asset == "0.8.13");
    offer = choose(arm_only, build("0.8.12", "release", "arm64"));
    assert(offer.available == "0.8.13" && offer.missing_asset.empty());

    // development builds (D4): the latest release and its notes, never an offer
    assert(development_build(build("0.0.0-dev", "development")));
    assert(development_build(build("0.8.12", "development")));
    assert(development_build(build("0.8.12", "prerelease")));
    assert(development_build(build("0.8.9-pda.1", "release")));
    assert(development_build(build("0.0.0", "release")));
    assert(development_build(build("garbage", "release")));
    assert(!development_build(build("0.8.12", "release")));
    offer = choose(releases, build("0.0.0-dev", "development"));
    assert(offer.newer.size() == 1 && offer.newer[0].version == "0.8.13" && offer.available.empty() &&
           offer.latest == "0.8.13");
    assert(choose({}, build("0.8.12")).latest.empty());
}

void test_headers_and_outcomes()
{
    Headers h = parse_headers(load(g_fixtures + "/ok.headers"));
    assert(h.etag == "W/\"3f1c0a1e5b0d2f4c9a8e7d6c5b4a39281706f5e4d3c2b1a09f8e7d6c5b4a3928\"");
    assert(h.rate_remaining == 57 && h.rate_reset == 1793480400LL);
    h = parse_headers(load(g_fixtures + "/rate-limit.headers"));
    assert(h.rate_remaining == 0 && h.rate_reset == 1793480400LL && h.etag.empty());
    // after a redirect only the last response counts
    h = parse_headers("HTTP/1.1 302 Found\r\nLocation: /x\r\nETag: \"a\"\r\nX-RateLimit-Remaining: 0\r\n\r\n"
                      "HTTP/2 200\r\nEtag: \"b\"\r\n\r\n");
    assert(h.etag == "\"b\"" && h.rate_remaining == -1 && h.rate_reset == 0);

    Headers limited;
    limited.rate_remaining = 0;
    Headers fine;
    fine.rate_remaining = 12;
    assert(classify(0, 200, fine) == Outcome::Ok);
    assert(classify(0, 304, fine) == Outcome::Ok);
    assert(classify(0, 403, limited) == Outcome::RateLimited);
    assert(classify(22, 403, limited) == Outcome::RateLimited);
    assert(classify(0, 403, fine) == Outcome::Http);
    assert(classify(0, 429, fine) == Outcome::RateLimited);
    assert(classify(0, 500, fine) == Outcome::Http);
    assert(classify(0, 404, fine) == Outcome::Http);
    assert(classify(0, 0, fine) == Outcome::Failed);
    assert(classify(6, 0, fine) == Outcome::Dns);
    assert(classify(7, 0, fine) == Outcome::Connect);
    assert(classify(28, 0, fine) == Outcome::Timeout);
    assert(classify(-2, 0, fine) == Outcome::Timeout);
    for (int rc : {35, 51, 58, 60, 77, 83}) assert(classify(rc, 0, fine) == Outcome::Certificate);
    assert(classify(127, 0, fine) == Outcome::NoCurl);
    assert(classify(3, 0, fine) == Outcome::Failed);

    for (Outcome o : {Outcome::None, Outcome::Ok, Outcome::Offline, Outcome::NoClock, Outcome::Dns, Outcome::Connect,
                      Outcome::Timeout, Outcome::Certificate, Outcome::RateLimited, Outcome::Http, Outcome::BadAnswer,
                      Outcome::NoCurl, Outcome::Failed}) {
        assert(outcome_from_key(outcome_key(o)) == o);
        assert(outcome_text(o, 500, "").empty() == !failed(o));
    }
    assert(outcome_from_key("whatever") == Outcome::None);
    assert(outcome_text(Outcome::RateLimited, 403, "21:15") == "GitHub's limit reached: try again after 21:15");
    assert(outcome_text(Outcome::RateLimited, 403, "") == "GitHub's limit reached: try again in an hour");
    assert(outcome_text(Outcome::Http, 500, "") == "GitHub answered HTTP 500");
    assert(outcome_text(Outcome::Offline, 0, "") == "No network: the last result is shown");
}

// ------------------------------------------------------------------ notes

bool clean(const NoteBlock &block)
{
    const std::string &t = block.text;
    if (contains(t, "\r")) return false;
    if (block.kind == BlockKind::Code) return true;
    return !contains(t, "**") && !contains(t, "`") && !contains(t, "<!--") && !contains(t, "-->") &&
           !contains(t, "](") && !contains(t, "![") && !(t.size() && t[0] == '|') && !t.empty();
}

void test_notes_all_files()
{
    DIR *dir = opendir(g_docs.c_str());
    assert(dir);
    std::vector<std::string> names;
    while (dirent *entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("release-notes-", 0) == 0 && name.size() > 3 && name.compare(name.size() - 3, 3, ".md") == 0)
            names.push_back(name);
    }
    closedir(dir);
    assert(names.size() >= 13); /* 0.8.0 to 0.8.12 at the time of report 047 */
    int nested_total = 0;
    for (const std::string &name : names) {
        const std::string markdown = load(g_docs + "/" + name);
        // the same file as GitHub serves it (CRLF) gives the same blocks
        std::string crlf;
        for (char c : markdown) {
            if (c == '\r') continue;
            if (c == '\n') crlf += '\r';
            crlf += c;
        }
        const std::vector<NoteBlock> blocks = notes_blocks(markdown);
        assert(blocks == notes_blocks(crlf));
        assert(!blocks.empty());
        assert(blocks[0].kind == BlockKind::Paragraph && blocks[0].text.rfind("New since v", 0) == 0);
        int bullets = 0;
        for (const NoteBlock &block : blocks) {
            if (!clean(block)) std::fprintf(stderr, "%s: not plain: [%s]\n", name.c_str(), block.text.c_str());
            assert(clean(block));
            assert(!contains(block.text, "Upgrading from")); /* after **Install**: dropped (D10) */
            assert(!contains(block.text, "tar xzf"));
            bullets += block.kind == BlockKind::Bullet;
            nested_total += block.kind == BlockKind::Bullet && block.level == 1;
        }
        assert(bullets >= 2);
        const bool pictures = name == "release-notes-0.8.8.md" || name == "release-notes-0.8.9.md";
        bool picture_note = false;
        for (const NoteBlock &block : blocks)
            picture_note = picture_note || (block.kind == BlockKind::Note && block.text == "(pictures in the online notes)");
        assert(picture_note == pictures);
        // the layout keeps every word of every block
        const std::string text = blocks_text(blocks, [](const std::string &s) { return 9 * static_cast<int>(s.size()); },
                                             300);
        assert(!text.empty() && !contains(text, "\n\n\n"));
    }
    assert(nested_total == 6); /* the 0.8.0 notes' nested bullets */

    // one file in full detail: 0.8.12
    const std::vector<NoteBlock> b = notes_blocks(load(g_docs + "/release-notes-0.8.12.md"));
    assert(b.size() == 7);
    assert(b[0].text == "New since v0.8.11 (pull requests #111, #112 and #113: grab rotation, a reorderable home grid, "
                        "the SSH indicator; docs #110)");
    assert(b[1].kind == BlockKind::Bullet && b[1].level == 0);
    assert(b[1].text == "Home grid: arrange the apps. Hold a tile for 3 seconds (a progress bar shows the hold), or "
                        "press Ctrl+O on a selected tile, to enter move mode. Arrow keys move the tile, a tap on another "
                        "position places it, Enter drops it and Esc cancels. The order is saved. Settings > Launcher > "
                        "Reset app order puts the default order back.");
    assert(b[3].text.rfind("Apps need a rebuild to show the indicator.", 0) == 0);
    assert(b[6].text == "Docs. docs/PDA-PROFILE.md has a section \"Apps with the band\" with renders of the handwriting "
                        "band.");
}

void test_notes_edges()
{
    // CRLF, comments (inline, multi-line), the end marker
    std::vector<NoteBlock> b = notes_blocks("**New** since\r\nv1 <!-- hidden -->text\r\n\r\n<!-- a\r\nmulti-line\r\n"
                                            "comment -->\r\n- one\r\n<!-- launcher-notes-end -->\r\n- after the marker\r\n");
    assert(b.size() == 2);
    assert(b[0] == (NoteBlock{BlockKind::Paragraph, 0, "New since v1 text"}));
    assert(b[1] == (NoteBlock{BlockKind::Bullet, 0, "one"}));

    // the marker wins over **Install**; without it **Install** ends the part
    assert(notes_part("a\n**Install** x\nb\n<!-- launcher-notes-end -->\nc") == "a\n**Install** x\nb\n");
    assert(notes_part("a\r\n**Install** (x)\r\nb") == "a\n");
    assert(notes_part("a\n **Install**\nb") == "a\n **Install**\nb"); /* only at the start of a line */
    assert(notes_part("no end") == "no end");

    // nested bullets, continuation lines, links, code spans, a heading, a rule, a table without pictures, a fence
    b = notes_blocks("## In pictures\n"
                     "- **Top** level with a [link](https://example.org/x) and `code`\n"
                     "  continued here\n"
                     "  - nested one\n"
                     "    continued\n"
                     "  - nested two\n"
                     "- back to top\n"
                     "\n"
                     "---\n"
                     "| a | b |\n|---|---|\n| 1 | 2 |\n"
                     "\n"
                     "| ![pic](docs/x.png) |\n"
                     "Text right after a table.\n"
                     "```\n"
                     "sudo ./install.sh --board <name>\n"
                     "   indented   \n"
                     "```\n"
                     "![lonely](p.png)\n"
                     "\n"
                     "<!-- never closed\nhidden\n");
    const std::vector<NoteBlock> expected = {
        {BlockKind::Heading, 0, "In pictures"},
        {BlockKind::Bullet, 0, "Top level with a link and code continued here"},
        {BlockKind::Bullet, 1, "nested one continued"},
        {BlockKind::Bullet, 1, "nested two"},
        {BlockKind::Bullet, 0, "back to top"},
        {BlockKind::Note, 0, "(a table in the online notes)"},
        {BlockKind::Note, 0, "(pictures in the online notes)"},
        {BlockKind::Paragraph, 0, "Text right after a table."},
        {BlockKind::Code, 0, "sudo ./install.sh --board <name>"},
        {BlockKind::Code, 0, "   indented"},
        {BlockKind::Note, 0, "(pictures in the online notes)"},
    };
    if (!(b == expected))
        for (const NoteBlock &block : b)
            std::fprintf(stderr, "[%d %d %s]\n", static_cast<int>(block.kind), block.level, block.text.c_str());
    assert(b == expected);

    assert(inline_plain("**a** `b` [c](d) ![e](f) g") == "a b c g");
    assert(inline_plain("[not a link] (x) <placeholder> #111") == "[not a link] (x) <placeholder> #111");
    assert(notes_blocks("").empty());
    assert(notes_blocks("\r\n\r\n").empty());
}

int width10(const std::string &text)
{
    return 10 * static_cast<int>(text.size());
}

void test_layout()
{
    const std::vector<NoteBlock> blocks = {
        {BlockKind::Heading, 0, "0.8.13 - 2026-11-04"},
        {BlockKind::Paragraph, 0, "New since v0.8.12"},
        {BlockKind::Bullet, 0, "Updates in Settings. The launcher checks GitHub"},
        {BlockKind::Bullet, 1, "nested words wrap under the text"},
        {BlockKind::Code, 0, "sudo ./install.sh"},
        {BlockKind::Note, 0, "0.8.12 is installed."},
    };
    // 10 px a character, 200 px: 20 characters a line
    const std::string text = blocks_text(blocks, width10, 200);
    assert(text == "0.8.13 - 2026-11-04\n"
                   "New since v0.8.12\n"
                   "\n"
                   "- Updates in\n"
                   "  Settings. The\n"
                   "  launcher checks\n"
                   "  GitHub\n"
                   "  - nested words\n"
                   "    wrap under the\n"
                   "    text\n"
                   "\n"
                   "sudo ./install.sh\n"
                   "\n"
                   "0.8.12 is installed.");
    // no measure: no hard wrap
    assert(blocks_text(blocks, nullptr, 0).find("- Updates in Settings. The launcher checks GitHub\n") !=
           std::string::npos);
    // a word longer than the line stays whole
    assert(blocks_text({{BlockKind::Bullet, 0, "abcdefghijklmnopqrstuvwxyz short"}}, width10, 100) ==
           "- abcdefghijklmnopqrstuvwxyz\n  short");
    // headings get a blank line before (not the first)
    assert(blocks_text({{BlockKind::Paragraph, 0, "a"}, {BlockKind::Heading, 0, "h"}, {BlockKind::Bullet, 0, "b"}},
                       nullptr, 0) == "a\n\nh\n- b");
}

void test_whats_new()
{
    const std::vector<Release> releases = releases_of("releases-0.8.13.json");
    const Offer offer = choose(releases, build("0.8.10"));
    State state;
    std::vector<NotesSource> sources = notes_sources(releases, offer, state, build("0.8.10"), "");
    assert(sources.size() == 3 && sources[0].version == "0.8.13" && sources[0].date == "2026-11-04");
    std::vector<NoteBlock> blocks = whats_new_blocks(sources, "0.8.10");
    assert(blocks.front() == (NoteBlock{BlockKind::Heading, 0, "0.8.13 - 2026-11-04"}));
    int headings = 0;
    for (const NoteBlock &block : blocks) headings += block.kind == BlockKind::Heading;
    assert(headings == 3);
    assert(blocks.back() == (NoteBlock{BlockKind::Note, 0, "0.8.10 is installed."}));
    // the 0.8.13 fixture's notes stop at its marker: no Install block
    for (const NoteBlock &block : blocks) assert(!contains(block.text, "sudo ./install.sh"));
    // capped by count, then by size: where the older notes are
    blocks = whats_new_blocks(sources, "0.8.10", 2);
    assert(blocks.back().text == "Older notes: github.com/OSRdesign/cyberdeck-zero-launcher/releases");
    blocks = whats_new_blocks(sources, "0.8.10", 5, 1000);
    headings = 0;
    for (const NoteBlock &block : blocks) headings += block.kind == BlockKind::Heading;
    assert(headings == 1 && blocks.back().text.rfind("Older notes", 0) == 0);
    // nothing at all
    blocks = whats_new_blocks({}, "0.8.12");
    assert(blocks.size() == 1 && blocks[0].text == "No release notes for 0.8.12 on this board yet.");

    // up to date after an update from 0.8.10: the versions after it up to the running one
    const std::vector<Release> real = releases_of("releases-real.json");
    state.updated_from = "0.8.10";
    sources = notes_sources(real, choose(real, build("0.8.12")), state, build("0.8.12"), "shipped");
    assert(sources.size() == 2 && sources[0].version == "0.8.12" && sources[1].version == "0.8.11");
    // the list lacks the running version: its shipped notes
    sources = notes_sources({}, Offer(), state, build("0.8.12"), "**New since** x");
    assert(sources.size() == 1 && sources[0].version == "0.8.12" && sources[0].date.empty() &&
           sources[0].markdown == "**New since** x");
    // no update seen: the running version's release
    state.updated_from.clear();
    sources = notes_sources(real, choose(real, build("0.8.12")), state, build("0.8.12"), "shipped");
    assert(sources.size() == 1 && sources[0].version == "0.8.12" && sources[0].date == "2026-10-30");
    assert(notes_sources({}, Offer(), state, build("0.8.12"), "  \n").empty());
    // a development build: the latest release
    sources = notes_sources(releases, choose(releases, build("0.0.0-dev", "development")), state,
                            build("0.0.0-dev", "development"), "");
    assert(sources.size() == 1 && sources[0].version == "0.8.13");
}

void test_state()
{
    State state;
    state.last_check = 1793476800;
    state.last_success = 1793476700;
    state.etag = "W/\"abc\"";
    state.latest = "0.8.13";
    state.available = "0.8.13";
    state.result = "ok";
    state.rate_reset = 5;
    state.last_shown = "0.8.12";
    state.updated_from = "0.8.11";
    state.notified = "0.8.13";
    assert(parse_state(format_state(state)) == state);
    // newlines in a value cannot break the file
    State odd;
    odd.etag = "a\nlatest=9.9.9";
    assert(parse_state(format_state(odd)).latest.empty());
    // unknown keys, garbage, CRLF
    const State parsed = parse_state("junk\r\n=x\r\nlast_check=12\r\nfuture=1\r\nlast_check_x=3\r\nrate_reset=abc\r\n");
    assert(parsed.last_check == 12 && parsed.rate_reset == 0);

    char dir_template[] = "/tmp/update-state-XXXXXX";
    const char *dir = mkdtemp(dir_template);
    assert(dir);
    const std::string path = std::string(dir) + "/a/b/update.state";
    State read;
    assert(!read_state(path, read));
    assert(write_state(path, state));
    assert(read_state(path, read) && read == state);
    state.latest = "0.8.14";
    assert(write_state(path, state) && read_state(path, read) && read.latest == "0.8.14");
    std::string text;
    assert(read_file(path, text) && text.rfind("# Settings > Updates", 0) == 0);
    std::remove(path.c_str());
    rmdir((std::string(dir) + "/a/b").c_str());
    rmdir((std::string(dir) + "/a").c_str());
    rmdir(dir);
    assert(!write_state("", state));

    // record_check: an answer, then a failure keeps what is known, a rate limit keeps its reset
    State s;
    Offer offer;
    offer.latest = "0.8.13";
    offer.available = "0.8.13";
    Headers headers;
    headers.etag = "\"e1\"";
    record_check(s, Outcome::Ok, &offer, headers, 1000);
    assert(s.last_check == 1000 && s.last_success == 1000 && s.etag == "\"e1\"" && s.available == "0.8.13" &&
           s.result == "ok");
    headers = Headers();
    headers.rate_reset = 4600;
    record_check(s, Outcome::RateLimited, nullptr, headers, 2000);
    assert(s.last_check == 2000 && s.last_success == 1000 && s.etag == "\"e1\"" && s.available == "0.8.13" &&
           s.result == "rate-limited" && s.rate_reset == 4600);
    record_check(s, Outcome::Ok, &offer, Headers(), 3000); /* a 304 without an ETag keeps the old one */
    assert(s.etag == "\"e1\"" && s.rate_reset == 0);
    record_check(s, Outcome::Offline, nullptr, Headers(), 4000); /* GitHub not asked: not a check */
    assert(s.last_check == 3000 && s.result == "offline" && s.available == "0.8.13");
    record_check(s, Outcome::NoClock, nullptr, Headers(), 5000);
    assert(s.last_check == 3000 && s.result == "no-clock");
}

void test_shown()
{
    // the root row
    State state;
    assert(root_value(state, build("0.8.12")).text.empty());
    state.result = "ok";
    RowValue value = root_value(state, build("0.8.12"));
    assert(value.text == "Up to date" && value.tone == Tone::Normal);
    state.available = "0.8.13";
    value = root_value(state, build("0.8.12"));
    assert(value.text == "0.8.13 available" && value.tone == Tone::Update);
    assert(root_value(state, build("0.8.13")).text == "Up to date"); /* after the update */
    state.result = "offline";
    assert(root_value(state, build("0.8.12")).text == "0.8.13 available"); /* still known */
    value = root_value(state, build("0.8.13"));
    assert(value.text == "Check failed" && value.tone == Tone::Muted);
    value = root_value(state, build("0.0.0-dev", "development"));
    assert(value.text == "Dev build" && value.tone == Tone::Muted);

    // times (TZ=UTC): 2026-10-31 10:42:00 is 1793443320
    const long long now = 1793443320;
    assert(last_checked_text(0, now) == "never");
    assert(last_checked_text(now, now) == "today 10:42");
    assert(last_checked_text(now - 10 * 3600 - 42 * 60, now) == "today 00:00");
    assert(last_checked_text(now - 10 * 3600 - 43 * 60, now) == "yesterday 23:59");
    assert(last_checked_text(now - 3 * kDaySeconds, now) == "3 days ago");
    assert(last_checked_text(now + 2 * kDaySeconds, now) == "2026-11-02 10:42");
    assert(clock_text(now) == "10:42" && clock_text(0).empty());

    // the daily background check
    State bg;
    assert(background_due(bg, now));
    bg.last_check = now - 3600;
    assert(!background_due(bg, now));
    bg.last_check = now - kDaySeconds;
    assert(background_due(bg, now));
    bg.last_check = now + 3600;
    assert(background_due(bg, now));

    // one toast per new version
    State toast;
    assert(take_available_toast(toast).empty());
    toast.available = "0.8.13";
    assert(take_available_toast(toast) == "Launcher 0.8.13 is available\nSettings > Updates");
    assert(toast.notified == "0.8.13" && take_available_toast(toast).empty());
    toast.available = "0.8.14";
    assert(!take_available_toast(toast).empty() && toast.notified == "0.8.14");

    // "Updated to" once after a version change; nothing on a first start or a downgrade
    State start;
    assert(take_start_notice(start, false, "0.8.12").empty() && start.last_shown == "0.8.12");
    assert(take_start_notice(start, true, "0.8.12").empty());
    assert(take_start_notice(start, true, "0.8.13") == "Updated to 0.8.13\nWhat's new: Settings > Updates");
    assert(start.last_shown == "0.8.13" && start.updated_from == "0.8.12");
    assert(take_start_notice(start, true, "0.8.13").empty());
    assert(take_start_notice(start, true, "0.8.11").empty() && start.last_shown == "0.8.11" &&
           start.updated_from.empty());
    State no_key; /* a state file from before the key existed */
    assert(take_start_notice(no_key, true, "0.8.13").empty() && no_key.last_shown == "0.8.13");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s FIXTURE_DIR DOCS_DEV_DIR\n", argv[0]);
        return 2;
    }
    g_fixtures = argv[1];
    g_docs = argv[2];
    setenv("TZ", "UTC", 1);
    tzset();
    test_versions();
    test_parse_real();
    test_parse_broken();
    test_choice();
    test_headers_and_outcomes();
    test_notes_all_files();
    test_notes_edges();
    test_layout();
    test_whats_new();
    test_state();
    test_shown();
    std::puts("settings update model OK");
    return 0;
}
