/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates: the rules (see settings_update_model.hpp).
 */

#include "settings_update_model.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

namespace launcher_update {

namespace {

bool all_digits(const std::string &text)
{
    if (text.empty()) return false;
    for (char c : text)
        if (c < '0' || c > '9') return false;
    return true;
}

bool parse_number(const std::string &text, int &value)
{
    if (!all_digits(text) || text.size() > 9) return false;
    value = std::atoi(text.c_str());
    return true;
}

std::vector<std::string> split(const std::string &text, char separator)
{
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::string lower(std::string text)
{
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string trim(const std::string &text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string without_cr(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        if (c != '\r') out.push_back(c);
    return out;
}

bool starts_with(const std::string &text, const char *prefix)
{
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

std::string json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring ? std::string(item->valuestring) : std::string();
}

bool json_true(const cJSON *object, const char *name)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(object, name)) != 0;
}

long long json_number(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) && item->valuedouble >= 0 ? static_cast<long long>(item->valuedouble) : 0;
}

/* Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm). */
long long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

bool local_tm(long long when, std::tm &out)
{
    const std::time_t t = static_cast<std::time_t>(when);
    return localtime_r(&t, &out) != nullptr;
}

std::string hh_mm(const std::tm &tm)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return text;
}

bool make_dirs(const std::string &dir)
{
    if (dir.empty()) return true;
    std::string path;
    for (const std::string &part : split(dir, '/')) {
        path += part;
        if (!path.empty() && ::mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) return false;
        path += '/';
    }
    return true;
}

std::string parent_of(const std::string &path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

} // namespace

// ------------------------------------------------------------------ versions

bool parse_version(const std::string &input, Version &out)
{
    std::string text = input;
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) text.erase(0, 1);
    const std::size_t plus = text.find('+');
    if (plus != std::string::npos) text.erase(plus);
    std::string pre;
    const std::size_t dash = text.find('-');
    if (dash != std::string::npos) {
        pre = text.substr(dash + 1);
        text.erase(dash);
        if (pre.empty()) return false;
    }
    const std::vector<std::string> core = split(text, '.');
    Version version;
    if (core.size() != 3 || !parse_number(core[0], version.major) || !parse_number(core[1], version.minor) ||
        !parse_number(core[2], version.patch))
        return false;
    if (!pre.empty()) {
        for (const std::string &id : split(pre, '.')) {
            if (id.empty()) return false;
            for (char c : id)
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
            version.pre.push_back(id);
        }
    }
    out = version;
    return true;
}

int compare_versions(const Version &a, const Version &b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.pre.empty() || b.pre.empty()) return a.pre.empty() == b.pre.empty() ? 0 : (a.pre.empty() ? 1 : -1);
    for (std::size_t i = 0; i < a.pre.size() && i < b.pre.size(); ++i) {
        const bool na = all_digits(a.pre[i]);
        const bool nb = all_digits(b.pre[i]);
        if (na && nb) {
            if (a.pre[i].size() != b.pre[i].size()) return a.pre[i].size() < b.pre[i].size() ? -1 : 1;
            const int c = a.pre[i].compare(b.pre[i]);
            if (c != 0) return c < 0 ? -1 : 1;
        } else if (na != nb) {
            return na ? -1 : 1; /* numeric identifiers sort below alphanumeric ones */
        } else {
            const int c = a.pre[i].compare(b.pre[i]);
            if (c != 0) return c < 0 ? -1 : 1;
        }
    }
    if (a.pre.size() == b.pre.size()) return 0;
    return a.pre.size() < b.pre.size() ? -1 : 1;
}

int compare_versions(const std::string &a, const std::string &b)
{
    Version va, vb;
    const bool pa = parse_version(a, va);
    const bool pb = parse_version(b, vb);
    if (!pa || !pb) return pa == pb ? 0 : (pa ? 1 : -1);
    return compare_versions(va, vb);
}

std::string version_of_tag(const std::string &tag)
{
    return !tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
}

