#!/bin/sh
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
# SPDX-License-Identifier: MIT

set -eu
build_dir="${TMPDIR:-/tmp}/cp0-lvgl-tests"
mkdir -p "$build_dir"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$(dirname "$0")/test_update_job.cpp" -o "$build_dir/test_update_job"
"$build_dir/test_update_job"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$(dirname "$0")/test_launcher_updater.cpp" -o "$build_dir/test_launcher_updater"
"$build_dir/test_launcher_updater"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="${TMPDIR:-/tmp}/cp0_lvgl_test_async_utils.$$"
esc_state_object="${TMPDIR:-/tmp}/cp0_esc_state.$$.$$.o"
trap 'rm -f "$binary" "$esc_state_object"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$root/include" \
    -c "$root/src/cp0_esc_state.c" -o "$esc_state_object"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_c_api_boundary.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_audio_runtime_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src/cp0" \
    -I"$root/../Miniaudio/include" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0/cp0_audio_system_sound_player.cpp" \
    "$root/tests/test_audio_system_sound_player.cpp" -ldl -lm -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_async_testable_utils.cpp" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_keyboard_input_lifecycle.c" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_keyboard_thread_lifecycle.c" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sync_signal.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_audio_mixer_control.cpp" \
    "$root/tests/test_audio_mixer_control.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_integer_codec.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_bluetooth_api_contract.cpp" \
    "$root/tests/test_bluetooth_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_bluetooth_error_policy.cpp" \
    "$root/tests/test_bluetooth_error_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    "$root/tests/test_bluetooth_recovery.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_pointer_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_font_cache_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src/cp0" \
    "$root/tests/test_wifi_error_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0/cp0_network_policy.cpp" \
    "$root/src/cp0_network_api_contract.cpp" \
    "$root/tests/test_wifi_connection.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" \
    "$root/tests/test_bounded_task_registry.cpp" -o "$binary"
"$binary"

# The Settings menu tree (cp0_tree.hpp): child walks, handles stable across appends, erase_children, destruction
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_cp0_tree.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/tests/test_runner_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_runner_resume_callback.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/tests/test_init_once.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/tests/test_signal_registration.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_init_plan.c" \
    "$root/tests/test_init_plan.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_display_screenshot_contract.cpp" \
    "$root/tests/test_display_screenshot_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_audio_api_contract.cpp" \
    "$root/tests/test_audio_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_camera_api_contract.cpp" \
    "$root/tests/test_camera_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_camera_api_contract.cpp" \
    "$root/src/cp0/cp0_camera_viewport.cpp" \
    "$root/src/sdl/sdl_lvgl_camera.cpp" \
    "$root/tests/test_sdl_camera_shutdown.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_framebuffer_codec.cpp" \
    "$root/tests/test_framebuffer_codec.cpp" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_fb_output.c" \
    "$root/tests/test_fb_output.c" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread \
    -I"$root/include" \
    "$root/src/cp0_backlight_profile.c" \
    "$root/tests/test_backlight_profile.c" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/src/cp0_alsa_parser.cpp" \
    "$root/tests/test_alsa_parser.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_alsa_parser.cpp" \
    "$root/src/cp0_soundcard_contract.cpp" \
    "$root/tests/test_soundcard_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_camera_viewport.cpp" \
    "$root/tests/test_camera_viewport.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_desktop_exec_policy.cpp" \
    "$root/tests/test_desktop_exec_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/tests/test_process_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_lora_runtime_policy.cpp" \
    "$root/tests/test_lora_runtime_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/tests/test_lora_gpio_offset_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_lora_contract.cpp" \
    "$root/tests/test_lora_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_lora_contract.cpp" \
    "$root/src/sdl/sdl_lvgl_lara.cpp" \
    "$root/tests/test_sdl_lora_registration.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_network_policy.cpp" \
    "$root/tests/test_network_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_network_api_contract.cpp" \
    "$root/tests/test_network_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$root/include" -I"$root/src" -I"$root/src/cp0" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0/cp0_process_commands.cpp" \
    "$root/tests/test_process_command_timeout.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_callback_result.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_callback_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_callback_owner_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_lvgl_callback_boundary.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sudo_timer_callback_boundary.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/tests/test_external_process_group.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_esc_exit_policy.cpp" -o "$binary"
