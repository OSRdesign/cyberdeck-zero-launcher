# How to build a native Settings page (cheat sheet)

The native Settings host draws the existing Settings tree full screen with the shell's widgets whenever
the dpi-scaled display backend exists, i.e. on every Pi board (`native_ui::enabled()`,
`projects/APPLaunch/main/ui/settings/settings_native_mode.hpp`; since task 014 P3e there is no switch and no compat
path). A page that has no native widget is hidden (`Custom`); stock CardputerZero, the SDL simulator and the emulator
have no such backend and keep the legacy roller pages.
Paths below are relative to `projects/APPLaunch/main/ui/settings/` unless they start with a repo folder. LVGL is 9.5.0.

## 1. How the host decides what a tree node becomes

`settings_node_kind()` (`settings_page.cpp`, declared in `settings_page.hpp`) classifies each `SettingEntry` from its
own page factory and API, in this order:

| Kind | Rule |
| --- | --- |
| `Native` | `settings_native_page(node)` is not `None` (checked first) |
| `Custom` | the entry's `page_type == PageType::FullCustom`, or any other factory not listed below (hidden: no compat path any more) |
| `Section` | factory `roller_page_factory` or `bluetooth_roller_page_factory` |
| `Choice` | a value-page factory: `brightness_page3_factory`, `dark_time_page3_factory`, `rtc_page3_factory`, `settings_rtc_confirm_page_factory` (Save?), `confirm_page3_factory`, `roller_page3_factory`, `settings_touch_choice_page_factory`, `settings_keyboard_choice_page_factory` |
| `Toggle` | no factory, `icon_enabled` and `has_api()` |
| `Action` | no factory, `has_api()` |
| `Plain` | nothing to open |

`settings_native_page()` (same file) maps a factory to a `SettingsNativePage` value (`KeyboardTest`, `Wifi`, `About`,
`User`, `Storage`, `Licenses`, `DateTimeInfo`), using `factory_is(factory, fn)`. The Wi-Fi section is matched as a
`roller_page_factory` node whose children contain `wifi_scan_page3_factory`: a whole section can become one page.
`settings_native_panel_create()` (`settings_native_panel.cpp`) switches on that value and calls the page's builder.
The host itself (`settings_native_host.hpp/.cpp`) shows the panel instead of its roller in `open_native()`.

## 2. Steps for a new page

1. **Model** (pure C++, no LVGL, no service call): `settings_<name>_model.hpp/.cpp` with the rules (sorting, texts,
   validation, what Enter does). Template: `settings_wifi_model.hpp` (`settings_wifi::signal_bars`, `sort_networks`,
   `row_action`, `password_error`, `status_line`). The blocking service wrapper can live next to it
   (`settings_wifi_com`: the `cp0_wifi_*` C API, called from a worker thread only).
2. **Unit test**: `projects/APPLaunch/tests/test_settings_<name>_model.cpp` (plain `assert`, see
   `test_settings_wifi_model.cpp`), registered as a compile-and-run block in `projects/APPLaunch/tests/run_tests.sh`
   (the Wi-Fi block compiles the test with `settings_wifi_model.cpp` and `-I` the settings folder and
   `ext_components/cp0_lvgl/include`). A new pure-C widget layout goes to `ext_components/cp0_lvgl/tests/test_ui_*.c`
   and `ext_components/cp0_lvgl/tests/run_tests.sh`.
3. **Classify**: add a value to `SettingsNativePage` (`settings_page.hpp`), return it from `settings_native_page()` for
   your factory, add the `case` in `settings_native_panel_create()`.
