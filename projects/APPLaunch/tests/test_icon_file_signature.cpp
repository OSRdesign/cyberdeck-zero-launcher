/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/icon_file_signature.hpp"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

static void write_file(const std::string &path, size_t bytes)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    assert(f);
    const std::string data(bytes, 'x');
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

static void set_mtime(const std::string &path, long seconds)
{
    struct timeval tv[2] = {{seconds, 0}, {seconds, 0}};
    assert(utimes(path.c_str(), tv) == 0);
}

int main()
{
    char tmpl[] = "/tmp/icon-sig-XXXXXX";
    const std::string root = mkdtemp(tmpl);
    assert(mkdir((root + "/share").c_str(), 0700) == 0);
    assert(mkdir((root + "/share/images").c_str(), 0700) == 0);
    const std::string real = root + "/share/images/app.png";
    const std::string lvgl = "A:/share/images/app.png";

    // LVGL drive paths map under the root; plain paths pass through
    assert(icon_real_path(lvgl, root) == real);
    assert(icon_real_path("A:share/images/app.png", root) == real);
    assert(icon_real_path("/opt/x.png", root) == "/opt/x.png");

    // a missing file is a signature of its own, and a path that is not a file never counts as one
    assert(!icon_file_signature(lvgl, root).exists);
    assert(!icon_file_signature("", root).exists);
    assert(!icon_file_signature(root, root).exists);

    write_file(real, 73);
    set_mtime(real, 1000000);
    IconSignatureSet set(root);
    set.capture({lvgl, "", lvgl});
    assert(set.size() == 2);       // the empty path is skipped
    assert(!set.changed());

    // replaced by a file of another size (the reported case: 73x64 icon replaced by a 100x100 one)
    write_file(real, 4096);
    set_mtime(real, 1000000);
    assert(set.changed());
    set.capture({lvgl});
    assert(!set.changed());

    // same size, new mtime (a same-dimension replacement)
    set_mtime(real, 1000050);
    assert(set.changed());
    set.capture({lvgl});
    assert(!set.changed());

    // removed, then back again
    assert(std::remove(real.c_str()) == 0);
    assert(set.changed());
    set.capture({lvgl});
    assert(!set.changed());
    write_file(real, 10);
    assert(set.changed());

    std::remove(real.c_str());
    rmdir((root + "/share/images").c_str());
    rmdir((root + "/share").c_str());
    rmdir(root.c_str());
    return 0;
}
