#pragma once

#include <Arduino.h>

#include "config.h"

namespace portal {

// Config access point with captive portal. Serves the settings page on 192.168.4.1.
void startAp(RepeaterConfig &cfg, const String &apSsid);
void stop();
void loop();

bool rebootRequested();    // set after settings were saved or reset
uint32_t lastActivityMs(); // last HTTP request or AP client connected
uint8_t apClients();
// Mode chosen on the web page; returns true once per request. The main loop applies it.
bool takeModeRequest(RepeaterMode &mode);

} // namespace portal