4. **Panel** (UI): `settings_native_<name>_page.hpp/.cpp` exporting
   `std::unique_ptr<SettingsNativePanel> settings_<name>_panel_create(const NodeIter &, const SettingsNativePanelContext &)`.
   The class derives from `SettingsNativePanel` (`settings_native_panel.hpp`); the context gives `page`
   (`cp0::ui::Page`), `group` (input group), `title` (path) and `back` (the host's back, posted). Follow
   `settings_native_wifi_page.cpp`:
   - build a `cp0::ui::Roller` on `page.root()` with `page.layout()` and `RollerEvents{activate, slide, back}`, add
     its `focus_object()` to the group (the Wi-Fi panel's own `focus()` helper does `lv_group_add_obj`);
   - rows are `cp0::ui::RowSpec` (`RowKind::Nav, Choice, Toggle, Slider, Info, Network, Busy`); `set_rows()` to
     rebuild, `update_row()` to change one in place;
   - service calls on a worker: a private `DComponens::LvglComponensBase` subclass (`Tasks`) and
     `run_async_task(AsyncTaskCallbacks<Result>, AsyncTaskOptions)` (`lvgl_components.hpp`); results come back on the LVGL thread and are checked against a generation counter;
   - never delete widgets inside their own event: queue with `post()` (`lv_async_call`), as the Wi-Fi page does;
   - `back()` returns true while the panel still has a level of its own (dialog, sub-list), false to let the host close
     it; the destructor cancels the async call, deletes the timer, clears fields and dialog, then resets `tasks_`
     (joins the workers) before the roller; errors in `page.set_caption(text, true)`, results also as
     `launcher_toast().show(...)` (`../launcher_toast.h`).
5. **Build files**: nothing to add. SCons takes every file of `ui/settings` (`main/SConstruct`,
   `append_srcs_dir(ADir('ui/settings'))`) and the render harness `$(wildcard $(UI)/settings/*.cpp)`
   (`projects/APPLaunch/tests/render/Makefile`).

## 3. Widgets (`ext_components/cp0_lvgl`)

All C++ widgets are in `include/cp0_ui_widgets.hpp` (namespace `cp0::ui`); their sizes come from pure-C layout headers
unit-tested on the PC. No widget takes pixel sizes from the page.

| Widget | C++ (`cp0_ui_widgets.hpp`, source) | Layout header (pure C), test |
| --- | --- | --- |
| Page scaffold: title, status line `set_caption`, on-screen Esc, `body()` for forms | `Page` | `cp0_ui_roller.h` (`cp0_ui_roller_page_t`) |
| Roller list (all row kinds) | `Roller`, `RowSpec`, `RollerEvents` (`src/cp0_ui_roller_widget.cpp`) | `cp0_ui_roller.h`, `tests/test_ui_roller.c` |
| InfoRow / InfoTable (key left, value right) | `InfoTable`, `InfoItem` (`src/cp0_ui_info_widget.cpp`); `settings_info::items_of()` turns "Key: value" lines into items (`settings_native_info_page.hpp`) | `cp0_ui_info.h`, `tests/test_ui_info.c` |
| Long text | `TextBlock` | - |
| Apps' list: top-aligned, Compact (dense data) or Comfortable (roller-height rows), sections, value tones, row affordances (Info / Open / Edit / Action, opt-in per row), live refresh, read-only, empty state; no scroll chevrons (when to use it instead of the Roller: `docs/cp0_lvgl.en.md`, "Lists: Roller or CompactList") | `CompactList`, `ListRow`, `RowAffordance`, `CompactListEvents`, `CompactListOptions` (`src/cp0_ui_list_widget.cpp`) | `cp0_ui_list.h`, `tests/test_ui_list.c` |
| Text field (Text, Number, Password with show / hide) | `TextField`, `TextFieldSpec`, `TextFieldEvents{changed, submit, back, escape}` (`src/cp0_ui_text_field.cpp`) | - |
| Dialog (title, body, 1-3 buttons: Primary = Enter, Cancel = Esc) | `Dialog`, `DialogSpec`, `DialogButton`, `ButtonRole`; fields go in `body()` with `field_layout()` | `cp0_ui_dialog.h`, `tests/test_ui_dialog.c` |
| On-screen keyboard; number pad for `TextType::Number` (`CP0_UI_OSK_PAGE_NUMPAD`: 1-9, Backspace 0 Enter) | `OnScreenKeyboard`, shown only by the `cp0::ui::osk` service (`request` / `release` / `dismiss`, `set_policy`) when a field edits; never summoned by a page (`src/cp0_ui_osk_widget.cpp`) | `cp0_ui_osk.h`, `tests/test_ui_osk.c` |

A field asks for the keyboard itself when it starts editing; a page only adds the field's `focus_object()` to the
group. The keyboard shows only when the host's policy allows it (no physical keyboard, setting Auto, no lock screen,
no desktop; `APPLAUNCH_OSK=force` in tests). Examples: the Wi-Fi password and hidden-network dialogs
(`settings_native_wifi_page.cpp`, `add_field()` and the `DialogSpec` with Cancel / Connect), the Number field of the
development keyboard form (`settings_keyboard_page.cpp`).

## 4. Harness scene with a fake backend

- The render harness (`projects/APPLaunch/tests/render/`, `README.md` there) runs the real launcher code on a fake
  framebuffer. Services are fakes in `src/harness_services.cpp` answering from `Fixtures` (`src/harness.h`): for Wi-Fi
  `cp0_signal_wifi_api` serves `wifi_networks`, accepts only `wifi_password` for a new secured network and
  `wifi_hidden_ssid` for Join Hidden. Add your service's commands and fixture fields there; an unknown command answers
  -1 and logs `no fixture`.
- Scene: `scenes/settings_native_<name>.scene`. Template: `scenes/settings_native_wifi.scene`
  (`env APPLAUNCH_DEV 0`, `env APPLAUNCH_OSK force`, `apps ...`, `home`,
  `header home`, `launch Settings`, `key DOWN` / `key ENTER`, `wait`, `shot NAME`, `header NAME "Settings > X" X`,
  `tapkey` for the on-screen keyboard). `review` marks shots without goldens. Without a `sizes` line a scene renders
  at 480x320, 640x480, 800x480, 720x720 and 1280x720 (`src/harness_main.cpp`).
- Run `tools/dev/check-all.sh` (unit tests, render, compare, twice, header); the contact sheet of the scene is
  `out/sheets/<scene>.png` in the WSL build tree's `projects/APPLaunch/tests/render/`.

Definition of done for launcher UI tasks: `docs/dev/DEV-GUIDE.md`, "Build, test, deploy".
