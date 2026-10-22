# SPDX-License-Identifier: MIT
#
# Task 014 P3e: no page of the Pi boards' Settings tree may be left to the compat (legacy) path. Every page factory
# the tree puts on a Pi board must be one the native host knows: a native page, a section or a value page (Choice).
# settings_node_kind() sends any other factory to SettingsNodeKind::Custom (hidden by the native host).

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/ui/settings/settings_page.cpp").read_text()

# the entries our boards' profile does not build (settings_hw_profile.hpp: the macros are 0 or not defined there; the
# Speaker and Battery rows of our boards follow the board's capabilities at run time, report 030)
NOT_ON_PI = [
    "APPLAUNCH_SETTINGS_SPEAKER",
    "APPLAUNCH_SETTINGS_ETHERNET",
    "APPLAUNCH_SETTINGS_BATTERY",
]


def function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for i in range(brace, len(text)):
        depth += text[i] == "{"
        depth -= text[i] == "}"
        if depth == 0:
            return text[brace : i + 1]
    raise AssertionError("unbalanced " + signature)


def strip_comments(text: str) -> str:
    return re.sub(r"//.*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))


def pi_lines(body: str) -> str:
    # an #if on a NOT_ON_PI macro (or #if 0) is skipped up to its #else (kept: that is the Pi's branch) or #endif;
    # every other #if keeps both branches
    kept, skip_depth, depth = [], 0, 0
    for line in body.splitlines():
        stripped = line.strip()
        if stripped.startswith("#if"):
            depth += 1
            if not skip_depth and (any(m in stripped for m in NOT_ON_PI) or stripped.startswith("#if 0")):
                skip_depth = depth
        elif stripped.startswith("#else"):
            if skip_depth == depth:
                skip_depth = 0
        elif stripped.startswith("#endif"):
            if skip_depth == depth:
                skip_depth = 0
            depth -= 1
        elif not skip_depth:
            kept.append(line)
    return "\n".join(kept)


tree = pi_lines(strip_comments(function_body(SOURCE, "void UISettingTreePage::create_page_detail()")))
used = set(re.findall(r"\b(\w+_factory)\b", tree))
assert "settings_rtc_confirm_page_factory" in used, "the Save? page is no longer in the tree"

known = set()
# the Wi-Fi section is one native page: its Networks / Join Hidden Network children belong to it
for signature in ("SettingsNativePage settings_native_page(", "SettingsWifiSection settings_wifi_section(",
                  "SettingsNodeKind settings_node_kind("):
    known |= set(re.findall(r"\b(\w+_factory)\b", strip_comments(function_body(SOURCE, signature))))

missing = sorted(used - known)
assert not missing, f"pages of the Pi tree that fall to the compat path: {missing}"
print("settings native coverage OK:", len(used), "factories")