// ------------------------------------------------------------------ releases

bool parse_releases(const std::string &json, std::vector<Release> &out, std::string *error)
{
    out.clear();
    cJSON *root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root) {
        if (error) *error = "not JSON";
        return false;
    }
    if (!cJSON_IsArray(root)) {
        if (error) {
            const std::string message = json_string(root, "message");
            *error = message.empty() ? "not a release list" : message;
        }
        cJSON_Delete(root);
        return false;
    }
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        if (!cJSON_IsObject(item)) continue;
        Release release;
        release.tag = json_string(item, "tag_name");
        if (release.tag.empty()) continue;
        release.version = version_of_tag(release.tag);
        release.draft = json_true(item, "draft");
        release.prerelease = json_true(item, "prerelease");
        release.published_at = json_string(item, "published_at");
        release.body = json_string(item, "body");
        const cJSON *assets = cJSON_GetObjectItemCaseSensitive(item, "assets");
        const cJSON *entry = nullptr;
        if (cJSON_IsArray(assets)) {
            cJSON_ArrayForEach(entry, assets)
            {
                if (!cJSON_IsObject(entry)) continue;
                Asset asset;
                asset.name = json_string(entry, "name");
                if (asset.name.empty()) continue;
                asset.size = json_number(entry, "size");
                asset.digest = json_string(entry, "digest");
                asset.url = json_string(entry, "browser_download_url");
                release.assets.push_back(std::move(asset));
            }
        }
        out.push_back(std::move(release));
    }
    cJSON_Delete(root);
    return true;
}

std::string asset_name(const std::string &version, const std::string &arch)
{
    return "cyberdeckzero-launcher-" + version + "-" + arch + ".tar.gz";
}

const Asset *find_asset(const Release &release, const std::string &arch)
{
    const std::string name = asset_name(release.version, arch);
    for (const Asset &asset : release.assets)
        if (asset.name == name) return &asset;
    return nullptr;
}

// ------------------------------------------------------------------ the running build and the choice

bool development_build(const Build &build)
{
    Version version;
    if (build.channel != "release" || !parse_version(build.version, version)) return true;
    return !version.pre.empty() || (version.major == 0 && version.minor == 0 && version.patch == 0);
}

bool eligible(const Release &release)
{
    Version version;
    return !release.draft && !release.prerelease && parse_version(release.version, version) && version.pre.empty();
}

Offer choose(const std::vector<Release> &releases, const Build &build)
{
    std::vector<Release> ok;
    for (const Release &release : releases)
        if (eligible(release)) ok.push_back(release);
    std::stable_sort(ok.begin(), ok.end(), [](const Release &a, const Release &b) {
        return compare_versions(a.version, b.version) > 0;
    });
    Offer offer;
    if (ok.empty()) return offer;
    offer.latest = ok.front().version;
    if (development_build(build)) {
        offer.newer.push_back(ok.front());
        return offer;
    }
    for (const Release &release : ok)
        if (compare_versions(release.version, build.version) > 0) offer.newer.push_back(release);
    for (const Release &release : offer.newer) {
        if (find_asset(release, build.arch)) {
            offer.available = release.version;
            break;
        }
    }
    if (!offer.newer.empty() && !find_asset(offer.newer.front(), build.arch))
        offer.missing_asset = offer.newer.front().version;
    return offer;
}

// ------------------------------------------------------------------ the check's outcome

Headers parse_headers(const std::string &dump)
{
    Headers headers;
    std::istringstream in(dump);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (starts_with(line, "HTTP/")) {
            headers = Headers(); /* a new response (after a redirect): only the last one counts */
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        if (name == "etag") {
            headers.etag = value;
        } else if (name == "x-ratelimit-remaining" && all_digits(value) && value.size() < 10) {
            headers.rate_remaining = std::atoi(value.c_str());
        } else if (name == "x-ratelimit-reset" && all_digits(value) && value.size() < 19) {
            headers.rate_reset = std::atoll(value.c_str());
        }
    }
    return headers;
}

