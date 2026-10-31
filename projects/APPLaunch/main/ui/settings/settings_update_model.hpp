/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates, phase P1 of report 047 (the launcher checks GitHub for a newer release and shows its notes): the
 * rules, with no LVGL, no thread and no process. Unit-tested in projects/APPLaunch/tests/test_settings_update_model.cpp.
 *
 *   versions       semantic version order (0.8.13-rc.1 < 0.8.13 < 0.8.14): the launcher's versions are semver
 *                  (main/SConstruct enforces X.Y.Z[-suffix]); the Debian order of apps_backend::compare_versions would
 *                  rank 0.8.9-pda.1 above 0.8.9
 *   releases       the GitHub API's release list (GET /repos/<owner>/<repo>/releases, JSON through the SDK's cJSON):
 *                  tag, draft, pre-release, date, notes (body) and the assets (name, size, digest, URL); unknown
 *                  fields are ignored
 *   choice         which releases count (not a draft, not a pre-release, a plain X.Y.Z tag), the ones newer than the
 *                  running launcher (newest first: the notes of every skipped version), the one offered (the newest
 *                  with a bundle for this board's architecture), and what a development build is (report 047 D4:
 *                  checks and notes on every build, an install only on the release channel; P1 installs nothing)
 *   check outcome  curl's exit status, the HTTP status and the answer's headers mapped to what the page says (no
 *                  network, GitHub's rate limit, a certificate refused, ...)
 *   notes          the release notes' Markdown turned into plain text blocks (report 047 section 6, D9 / D10): up to
 *                  the marker <!-- launcher-notes-end --> (else up to the line **Install**), comments dropped, tables
 *                  of pictures as one line, bullets with their level, hard-wrapped lines joined; then laid out as one
 *                  text with version headings, bullets wrapped with a hanging indent by a width function the page
 *                  gives (LVGL's text width), at most 5 versions or about 24 KB
 *   state          ~/.local/state/cyberdeckzero/update.state (key=value lines; not config.json, whose store is near
 *                  its 64-key cap): last check, ETag, latest release seen, last outcome, the version whose start was
 *                  last seen ("Updated to" once, D11), the version a toast last announced (once per version, D3)
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace launcher_update {

constexpr const char *kDefaultReleasesUrl =
    "https://api.github.com/repos/OSRdesign/cyberdeck-zero-launcher/releases?per_page=10";
constexpr const char *kReleasesPage = "github.com/OSRdesign/cyberdeck-zero-launcher/releases";
constexpr const char *kNotesEndMarker = "<!-- launcher-notes-end -->";
constexpr long long kDaySeconds = 24 * 60 * 60;
constexpr std::size_t kMaxNoteVersions = 5;
constexpr std::size_t kMaxNoteBytes = 24 * 1024;

// ------------------------------------------------------------------ versions

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::vector<std::string> pre; /* the pre-release identifiers after '-' ("rc", "1"); empty for a release */
};

/* "0.8.12", "v0.8.12", "0.8.13-rc.1" (a "+build" part is accepted and ignored). False for anything else. */
bool parse_version(const std::string &text, Version &out);
/* Semantic version precedence: <0, 0, >0. A text that is no version sorts below every version. */
int compare_versions(const Version &a, const Version &b);
int compare_versions(const std::string &a, const std::string &b);
/* The tag without its leading "v" ("v0.8.12" -> "0.8.12"). */
std::string version_of_tag(const std::string &tag);

// ------------------------------------------------------------------ releases

struct Asset {
    std::string name;
    long long size = 0;
    std::string digest; /* "sha256:<hex>" when GitHub gives one */
    std::string url;    /* browser_download_url */
};

struct Release {
    std::string tag;          /* "v0.8.12" */
    std::string version;      /* "0.8.12" */
    std::string published_at; /* "2026-10-30T22:02:49Z" */
    std::string body;         /* the notes (Markdown, CRLF as GitHub serves it) */
    bool draft = false;
    bool prerelease = false;
    std::vector<Asset> assets;

    std::string date() const { return published_at.size() >= 10 ? published_at.substr(0, 10) : std::string(); }
};

/* The API's answer (a JSON array of releases). False (and why in error) when it is not one. Entries without a
 * tag_name are skipped. */
bool parse_releases(const std::string &json, std::vector<Release> &out, std::string *error = nullptr);
/* "cyberdeckzero-launcher-<version>-<arch>.tar.gz" (pizero2w/build.sh's name). */
std::string asset_name(const std::string &version, const std::string &arch);
/* The release's bundle for this architecture, nullptr when it has none. */
const Asset *find_asset(const Release &release, const std::string &arch);

// ------------------------------------------------------------------ the running build and the choice

struct Build {
    std::string version; /* LAUNCHER_VERSION_RAW: "0.8.12", "0.0.0-dev", "0.8.9-pda.1" */
    std::string channel; /* LAUNCHER_CHANNEL_RAW: "release", "development", "prerelease" */
    std::string arch;    /* apps_backend::system_arch(): "arm64", "amd64" */
};

/* Report 047 D4: anything that is not a plain X.Y.Z on the release channel (a local build 0.0.0-dev, a CI pre-release,
 * a test version with a suffix). It is shown the latest release and its notes, never offered one. */
bool development_build(const Build &build);

/* A release that counts: not a draft, not a pre-release, a plain X.Y.Z tag. */
bool eligible(const Release &release);

struct Offer {
    std::vector<Release> newer; /* eligible releases newer than the running launcher, newest first (a development
                                   build: the latest eligible release alone) */
    std::string latest;         /* the newest eligible release ("" when none) */
    std::string available;      /* the release offered: the newest newer one with a bundle for this architecture ("" on
                                   a development build or when none) */
    std::string missing_asset;  /* the newest newer release that has no bundle for this architecture, when it is newer
                                   than the one offered ("" otherwise) */
};
Offer choose(const std::vector<Release> &releases, const Build &build);

// ------------------------------------------------------------------ the check's outcome

enum class Outcome {
    None,        /* never checked */
    Ok,          /* an answer (200, or 304 with the cached list) */
    Offline,     /* no default route */
    NoClock,     /* the clock is not synchronised yet: a certificate could look "not yet valid" */
    Dns,         /* curl 6 */
    Connect,     /* curl 7 */
    Timeout,     /* curl 28 */
    Certificate, /* curl 35, 51, 58, 60, 77, 83 */
    RateLimited, /* HTTP 403 / 429 with X-RateLimit-Remaining: 0 */
    Http,        /* any other HTTP error */
    BadAnswer,   /* a 200 that is not a release list */
    NoCurl,      /* curl missing (127) */
    Failed,      /* anything else */
};

struct Headers {
    std::string etag;
    int rate_remaining = -1;  /* X-RateLimit-Remaining, -1: absent */
    long long rate_reset = 0; /* X-RateLimit-Reset (epoch seconds), 0: absent */
};
/* The headers of the last response in a curl -D dump (after redirects), names in any case. */
Headers parse_headers(const std::string &dump);
/* curl's exit status, the HTTP status (0: none) and the headers -> the outcome. 304 is Ok (the cached list). */
Outcome classify(int curl_rc, int http_status, const Headers &headers);
bool failed(Outcome outcome);
/* The status line for a failed check ("" for None / Ok). rate_reset_local: "21:15" or "" (unknown). */
std::string outcome_text(Outcome outcome, int http_status, const std::string &rate_reset_local);
/* Persisted name of an outcome ("ok", "offline", ...) and back (unknown: None). */
const char *outcome_key(Outcome outcome);
Outcome outcome_from_key(const std::string &key);

// ------------------------------------------------------------------ release notes

enum class BlockKind {
    Heading,   /* "## In pictures", or a version heading "0.8.13 - 2026-11-04" */
    Paragraph, /* joined lines */
    Bullet,    /* level 0 = "- ", level 1 = "  - " */
    Code,      /* one line of a fenced block, as it is */
    Note,      /* a line of our own: "(pictures in the online notes)", "0.8.12 is installed." */
};

struct NoteBlock {
    BlockKind kind = BlockKind::Paragraph;
    int level = 0;
    std::string text;

    bool operator==(const NoteBlock &other) const
    {
        return kind == other.kind && level == other.level && text == other.text;
    }
};

/* Inline Markdown to plain text: **bold** and `code` lose their markers, [text](url) keeps the text, ![alt](path)
 * goes. */
std::string inline_plain(const std::string &text);
/* The part of the notes the launcher shows (D10): up to the marker <!-- launcher-notes-end -->, else up to the line
 * that starts with **Install**, else all; CR removed. */
std::string notes_part(const std::string &markdown);
/* The notes as blocks (notes_part first). */
std::vector<NoteBlock> notes_blocks(const std::string &markdown);

/* The notes of one version, for What's new. */
struct NotesSource {
    std::string version;
    std::string date; /* "2026-11-04", "" when unknown (the notes shipped with the launcher) */
    std::string markdown;
};

/* What's new: one heading per version ("0.8.13 - 2026-11-04"), newest first, its blocks, at most max_versions
 * versions and about max_bytes of text; then "<installed> is installed." and, when versions were left out, where the
 * older notes are. Nothing to show: one Note saying so. */
std::vector<NoteBlock> whats_new_blocks(const std::vector<NotesSource> &newest_first, const std::string &installed,
                                        std::size_t max_versions = kMaxNoteVersions,
                                        std::size_t max_bytes = kMaxNoteBytes);

/* Width in pixels of a text in the page's font (LVGL's lv_text_get_width on the LVGL thread; a fixed width per
 * character in the tests). */
using MeasureText = std::function<int(const std::string &text)>;
/* The blocks as one text for the TextBlock: headings with a blank line before, paragraphs, bullets "- " (an indent of
 * two spaces per level) whose continuation lines start under the text (hard-wrapped with measure into width pixels;
 * no measure or width <= 0: no hard wrap), code lines as they are, a blank line between a run of bullets or code and
 * what follows. */
std::string blocks_text(const std::vector<NoteBlock> &blocks, const MeasureText &measure, int width);

// ------------------------------------------------------------------ state file

struct State {
    long long last_check = 0;   /* epoch seconds of the last check that ended (0: never) */
    long long last_success = 0; /* the last one that got an answer */
    std::string etag;           /* of the cached answer (If-None-Match) */
    std::string latest;         /* the newest eligible release seen */
    std::string available;      /* the release offered at the last good check ("" none) */
    std::string result;         /* outcome_key() of the last check */
    long long rate_reset = 0;   /* GitHub's rate limit reset (epoch), when the last check hit it */
    std::string last_shown;     /* the launcher version whose start was last seen */
    std::string updated_from;   /* the version before the last version change seen at a start */
    std::string notified;       /* the newest release a toast announced */
    long long rollback_seen = 0; /* P2: the start time of the root helper's run whose rollback a toast announced */

    bool operator==(const State &other) const;
};

/* key=value lines; unknown keys and malformed lines are ignored. */
State parse_state(const std::string &text);
std::string format_state(const State &state);
/* False when the file does not exist (state stays default) or cannot be read. */
bool read_state(const std::string &path, State &state);
/* Written to a temporary file next to it, then renamed; the folder is created. False on any failure. */
bool write_state(const std::string &path, const State &state);
/* Small file helpers (the cached release list): whole-file read, atomic write (folder created). */
bool read_file(const std::string &path, std::string &text);
bool write_file_atomic(const std::string &path, const std::string &text);

/* What a check that ended at `now` leaves in the state (latest, available, result, rate_reset, last_check, and
 * last_success / etag on an answer). Offline and NoClock record the result only: GitHub was not asked, so the daily
 * background check stays due and "Last checked" keeps the last real check. */
void record_check(State &state, Outcome outcome, const Offer *offer, const Headers &headers, long long now);

// ------------------------------------------------------------------ what the launcher shows

enum class Tone { Normal, Update, Muted };

struct RowValue {
    std::string text;
    Tone tone = Tone::Normal;
};

/* The Settings root row "Updates" and the Launcher row of the page: "0.8.13 available" (Update: amber), "Up to date"
 * (Normal: green), "Check failed" (Muted), "Dev build" (Muted: a development build, D4), "" before the first check. */
RowValue root_value(const State &state, const Build &build);

/* "today 10:42", "yesterday 18:03", "3 days ago", "never" (local time). */
std::string last_checked_text(long long when, long long now);
/* "21:15" (local time) or "" for 0. */
std::string clock_text(long long when);

/* D3: the background check is due once a day (never checked, a day since the last check, or a last check "in the
 * future" after a clock jump). */
bool background_due(const State &state, long long now);
/* D3: the toast for a release found by a background check, once per version: the text ("" none). Records it as
 * notified. */
std::string take_available_toast(State &state);

/* D11, at the launcher's start: the toast "Updated to <version>" once after a version change (an empty text when none:
 * a first start without a state file, the same version, a downgrade). Records the version as shown and, after a
 * change, the version it came from. state_existed: the state file was there. */
std::string take_start_notice(State &state, bool state_existed, const std::string &installed);

/* The notes What's new shows, newest first (report 047 section 5.9, 6.4):
 *   - releases newer than the running launcher (a development build: the latest release);
 *   - none: after an update (updated_from), the releases after updated_from up to the running one;
 *   - else the running version's own notes: its release in the list, else shipped_markdown (the notes the bundle
 *     installs, /usr/share/doc/cyberdeckzero-launcher/release-notes-<version>.md) */
std::vector<NotesSource> notes_sources(const std::vector<Release> &releases, const Offer &offer, const State &state,
                                       const Build &build, const std::string &shipped_markdown);

} // namespace launcher_update
