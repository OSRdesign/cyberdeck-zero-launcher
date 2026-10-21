# SPDX-License-Identifier: MIT
#
# Render harness build: host gcc/g++ + FreeType, no SDL, no libinput, no D-Bus (see README.md).
# Usually driven by run.sh:  make -C projects/APPLaunch/tests/render -j$(nproc)
#
# LVGL 9.5 is compiled from the SDK copy the device build already downloaded
# (SDK/github_source/lvgl/lvgl_9_5), with the device's LVGL config (config/lvgl_config.h) and the
# launcher's own FreeType override (main/lvgl). Launcher sources are compiled unchanged; the harness
# replaces only what talks to hardware or system services (src/).

HARNESS  := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ROOT     := $(abspath $(HARNESS)/../../../..)
PROJECT  := $(ROOT)/projects/APPLaunch
UI       := $(PROJECT)/main/ui
CP0      := $(ROOT)/ext_components/cp0_lvgl
LVGL_DIR := $(ROOT)/SDK/github_source/lvgl/lvgl_9_5
LVGL     := $(LVGL_DIR)/lvgl
BUILD    ?= $(HARNESS)/build
OBJ      := $(BUILD)/obj
BIN      := $(BUILD)/render-harness

CC  ?= gcc
CXX ?= g++

ifeq ($(wildcard $(LVGL)/lvgl.h),)
$(error LVGL 9.5 not found at $(LVGL): run the device build once (scons) so the SDK downloads it)
endif

FT_CFLAGS  := $(shell pkg-config --cflags freetype2)
FT_LIBS    := $(shell pkg-config --libs freetype2)
PNG_CFLAGS := $(shell pkg-config --cflags libpng)
PNG_LIBS   := $(shell pkg-config --libs libpng)

OPT      := -O2 -g0 -ffunction-sections -fdata-sections -fno-strict-aliasing -U_FORTIFY_SOURCE
LV_DEFS  := '-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="$(HARNESS)/config/lvgl_config.h"'
LV_INC   := -I$(LVGL_DIR) -I$(LVGL) $(FT_CFLAGS)

# Same defines as the deck build (APPLAUNCH_HW=pizero2w, SConstruct of main and cp0_lvgl).
APP_DEFS := -DAPPLAUNCH_HW_PIZERO2W=1 -DLAUNCHER_BUILD \
            -DLAUNCHER_GIT_COMMIT_RAW=harness -DLAUNCHER_VERSION_RAW=0.5.0 \
            -DLAUNCHER_CHANNEL_RAW=harness -DLAUNCHER_BUILD_DATE_RAW=2026-10-10 \
            $(foreach f,FILESYSTEM CONFIG PTY RPC AUDIO PROCESS SUDO OSINFO SCREENSHOT LORA WIFI \
                        BLUETOOTH SETTINGS BQ27220 SAVED_SETTINGS BATTERY SOUNDCARD,-DCONFIG_CP0_LVGL_INIT_$(f)=1)
APP_INC  := -I$(HARNESS)/src -I$(PROJECT)/main -I$(PROJECT)/main/include -I$(UI) \
            -I$(CP0)/include -I$(CP0)/src -I$(ROOT)/SDK/github_source/eventpp/include \
            -I$(ROOT)/SDK/components/utilities/include $(LV_INC) $(PNG_CFLAGS)

# LVGL: everything under src/, with the launcher's FreeType files instead of LVGL's.
LV_OVERRIDE := lv_freetype.c lv_freetype_glyph.c lv_freetype_image.c
LVGL_SRCS := $(filter-out $(addprefix $(LVGL)/src/libs/freetype/,$(LV_OVERRIDE)),\
               $(shell find $(LVGL)/src -name '*.c' -o -name '*.cpp')) \
             $(addprefix $(PROJECT)/main/lvgl/,$(LV_OVERRIDE))
LVGL_PRIV := -I$(PROJECT)/main/lvgl -I$(LVGL)/src/libs/freetype

