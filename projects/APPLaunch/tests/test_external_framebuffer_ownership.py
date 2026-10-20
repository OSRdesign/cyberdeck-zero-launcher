#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
# SPDX-License-Identifier: MIT
#
# Who owns the framebuffer around a full-screen app (study 026 section 1.6, rewritten for task 015).
#  1. The legacy path (APPLAUNCH_MULTITASK unset, the default): paint, stop every timer, run the app blocking, timers
#     back, home. No thread, no timer, no toast.
#  2. The supervised path (APPLAUNCH_MULTITASK=1): taken only for full-screen apps with the flag on, before anything
#     of the legacy path happens; paints before handing over, never stops the LVGL timers wholesale (the main loop
#     keeps serving the supervisor's bridge) and never blocks.
#  3. The hand-overs: the app's turn pauses the timers and suspends the display; the launcher's turn gives the display
#     back before it repaints home and before keys are accepted again.

from pathlib import Path

ui = Path(__file__).resolve().parents[1] / "main/ui"
source = (ui / "launch.cpp").read_text(encoding="utf-8")


def body_of(signature, end_marker="\n}\n"):
    start = source.index(signature)
    return source[start:source.index(end_marker, start) + len(end_marker)]


# 1. legacy path
start = source.index("void Launch::launch_Exec(")
end = source.index("\nvoid Launch::select_next_app()", start)
body = source[start:end]

branch = body.index("multitask_ui::active()")
paint = body.index("lv_refr_now(disp)")
pause = body.index("lv_timer_enable(false)")
execute = body.index('"ExecBlocking"')
resume = body.index("lv_timer_enable(true)")
# show_home() (native-UI aware wrapper around page->show_home_screen()); the first one is the compat-window branch
restore = body.index("show_home();", resume)

assert branch < paint < pause < execute < resume < restore
assert "lv_timer_create" not in body
assert "std::thread" not in body
assert "launcher_toast" not in body
# the supervised branch is only for full-screen apps and returns before the legacy path
branch_line = body[body.rindex("\n", 0, branch):body.index("\n", branch)]
assert "fullscreen" in branch_line, branch_line
assert body.index("return;", branch) < paint

# 2. supervised path
supervised = body_of("void Launch::launch_fullscreen_supervised(")
assert supervised.index("LVGL_RUN_FLAGE = 0") < supervised.index("lv_refr_now(disp)")
assert supervised.index("lv_refr_now(disp)") < supervised.index("multitask_ui::launch(")
for forbidden in ("lv_timer_enable", "ExecBlocking", "std::thread", "show_home"):
    assert forbidden not in supervised, forbidden

# 3. hand-overs
app_front = body_of("void Launch::mt_app_front()")
assert "LVGL_RUN_FLAGE = 0" in app_front
assert "display_handoff::enter_app_front(multitask_ui::bridge_timer())" in app_front
assert "lv_timer_enable" not in app_front

launcher_front = body_of("void Launch::mt_launcher_front(")
order = [launcher_front.index(token) for token in (
    "cp0_ui_state_republish()",
    "display_handoff::leave_app_front()",
    "cp0_display_set_touch_swallow(2)",
    "show_home()",
    "lv_refr_now(disp)",
    "LVGL_RUN_FLAGE = 1",
)]
assert order == sorted(order), order

handoff = (ui / "multitask/display_handoff.cpp").read_text(encoding="utf-8")
enter = handoff[handoff.index("void enter_app_front("):handoff.index("void leave_app_front(")]
assert enter.index("cp0_display_suspend()") < enter.index("lv_timer_pause(")
assert "lv_indev_reset(" in enter and "lv_timer_enable" not in enter
leave = handoff[handoff.index("void leave_app_front("):handoff.index("bool app_front(")]
assert leave.index("cp0_display_resume()") < leave.index("lv_timer_resume(")