"$binary"

# Esc-hold watcher of external apps: every keyboard, hotplug, unplug while held, no fd leak
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_esc_key_watch.cpp" \
    "$root/tests/test_esc_key_watch.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$esc_state_object" \
    "$root/src/sdl/sdl_external_app_runner.cpp" \
    "$root/tests/test_sdl_external_app_runner.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_filesystem_api.cpp" \
    "$root/src/cp0_posix_filesystem.cpp" \
    "$root/tests/test_posix_filesystem.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_resource_path_policy.cpp" \
    "$root/tests/test_resource_path_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_filesystem_api.cpp" \
    "$root/src/cp0_posix_filesystem.cpp" \
    "$root/src/cp0_resource_path_policy.cpp" \
    "$root/src/sdl/sdl_lvgl_filesystem.cpp" \
    "$root/tests/test_sdl_filesystem_api.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_dispatch_testable.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_dispatch_fallback.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_battery_testable.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_battery_codec.cpp" \
    "$root/tests/test_battery_codec.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/tests/test_battery_snapshot_cache.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" \
    "$root/src/cp0_battery_lifecycle.cpp" \
    "$root/tests/test_battery_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_battery_timer_release.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_battery_runtime_shutdown.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_runner_service_shutdown.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_rpc_runtime_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/tests/test_dispatch_shutdown_gate.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" "$root/src/cp0_battery_lifecycle.cpp" \
    "$root/tests/test_battery_init_failure.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" \
    "$root/src/cp0_status_lifecycle.cpp" \
    "$root/tests/test_status_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_status_component_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_owned_component_lifecycle.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_status_mount_rollback.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_component_delete_callback.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_screen_component_detach.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_screen_delete_cleanup.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_battery_codec.cpp" \
    "$root/src/cp0_battery_api_contract.cpp" \
    "$root/tests/test_battery_api_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_battery_codec.cpp" \
    "$root/src/cp0_battery_api_contract.cpp" \
    "$root/src/cp0_battery_lifecycle.cpp" \
    "$root/src/sdl/sdl_lvgl_bq27220.cpp" \
    "$root/tests/test_sdl_bq27220_api.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_osinfo_codec.cpp" \
    "$root/tests/test_osinfo_codec.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_osinfo_codec.cpp" \
    "$root/src/cp0_osinfo_contract.cpp" \
    "$root/tests/test_osinfo_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_settings_policy.cpp" \
    "$root/tests/test_settings_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" \
    "$root/src/cp0_pty_contract.cpp" \
    "$root/tests/test_pty_contract.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    -I"$root/../../SDK/github_source/eventpp/include" \
    "$root/src/cp0_pty_contract.cpp" \
    "$root/src/sdl/sdl_lvgl_pty.cpp" \
    "$root/tests/test_pty_runtime.cpp" -lutil -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_config_service.cpp" \
    "$root/tests/test_config_service.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" "$root/src/cp0_sudo_coordinator.cpp" \
    "$root/tests/test_sudo_coordinator.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sudo_prompt_key_policy.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_sudo_request_factory.cpp" \
    "$root/tests/test_sudo_request_factory.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/src" \
    "$root/tests/test_sudo_worker_registry.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sudo_command.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/src" "$root/tests/test_sudo_c_api_boundary.cpp" -o "$binary"
"$binary"

# time zone names (Settings > Date & Time > Time zone, the sudo admin command TimezoneSet)
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/src/cp0_timezone.cpp" "$root/tests/test_timezone.cpp" -o "$binary"
"$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" "$root/src/cp0_process_runner.cpp" \
    "$root/tests/test_process_runner.cpp" -o "$binary"
"$binary"

# The keymap test links libxkbcommon (apt: libxkbcommon-dev). Without it: SKIP with a clear message, unless CI=1 or
# REQUIRE_XKBCOMMON=1, where a missing library stays a hard failure (the compile below fails as before).
if printf '#include <xkbcommon/xkbcommon.h>\n' | "${CC:-cc}" -E -x c - >/dev/null 2>&1 \
    || [ "${CI:-}" = 1 ] || [ "${REQUIRE_XKBCOMMON:-}" = 1 ]; then
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -I"$root/include" -I"$root/src/cp0" \
        "$root/src/cp0/cp0_keyboard_keymap.c" \
        "$root/tests/test_keyboard_keymap.c" -lxkbcommon -o "$binary"
    "$binary"