Outcome classify(int curl_rc, int http_status, const Headers &headers)
{
    switch (curl_rc) {
    case 0:
    case 22: /* --fail: the HTTP status says the rest */
        break;
    case 6: return Outcome::Dns;
    case 7: return Outcome::Connect;
    case 28:
    case -2: /* our own process timeout */
        return Outcome::Timeout;
    case 35:
    case 51:
    case 58:
    case 60:
    case 77:
    case 83: return Outcome::Certificate;
    case 127: return Outcome::NoCurl;
    default: return Outcome::Failed;
    }
    if (http_status == 200 || http_status == 304) return Outcome::Ok;
    if ((http_status == 403 && headers.rate_remaining == 0) || http_status == 429) return Outcome::RateLimited;
    if (http_status == 0) return Outcome::Failed;
    return Outcome::Http;
}

bool failed(Outcome outcome)
{
    return outcome != Outcome::None && outcome != Outcome::Ok;
}

std::string outcome_text(Outcome outcome, int http_status, const std::string &rate_reset_local)
{
    switch (outcome) {
    case Outcome::None:
    case Outcome::Ok: return std::string();
    case Outcome::Offline: return "No network: the last result is shown";
    case Outcome::NoClock: return "Waiting for network time";
    case Outcome::Dns: return "Cannot find GitHub: no network or no DNS";
    case Outcome::Connect: return "Cannot reach GitHub";
    case Outcome::Timeout: return "GitHub did not answer in time";
    case Outcome::Certificate: return "GitHub's certificate was refused: check the clock";
    case Outcome::RateLimited:
        return rate_reset_local.empty() ? "GitHub's limit reached: try again in an hour"
                                        : "GitHub's limit reached: try again after " + rate_reset_local;
    case Outcome::Http: return "GitHub answered HTTP " + std::to_string(http_status);
    case Outcome::BadAnswer: return "Unexpected answer from GitHub";
    case Outcome::NoCurl: return "curl is not installed";
    case Outcome::Failed: break;
    }
    return "The check failed";
}

namespace {
struct OutcomeName {
    Outcome outcome;
    const char *key;
};
constexpr OutcomeName kOutcomeNames[] = {
    {Outcome::None, ""},          {Outcome::Ok, "ok"},
    {Outcome::Offline, "offline"}, {Outcome::NoClock, "no-clock"},
    {Outcome::Dns, "dns"},         {Outcome::Connect, "connect"},
    {Outcome::Timeout, "timeout"}, {Outcome::Certificate, "certificate"},
    {Outcome::RateLimited, "rate-limited"}, {Outcome::Http, "http"},
    {Outcome::BadAnswer, "bad-answer"},     {Outcome::NoCurl, "no-curl"},
    {Outcome::Failed, "failed"},
};
} // namespace

const char *outcome_key(Outcome outcome)
{
    for (const OutcomeName &name : kOutcomeNames)
        if (name.outcome == outcome) return name.key;
    return "";
}

Outcome outcome_from_key(const std::string &key)
{
    for (const OutcomeName &name : kOutcomeNames)
        if (key == name.key) return name.outcome;
    return Outcome::None;
}

// ------------------------------------------------------------------ release notes

std::string inline_plain(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const bool image = text[i] == '!' && i + 1 < text.size() && text[i + 1] == '[';
        if (text[i] == '[' || image) {
            /* [text](url) -> text; ![alt](path) -> nothing; anything else stays as it is */
            const std::size_t open = image ? i + 1 : i;
            const std::size_t close = text.find(']', open + 1);
            if (close != std::string::npos && close + 1 < text.size() && text[close + 1] == '(') {
                const std::size_t end = text.find(')', close + 2);
                if (end != std::string::npos) {
                    if (!image) out += text.substr(open + 1, close - open - 1);
                    i = end + 1;
                    continue;
                }
            }
        }
        if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            continue;
        }
        if (text[i] == '`') {
            ++i;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    /* runs of spaces left by a removed picture: one space */
    std::string collapsed;
    collapsed.reserve(out.size());
    for (char c : out) {
        if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
        collapsed.push_back(c);
    }
    return trim(collapsed);
}

