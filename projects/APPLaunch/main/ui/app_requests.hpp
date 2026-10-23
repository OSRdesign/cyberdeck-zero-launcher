/*
 * SPDX-License-Identifier: MIT
 *
 * What the launcher does for apps outside its own screens (report 035): the board.state publisher and the launcher.sock
 * service, started together. Device builds only; elsewhere these calls do nothing.
 */
#pragma once

#include <string>

namespace app_requests {

/* Starts the board.state publisher thread (written at start, again after the first sound-server probe, then when a
 * fact changes: re-probed every 30 s), the launcher.sock server and the media worker (app_media.hpp). After the audio,
 * settings, Wi-Fi and Bluetooth services exist; stop() before they are torn down. Twice does nothing. */
void start();
void stop();

/* The reply of `hello` for a client, with the verbs of the board; exposed for the tests. */
std::string hello_reply(const std::string &launcher_version, const std::string &app, const std::string &verbs);

} // namespace app_requests
