/*
 * SPDX-License-Identifier: MIT
 */

#include "cp0_lvgl_app_runner.hpp"
#include "lanscan.hpp"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return cp0_lvgl_run_app<UILanScanPage>();
}