std::string notes_part(const std::string &markdown)
{
    std::string text = without_cr(markdown);
    const std::size_t marker = text.find(kNotesEndMarker);
    if (marker != std::string::npos) return text.substr(0, marker);
    std::size_t start = 0;
    while (start < text.size()) {
        if (starts_with(text.substr(start, 11), "**Install**")) return text.substr(0, start);
        const std::size_t next = text.find('\n', start);
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return text;
}

namespace {

std::string without_comments(const std::string &text)
{
    std::string out;
    std::size_t i = 0;
    for (;;) {
        const std::size_t open = text.find("<!--", i);
        if (open == std::string::npos) {
            out += text.substr(i);
            return out;
        }
        out += text.substr(i, open - i);
        const std::size_t close = text.find("-->", open + 4);
        if (close == std::string::npos) return out; /* unterminated: the rest is the comment */
        i = close + 3;
    }
}

/* "  - text" -> level 1, "text"; false when the line is no bullet. */
bool bullet_of(const std::string &line, int &level, std::string &text)
{
    std::size_t spaces = 0;
    while (spaces < line.size() && line[spaces] == ' ') ++spaces;
    if (spaces + 1 >= line.size()) return false;
    const char mark = line[spaces];
    if ((mark != '-' && mark != '*' && mark != '+') || line[spaces + 1] != ' ') return false;
    level = std::min(3, static_cast<int>(spaces / 2));
    text = line.substr(spaces + 2);
    return true;
}

bool heading_of(const std::string &line, std::string &text)
{
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#') ++hashes;
    if (hashes == 0 || hashes > 6 || hashes >= line.size() || line[hashes] != ' ') return false;
    text = line.substr(hashes + 1);
    return true;
}

bool rule_line(const std::string &trimmed)
{
    if (trimmed.size() < 3) return false;
    for (char c : trimmed)
        if (c != '-' && c != '*' && c != '_') return false;
    return true;
}

} // namespace

std::vector<NoteBlock> notes_blocks(const std::string &markdown)
{
    const std::string text = without_comments(notes_part(markdown));
    std::vector<NoteBlock> blocks;
    bool open = false;       /* blocks.back() is a paragraph or bullet still taking continuation lines */
    std::string raw;         /* its text as written */
    bool in_code = false;
    bool in_table = false;
    bool table_pictures = false;

    auto close_block = [&]() {
        if (!open) return;
        open = false;
        const std::string plain = inline_plain(raw);
        if (plain.empty()) {
            if (raw.find("![") != std::string::npos) {
                blocks.back() = NoteBlock{BlockKind::Note, 0, "(pictures in the online notes)"};
            } else {
                blocks.pop_back();
            }
        } else {
            blocks.back().text = plain;
        }
        raw.clear();
    };
    auto close_table = [&]() {
        if (!in_table) return;
        in_table = false;
        const NoteBlock note{BlockKind::Note, 0,
                             table_pictures ? "(pictures in the online notes)" : "(a table in the online notes)"};
        table_pictures = false;
        if (blocks.empty() || !(blocks.back() == note)) blocks.push_back(note); /* two tables in a row: one line */
    };

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (starts_with(trimmed, "```")) {
            close_block();
            close_table();
            in_code = !in_code;
            continue;
        }
        if (in_code) {
            std::string code = line;
            while (!code.empty() && (code.back() == ' ' || code.back() == '\t')) code.pop_back();
            blocks.push_back(NoteBlock{BlockKind::Code, 0, code});
            continue;
        }
        if (!trimmed.empty() && trimmed[0] == '|') {
            close_block();
            in_table = true;
            if (trimmed.find("![") != std::string::npos) table_pictures = true;
            continue;
        }
        close_table();
        if (trimmed.empty()) {
            close_block();
            continue;
        }
        std::string body;
        int level = 0;
        if (heading_of(trimmed, body)) {
            close_block();
            blocks.push_back(NoteBlock{BlockKind::Heading, 0, inline_plain(body)});
            continue;
        }
        if (rule_line(trimmed)) {
            close_block();
            continue;
        }
        if (bullet_of(line, level, body)) {
            close_block();
            blocks.push_back(NoteBlock{BlockKind::Bullet, level, std::string()});
            raw = body;
            open = true;
            continue;
        }
        if (open) { /* a hard-wrapped line of the paragraph or bullet before */
            raw += ' ';
            raw += trimmed;
            continue;
        }
        blocks.push_back(NoteBlock{BlockKind::Paragraph, 0, std::string()});
        raw = trimmed;
        open = true;
    }
    close_block();
    close_table();
    /* a fenced block cut by the end of the part keeps its lines; trailing empty code lines go */
    while (!blocks.empty() && blocks.back().kind == BlockKind::Code && trim(blocks.back().text).empty())
        blocks.pop_back();
    return blocks;
}