else
    echo "SKIP test_keyboard_keymap (libxkbcommon-dev missing: sudo apt install libxkbcommon-dev)"
fi

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_keyboard_text.c" \
    "$root/src/cp0_esc_state.c" \
    "$root/src/cp0_keyboard_queue.c" \
    "$root/tests/test_keyboard_contract.c" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=thread -g \
    -I"$root/include" "$root/src/cp0_esc_state.c" \
    "$root/tests/test_esc_state_tsan.c" -o "$binary"
TSAN_OPTIONS="halt_on_error=1" "$binary"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
    -I"$root/include" "$root/tests/test_keyboard_input_context.cpp" -o "$binary"
"$binary"

# F1 phase 0: keyboard presence (parser against every device seen on the boards, rescan timing, inotify
# watcher), the keyboard thread's source bookkeeping (no duplicated keys), the input policy file
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread \
    -I"$root/include" \
    "$root/src/cp0/cp0_keyboard_presence.c" \
    "$root/tests/test_keyboard_presence.c" -o "$binary"
"$binary" "$root/tests/fixtures/proc_bus_input_devices_all_boards.txt" "$root/tests/fixtures/proc_bus_input_devices_fzm1.txt"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=thread -g \
    -I"$root/include" \
    "$root/src/cp0/cp0_keyboard_presence.c" \
    "$root/tests/test_keyboard_presence.c" -o "$binary"
TSAN_OPTIONS="halt_on_error=1" "$binary" "$root/tests/fixtures/proc_bus_input_devices_all_boards.txt" "$root/tests/fixtures/proc_bus_input_devices_fzm1.txt"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/src/cp0" \
    "$root/src/cp0/cp0_keyboard_sources.c" \
    "$root/tests/test_keyboard_sources.c" -o "$binary"
"$binary"

# Pointer navigation core (dead zone, axis, step rate and its cap, release, tap / hold / drag / click guard, the tap
# filter (min_click_ms) and its per-device table, wheel,
# the APPLAUNCH_POINTER_* overrides, the process-wide switch) against traces of the measured HackBerry pad; the
# pointer classifier is in test_keyboard_presence above
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_pointer_nav.c" \
    "$root/tests/test_pointer_nav.c" -o "$binary"
"$binary"

# Multitasking P-c: the slot-aware touch tracker (single touch unchanged against the reader of before), the
# three-finger hold G1 (study 026 section 5.3 cases in both touch orientations) and the status strip long-press G2, then
# G1 replayed over the P-a recordings of the deck and the Pi 3A+ (the analyser's numbers: deck 9 + 0 + 8 of 20 holds,
# Pi 3A+ 9 of 10, no false positive); AddressSanitizer + UndefinedBehaviorSanitizer when the compiler has them
mt_san="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
if ! printf 'int main(void){return 0;}\n' | "${CC:-cc}" -x c $mt_san - -o "$binary" 2>/dev/null; then
    echo "NOTE touch tracker / gesture tests without sanitizers (no ASan/UBSan runtime)"
    mt_san=""
fi
"${CC:-cc}" -std=c11 -g -Wall -Wextra -Werror $mt_san \
    -I"$root/include" \
    "$root/src/cp0_touch_slots.c" \
    "$root/tests/test_touch_slots.c" -o "$binary"
"$binary"
"${CC:-cc}" -std=c11 -g -Wall -Wextra -Werror $mt_san \
    -I"$root/include" \
    "$root/src/cp0_touch_slots.c" "$root/src/cp0_gesture_g1.c" "$root/src/cp0_gesture_g2.c" \
    "$root/tests/test_gesture_g1.c" -lm -o "$binary"
"$binary" > /dev/null || "$binary"
"${CC:-cc}" -std=c11 -g -Wall -Wextra -Werror $mt_san \
    -I"$root/include" \
    "$root/src/cp0_touch_slots.c" "$root/src/cp0_gesture_g1.c" "$root/src/cp0_ui_metrics.c" \
    "$root/tests/test_g1_replay.c" -lm -o "$binary"
