/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <sys/stat.h>

#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/*
 * Identity of an icon file on disk: existence + mtime + size. The home grid decodes an icon (and an
 * lv_image widget remembers its width/height) when the app list is built; this lets the launcher notice
 * that the file was replaced afterwards (app upgrade, manual copy) and rebuild instead of drawing the
 * new pixels at the geometry of the old file. Pure standard C++/POSIX: no LVGL, testable on the host.
 */
struct IconFileSignature {
    bool exists = false;
    std::int64_t mtime_ns = 0;
    std::uint64_t size = 0;

    bool operator==(const IconFileSignature &o) const
    {
        return exists == o.exists && mtime_ns == o.mtime_ns && size == o.size;
    }
    bool operator!=(const IconFileSignature &o) const { return !(*this == o); }
};

inline constexpr const char *kIconFsRoot = "/usr/share/APPLaunch";

/* "A:/share/images/x.png" (an LVGL drive path) -> "<root>/share/images/x.png"; other paths are returned as is. */
inline std::string icon_real_path(const std::string &path, const std::string &root = kIconFsRoot)
{
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        std::string rest = path.substr(2);
        if (rest.empty() || rest.front() != '/') rest.insert(rest.begin(), '/');
        return root + rest;
    }
    return path;
}

inline IconFileSignature icon_file_signature(const std::string &path, const std::string &root = kIconFsRoot)
{
    IconFileSignature sig;
    if (path.empty()) return sig;
    struct stat st {};
    if (stat(icon_real_path(path, root).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return sig;
    sig.exists = true;
    sig.size = static_cast<std::uint64_t>(st.st_size);
#if defined(__APPLE__)
    sig.mtime_ns = static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
    sig.mtime_ns = static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
#endif
    return sig;
}

/* The signatures of a list of icon paths, taken at one moment; changed() re-stats and compares. */
class IconSignatureSet {
public:
    explicit IconSignatureSet(std::string root = kIconFsRoot) : root_(std::move(root)) {}

    void capture(const std::vector<std::string> &paths)
    {
        entries_.clear();
        entries_.reserve(paths.size());
        for (const std::string &p : paths)
            if (!p.empty()) entries_.emplace_back(p, icon_file_signature(p, root_));
    }

    bool changed() const
    {
        for (const auto &e : entries_)
            if (icon_file_signature(e.first, root_) != e.second) return true;
        return false;
    }

    std::size_t size() const { return entries_.size(); }

private:
    std::string root_;
    std::vector<std::pair<std::string, IconFileSignature>> entries_;
};