std::vector<NoteBlock> whats_new_blocks(const std::vector<NotesSource> &newest_first, const std::string &installed,
                                        std::size_t max_versions, std::size_t max_bytes)
{
    std::vector<NoteBlock> out;
    if (newest_first.empty()) {
        out.push_back(NoteBlock{BlockKind::Note, 0, "No release notes for " + installed + " on this board yet."});
        return out;
    }
    std::size_t bytes = 0;
    std::size_t shown = 0;
    for (const NotesSource &source : newest_first) {
        if (shown >= max_versions) break;
        std::vector<NoteBlock> blocks = notes_blocks(source.markdown);
        std::size_t size = source.version.size() + 16;
        for (const NoteBlock &block : blocks) size += block.text.size() + 4;
        if (shown > 0 && bytes + size > max_bytes) break;
        bytes += size;
        ++shown;
        out.push_back(NoteBlock{BlockKind::Heading, 0,
                                source.date.empty() ? source.version : source.version + " - " + source.date});
        out.insert(out.end(), blocks.begin(), blocks.end());
    }
    out.push_back(NoteBlock{BlockKind::Note, 0, installed + " is installed."});
    if (shown < newest_first.size()) out.push_back(NoteBlock{BlockKind::Note, 0, std::string("Older notes: ") + kReleasesPage});
    return out;
}

namespace {

/* The bullet's words on lines of at most `width` pixels: the first line starts with `prefix`, the others with spaces
 * as wide as it (at least). */
void wrap_bullet(std::string &out, const std::string &prefix, const std::string &text, const MeasureText &measure,
                 int width)
{
    if (!measure || width <= 0) {
        out += prefix + text + "\n";
        return;
    }
    const int prefix_w = measure(prefix);
    std::string hang;
    while (measure(hang) < prefix_w && hang.size() < 24) hang.push_back(' ');
    std::string line = prefix;
    bool has_word = false;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        const std::string candidate = has_word ? line + " " + word : line + word;
        if (!has_word || measure(candidate) <= width) {
            line = candidate;
            has_word = true;
            continue;
        }
        out += line + "\n";
        line = hang + word;
    }
    out += line + "\n";
}

} // namespace