# cp0_lvgl pieces the shell and the pages draw with (real code).
CP0_SRCS := \
    $(CP0)/src/cp0/cp0_fb_output.c \
    $(CP0)/src/cp0_app_contract.c \
    $(CP0)/src/cp0_ui_metrics.c \
    $(CP0)/src/cp0_ui_metrics_lvgl.c \
    $(CP0)/src/cp0_ui_overlay.c \
    $(CP0)/src/cp0_ui_roller.c \
    $(CP0)/src/cp0_ui_roller_widget.cpp \
    $(CP0)/src/cp0_ui_page.cpp \
    $(CP0)/src/cp0_ui_status_strip.c \
    $(CP0)/src/cp0_ui_status_strip_widget.cpp \
    $(CP0)/src/cp0_ui_osk.c \
    $(CP0)/src/cp0_ui_osk_widget.cpp \
    $(CP0)/src/cp0_ui_text_field.cpp \
    $(CP0)/src/cp0_ui_info.c \
    $(CP0)/src/cp0_ui_info_widget.cpp \
    $(CP0)/src/cp0_ui_list.c \
    $(CP0)/src/cp0_ui_list_widget.cpp \
    $(CP0)/src/cp0_ui_dialog.c \
    $(CP0)/src/cp0_ui_dialog_widget.cpp \
    $(CP0)/src/cp0_sudo_prompt_ui.cpp \
    $(CP0)/src/cp0_timezone.cpp \
    $(CP0)/src/cp0_keyboard_inject_key.c \
    $(CP0)/src/cp0_pointer_nav.c \
    $(CP0)/src/cp0_touch_slots.c \
    $(CP0)/src/cp0/cp0_ui_state.c \
    $(CP0)/src/cp0/cp0_statusbar.c \
    $(CP0)/src/cp0/cp0_keyboard_lvgl_input.c \
    $(CP0)/src/cp0_keyboard_queue.c \
    $(CP0)/src/cp0_keyboard_text.c \
    $(CP0)/src/cp0_esc_state.c \
    $(CP0)/src/cp0_backlight_profile.c \
    $(CP0)/src/commount.c \
    $(CP0)/src/commount.cpp \
    $(CP0)/src/cp0_font_service.cpp \
    $(CP0)/src/ui_app_page.cpp \
    $(CP0)/src/cp0_network_api_contract.cpp \
    $(CP0)/src/cp0_status_lifecycle.cpp \
    $(CP0)/src/cp0_status_ethernet.c \
    $(CP0)/src/cp0_lvgl_app_page_assets.c

# The display manager: compiled unchanged, then its open/ioctl/mmap references are renamed to the
# harness fake devices (src/harness_fb.c) with objcopy. Nothing else in the binary is affected.
DPI_SRC := $(CP0)/src/cp0/cp0_lvgl_dpi_scaled.c
DPI_OBJ := $(OBJ)/dpi_scaled_redirected.o
# Its touch reader (multitasking P-c) the same way: its ioctl calls (capabilities, read back) reach the fake device.
TOUCH_SRC := $(CP0)/src/cp0/cp0_touch_reader.c
TOUCH_OBJ := $(OBJ)/touch_reader_redirected.o

# Settings > Apps' workers (task 014 P3d): compiled unchanged, then their references to the Store backend program
# (apps_backend::run, run_program, available) are renamed to the in-process fake (src/harness_apps.cpp, namespace
# harness_apps: as long as apps_backend, so the mangled names keep their shape) with objcopy. Nothing else is affected.
APPS_JOBS_SRC := $(UI)/settings/settings_apps_jobs.cpp
APPS_JOBS_OBJ := $(OBJ)/settings_apps_jobs_redirected.o
APPS_BACKEND_SYMS := _ZN12apps_backend(3run|11run_program|9available)E