fx="$root/tests/fixtures/mta"
gzip -dc "$fx/mtrec-zero7-20261020-000203.csv.gz" | "$binary" - 640x480 9/10 0
gzip -dc "$fx/mtrec-zero7-20261020-000730.csv.gz" | "$binary" - 640x480 0/0 0
gzip -dc "$fx/mtrec-zero7-20261020-000753.csv.gz" | "$binary" - 640x480 8/10 0
gzip -dc "$fx/mtrec-PI3A-20261020-002544.csv.gz" | "$binary" - 800x480 9/10 0

# Multitasking P-e: the app side of the lifecycle protocol (parse, environment, hide / show / close over a socketpair,
# the answer, the launcher going away, the key rule); AddressSanitizer + UndefinedBehaviorSanitizer as above
"${CC:-cc}" -std=c11 -g -Wall -Wextra -Werror $mt_san \
    -I"$root/include" \
    "$root/src/cp0_app_lifecycle.c" \
    "$root/tests/test_app_lifecycle.c" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0/cp0_input_state.c" \
    "$root/src/cp0_app_contract.c" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/tests/test_input_state.c" -o "$binary"
"$binary"

# Task 014 P5: the app contract (environment, screen.state and input.state read back as the launcher writes them,
# the token fallback, the on-screen keyboard rule shared with the launcher's keyboard)
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_app_contract.c" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_osk.c" \
    "$root/src/cp0/cp0_ui_state.c" \
    "$root/src/cp0/cp0_input_state.c" \
    "$root/tests/test_app_contract.c" -o "$binary"
"$binary"

# Task 014 P1b: layout service (classes, density, tokens, pinned deck / Pi 3A+ presets, compat placement,
# screen.state)
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0/cp0_ui_state.c" \
    "$root/tests/test_ui_metrics.c" -o "$binary"
"$binary"

# Task 014 P2a: roller maths and the native Settings page layout (centre row, falloff, snap, width cap, tokens)
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_roller.c" \
    "$root/tests/test_ui_roller.c" -o "$binary"
"$binary"

# CompactList (the apps' list): layout from the tokens in both densities, row tops, scroll-to-visible with one row of
# context, item stepping, keys, the tap rule, the live refresh rule (selection and scroll kept)
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_roller.c" \
    "$root/src/cp0_ui_list.c" \
    "$root/tests/test_ui_list.c" -o "$binary"
"$binary"

# Task L1: the shared status strip's place from the shell tokens, the Wi-Fi signal rule, the leftmost drawn column
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_status_strip.c" \
    "$root/tests/test_ui_status_strip.c" -o "$binary"
"$binary"

# Task 014 P2b: on-screen keyboard core (layout from the tokens, pages, Shift / Ctrl state machine, key event
# sequences, presence policy gate, dialog placement) and its injection path (press + release with text through the
# real queue, Ctrl+C, the Esc state and the release owed when the keyboard goes away mid-press, the text hint)
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_osk.c" \
    "$root/src/cp0_app_contract.c" \
    "$root/tests/test_ui_osk.c" -o "$binary"
"$binary"

# Task 014 P3a / P3b: info row layout (key / value, key cap, dots, the two-line wrap on Compact) and the dialog layout
# (tokens, centred above the on-screen keyboard, buttons / padding / header / scrolling steps at the five sizes)
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_roller.c" \
    "$root/src/cp0_ui_info.c" \
    "$root/tests/test_ui_info.c" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_osk.c" \
    "$root/src/cp0_app_contract.c" \
    "$root/src/cp0_ui_dialog.c" \
    "$root/tests/test_ui_dialog.c" -o "$binary"
"$binary"

# Task 014 P4: overlay sizes (toast, media OSD, loading): the compat branch and the pinned DECK / COMPACT tables equal
# today's numbers, computed sizes come from the tokens
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_overlay.c" \
    "$root/tests/test_ui_overlay.c" -o "$binary"
"$binary"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread \
    -I"$root/include" -I"$root/src" \
    "$root/src/cp0_ui_metrics.c" \
    "$root/src/cp0_ui_osk.c" \
    "$root/src/cp0_app_contract.c" \
    "$root/src/cp0_keyboard_inject_key.c" \
    "$root/src/cp0_keyboard_text.c" \
    "$root/src/cp0_esc_state.c" \
    "$root/src/cp0_keyboard_queue.c" \
    "$root/tests/test_osk_inject.c" -o "$binary"
"$binary"