std::string blocks_text(const std::vector<NoteBlock> &blocks, const MeasureText &measure, int width)
{
    std::string out;
    const NoteBlock *previous = nullptr;
    for (const NoteBlock &block : blocks) {
        const bool after_heading = previous && previous->kind == BlockKind::Heading;
        bool blank = false;
        if (previous) {
            switch (block.kind) {
            case BlockKind::Heading: blank = true; break;
            case BlockKind::Bullet: blank = !after_heading && previous->kind != BlockKind::Bullet; break;
            case BlockKind::Code: blank = !after_heading && previous->kind != BlockKind::Code; break;
            case BlockKind::Paragraph:
            case BlockKind::Note: blank = !after_heading; break;
            }
        }
        if (blank) out += "\n";
        if (block.kind == BlockKind::Bullet) {
            wrap_bullet(out, std::string(static_cast<std::size_t>(2 * block.level), ' ') + "- ", block.text, measure,
                        width);
        } else {
            out += block.text + "\n";
        }
        previous = &block;
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// ------------------------------------------------------------------ state file

bool State::operator==(const State &o) const
{
    return last_check == o.last_check && last_success == o.last_success && etag == o.etag && latest == o.latest &&
           available == o.available && result == o.result && rate_reset == o.rate_reset &&
           last_shown == o.last_shown && updated_from == o.updated_from && notified == o.notified &&
           rollback_seen == o.rollback_seen;
}

State parse_state(const std::string &text)
{
    State state;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t equal = line.find('=');
        if (equal == std::string::npos || equal == 0) continue;
        const std::string key = line.substr(0, equal);
        const std::string value = line.substr(equal + 1);
        auto number = [&value]() -> long long {
            return all_digits(value) && value.size() < 19 ? std::atoll(value.c_str()) : 0;
        };
        if (key == "last_check") state.last_check = number();
        else if (key == "last_success") state.last_success = number();
        else if (key == "etag") state.etag = value;
        else if (key == "latest") state.latest = value;
        else if (key == "available") state.available = value;
        else if (key == "result") state.result = value;
        else if (key == "rate_reset") state.rate_reset = number();
        else if (key == "last_shown") state.last_shown = value;
        else if (key == "updated_from") state.updated_from = value;
        else if (key == "notified") state.notified = value;
        else if (key == "rollback_seen") state.rollback_seen = number();
    }
    return state;
}

std::string format_state(const State &state)
{
    auto clean = [](const std::string &value) {
        std::string out;
        for (char c : value)
            if (c != '\n' && c != '\r') out.push_back(c);
        return out;
    };
    std::ostringstream out;
    out << "# Settings > Updates (report 047): written by the launcher\n";
    out << "last_check=" << state.last_check << "\n";
    out << "last_success=" << state.last_success << "\n";
    out << "etag=" << clean(state.etag) << "\n";
    out << "latest=" << clean(state.latest) << "\n";
    out << "available=" << clean(state.available) << "\n";
    out << "result=" << clean(state.result) << "\n";
    out << "rate_reset=" << state.rate_reset << "\n";
    out << "last_shown=" << clean(state.last_shown) << "\n";
    out << "updated_from=" << clean(state.updated_from) << "\n";
    out << "notified=" << clean(state.notified) << "\n";
    out << "rollback_seen=" << state.rollback_seen << "\n";
    return out.str();
}

bool read_file(const std::string &path, std::string &text)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return !in.bad();
}

bool write_file_atomic(const std::string &path, const std::string &text)
{
    if (path.empty() || !make_dirs(parent_of(path))) return false;
    const std::string temp = path + ".tmp." + std::to_string(static_cast<long long>(::getpid()));
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        out.flush();
        if (!out) {
            ::unlink(temp.c_str());
            return false;
        }
    }
    if (::rename(temp.c_str(), path.c_str()) != 0) {
        ::unlink(temp.c_str());
        return false;
    }
    return true;
}

bool read_state(const std::string &path, State &state)
{
    std::string text;
    if (!read_file(path, text)) return false;
    state = parse_state(text);
    return true;
}

bool write_state(const std::string &path, const State &state)
{
    return write_file_atomic(path, format_state(state));
}

void record_check(State &state, Outcome outcome, const Offer *offer, const Headers &headers, long long now)
{
    state.result = outcome_key(outcome);
    if (outcome == Outcome::Offline || outcome == Outcome::NoClock) return; /* nothing was asked: not a check */
    state.last_check = now;
    if (outcome == Outcome::Ok) {
        state.last_success = now;
        if (!headers.etag.empty()) state.etag = headers.etag;
        if (offer) {
            state.latest = offer->latest;
            state.available = offer->available;
        }
        state.rate_reset = 0;
    } else if (outcome == Outcome::RateLimited) {
        state.rate_reset = headers.rate_reset;
    }
}

