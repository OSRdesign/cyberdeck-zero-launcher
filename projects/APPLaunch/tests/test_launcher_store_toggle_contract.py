# SPDX-License-Identifier: MIT
#
# Task 014 P3e: Settings > Launcher lists the Store on the Pi boards (it can be hidden: a touch-only board cannot use
# it), the default profile keeps it always on. The switch is APPLAUNCH_LAUNCHER_STORE_TOGGLE in
# settings_hw_profile.hpp; the built-in app table and the render harness's copy of it must both follow it, and the
# Settings > Launcher filter must not drop the Store by its key.

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = (ROOT / "main/ui/settings/settings_hw_profile.hpp").read_text(encoding="utf-8")
REGISTRY = (ROOT / "main/ui/builtin_app_registry.cpp").read_text(encoding="utf-8")
HARNESS = (ROOT / "tests/render/src/harness_stubs.cpp").read_text(encoding="utf-8")
MODEL = (ROOT / "main/ui/settings/settings_launcher_model.hpp").read_text(encoding="utf-8")

MACRO = "APPLAUNCH_LAUNCHER_STORE_TOGGLE"
STORE_ROW = re.compile(
    r'\{"Store",\s*"store_100\.png",\s*"app_Store",\s*'
    + MACRO + r"\s*!=\s*0,\s*" + MACRO + r"\s*==\s*0\}"
)


def macro_values(text: str) -> list:
    return re.findall(r"#define\s+" + MACRO + r"\s+(\d)", text)


# Pi block first (#if defined(APPLAUNCH_HW_PIZERO2W)), then the default block (#else)
pi_block, stock_block = PROFILE.split("#else // default profile (no APPLAUNCH_HW)", 1)
assert macro_values(pi_block) == ["1"], macro_values(pi_block)
assert macro_values(stock_block) == ["0"], macro_values(stock_block)

assert STORE_ROW.search(REGISTRY), "builtin_app_registry.cpp: the Store row must use " + MACRO
assert '#include "settings/settings_hw_profile.hpp"' in REGISTRY
assert STORE_ROW.search(HARNESS), "harness_stubs.cpp: the Store row must match BUILTIN_APPS"
assert '#include "settings/settings_hw_profile.hpp"' in HARNESS

protected = re.search(r"is_protected_launcher_app\(std::string_view config_key\)\s*\{(.*?)\}", MODEL, re.S)
assert protected, "is_protected_launcher_app not found"
assert '"app_Store"' not in protected.group(1)
assert '"app_Setting"' in protected.group(1) and '"app_CLI"' in protected.group(1)


if __name__ == "__main__":
    pass