# Launcher sources (real code).
APP_SRCS := $(addprefix $(UI)/,\
    native_ui.cpp app_launch_env.cpp launcher_toast.cpp native_screensaver.cpp touch_settings.cpp launcher_app.cpp \
    ui_loading.cpp launcher_media_osd.cpp esc_ui_watchdog.cpp app_directory_watcher.cpp launcher_media_controls.cpp \
    native_vkbd.cpp keyboard_text_input.cpp app_registry.cpp launcher_osk.cpp launcher_pointer.cpp \
    model/launcher_media_model.cpp model/setup_value_policy.cpp model/launcher_navigation_model.cpp \
    page_app/ui_app_calculator.cpp \
    page_app/ui_app_ssh.cpp page_app/ui_app_ssh_view.cpp page_app/ui_app_ssh_native.cpp \
    model/ssh_connection_model.cpp model/ssh_native_form_model.cpp \
    page_app/ui_app_st.cpp page_app/ui_app_st_parser.cpp page_app/ui_app_st_renderer.cpp \
    page_app/ui_app_st_session.cpp page_app/ui_app_st_viewport.cpp \
    model/st_key_encoder.cpp model/terminal_unicode.cpp model/async_operation_lifecycle.cpp \
    multitask/display_handoff.cpp multitask/switcher_view.cpp multitask/switcher_model.cpp) \
    $(filter-out $(APPS_JOBS_SRC),$(wildcard $(UI)/settings/*.cpp))

HARNESS_SRCS := $(wildcard $(HARNESS)/src/*.c $(HARNESS)/src/*.cpp)

obj_of = $(patsubst $(ROOT)/%,$(OBJ)/%.o,$(1))

LVGL_OBJS    := $(call obj_of,$(LVGL_SRCS))
CP0_OBJS     := $(call obj_of,$(CP0_SRCS))
APP_OBJS     := $(call obj_of,$(APP_SRCS))
HARNESS_OBJS := $(call obj_of,$(HARNESS_SRCS))
ALL_OBJS     := $(LVGL_OBJS) $(CP0_OBJS) $(APP_OBJS) $(HARNESS_OBJS) $(call obj_of,$(DPI_SRC)) \
                $(call obj_of,$(TOUCH_SRC)) $(call obj_of,$(APPS_JOBS_SRC))

.PHONY: all clean
all: $(BIN)

$(BIN): $(LVGL_OBJS) $(CP0_OBJS) $(APP_OBJS) $(HARNESS_OBJS) $(DPI_OBJ) $(TOUCH_OBJ) $(APPS_JOBS_OBJ)
	$(CXX) -o $@ $^ -Wl,--gc-sections $(FT_LIBS) $(PNG_LIBS) -lpthread -lm

$(DPI_OBJ): $(call obj_of,$(DPI_SRC))
	objcopy --redefine-sym open=harness_fb_open --redefine-sym ioctl=harness_fb_ioctl \
	        --redefine-sym mmap=harness_fb_mmap $< $@
	@if nm -u $@ | grep -Eqw 'open|open64|ioctl|mmap|mmap64'; then \
	    echo "display manager still calls the real open/ioctl/mmap:"; nm -u $@; rm -f $@; exit 1; fi

$(TOUCH_OBJ): $(call obj_of,$(TOUCH_SRC))
	objcopy --redefine-sym open=harness_fb_open --redefine-sym ioctl=harness_fb_ioctl $< $@
	@if nm -u $@ | grep -Eqw 'open|open64|ioctl'; then \
	    echo "touch reader still calls the real open/ioctl:"; nm -u $@; rm -f $@; exit 1; fi

$(APPS_JOBS_OBJ): $(call obj_of,$(APPS_JOBS_SRC))
	objcopy $$(nm -u $< | awk '$$2 ~ /^$(APPS_BACKEND_SYMS)/ { t = $$2; sub(/12apps_backend/, "12harness_apps", t); \
	        printf "--redefine-sym %s=%s ", $$2, t }') $< $@
	@if nm -u $@ | grep -Eq '$(APPS_BACKEND_SYMS)' || ! nm -u $@ | grep -q '_ZN12harness_apps3runE'; then \
	    echo "Settings > Apps workers still call the real Store backend:"; nm -u $@; rm -f $@; exit 1; fi

# LVGL (and the launcher's LVGL FreeType override): LVGL's own include set.
$(OBJ)/SDK/%.c.o: $(ROOT)/SDK/%.c
	@mkdir -p $(@D)
	$(CC) -std=gnu11 $(OPT) -w $(LV_DEFS) $(LVGL_PRIV) $(LV_INC) -MMD -MP -c $< -o $@
$(OBJ)/SDK/%.cpp.o: $(ROOT)/SDK/%.cpp
	@mkdir -p $(@D)
	$(CXX) -std=gnu++17 $(OPT) -w $(LV_DEFS) $(LVGL_PRIV) $(LV_INC) -MMD -MP -c $< -o $@
$(OBJ)/projects/APPLaunch/main/lvgl/%.c.o: $(PROJECT)/main/lvgl/%.c
	@mkdir -p $(@D)
	$(CC) -std=gnu11 $(OPT) -w $(LV_DEFS) $(LVGL_PRIV) $(LV_INC) -MMD -MP -c $< -o $@

# Launcher, cp0_lvgl and harness sources.
$(OBJ)/%.c.o: $(ROOT)/%.c
	@mkdir -p $(@D)
	$(CC) -std=gnu11 $(OPT) $(LV_DEFS) $(APP_DEFS) $(APP_INC) -MMD -MP -c $< -o $@
$(OBJ)/%.cpp.o: $(ROOT)/%.cpp
	@mkdir -p $(@D)
	$(CXX) -std=gnu++17 $(OPT) $(LV_DEFS) $(APP_DEFS) $(APP_INC) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(ALL_OBJS:.o=.d)