// ------------------------------------------------------------------ what the launcher shows

RowValue root_value(const State &state, const Build &build)
{
    if (development_build(build)) return {"Dev build", Tone::Muted};
    if (!state.available.empty() && compare_versions(state.available, build.version) > 0)
        return {state.available + " available", Tone::Update};
    const Outcome outcome = outcome_from_key(state.result);
    if (failed(outcome)) return {"Check failed", Tone::Muted};
    if (outcome == Outcome::Ok) return {"Up to date", Tone::Normal};
    return {};
}

std::string last_checked_text(long long when, long long now)
{
    if (when <= 0) return "never";
    std::tm then {}, today {};
    if (!local_tm(when, then) || !local_tm(now, today)) return "never";
    const long long days = days_from_civil(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday) -
                           days_from_civil(then.tm_year + 1900, then.tm_mon + 1, then.tm_mday);
    if (days == 0) return "today " + hh_mm(then);
    if (days == 1) return "yesterday " + hh_mm(then);
    if (days > 1) return std::to_string(days) + " days ago";
    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d %s", then.tm_year + 1900, then.tm_mon + 1, then.tm_mday,
                  hh_mm(then).c_str());
    return text;
}

std::string clock_text(long long when)
{
    std::tm tm {};
    if (when <= 0 || !local_tm(when, tm)) return std::string();
    return hh_mm(tm);
}

bool background_due(const State &state, long long now)
{
    if (state.last_check <= 0) return true;
    if (state.last_check > now + 300) return true; /* the clock went back: a check "in the future" */
    return now - state.last_check >= kDaySeconds;
}

std::string take_available_toast(State &state)
{
    if (state.available.empty() || compare_versions(state.available, state.notified) <= 0) return std::string();
    state.notified = state.available;
    return "Launcher " + state.available + " is available\nSettings > Updates";
}

std::string take_start_notice(State &state, bool state_existed, const std::string &installed)
{
    if (!state_existed || state.last_shown.empty()) {
        state.last_shown = installed; /* a first start: nothing to announce */
        return std::string();
    }
    const int change = compare_versions(installed, state.last_shown);
    if (change == 0) return std::string();
    if (change < 0) { /* an older launcher again (installed by hand): no toast */
        state.last_shown = installed;
        state.updated_from.clear();
        return std::string();
    }
    state.updated_from = state.last_shown;
    state.last_shown = installed;
    return "Updated to " + installed + "\nWhat's new: Settings > Updates";
}

std::vector<NotesSource> notes_sources(const std::vector<Release> &releases, const Offer &offer, const State &state,
                                       const Build &build, const std::string &shipped_markdown)
{
    std::vector<NotesSource> sources;
    for (const Release &release : offer.newer) sources.push_back({release.version, release.date(), release.body});
    if (!sources.empty()) return sources;

    std::vector<Release> ok;
    for (const Release &release : releases)
        if (eligible(release)) ok.push_back(release);
    std::stable_sort(ok.begin(), ok.end(), [](const Release &a, const Release &b) {
        return compare_versions(a.version, b.version) > 0;
    });
    if (!state.updated_from.empty() && compare_versions(state.updated_from, build.version) < 0) {
        for (const Release &release : ok)
            if (compare_versions(release.version, state.updated_from) > 0 &&
                compare_versions(release.version, build.version) <= 0)
                sources.push_back({release.version, release.date(), release.body});
        if (!sources.empty() && compare_versions(sources.front().version, build.version) == 0) return sources;
        sources.clear(); /* the list lacks the running version (no answer since the update): its shipped notes */
    }
    for (const Release &release : releases) {
        if (compare_versions(release.version, build.version) == 0 && !release.draft) {
            sources.push_back({release.version, release.date(), release.body});
            return sources;
        }
    }
    if (!trim(shipped_markdown).empty()) sources.push_back({build.version, std::string(), shipped_markdown});
    return sources;
}

} // namespace launcher_update
