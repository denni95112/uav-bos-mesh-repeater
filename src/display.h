#pragma once

#include <Arduino.h>

#include "config.h"
#include "mesh.h"

namespace display {

void begin();
void setMode(RepeaterMode mode);
bool on();
void setOn(bool on);
void toggle();

void showBoot(const char *version);
void showStatus(const MeshStats &mesh, int apClients);
void showModeSelect(RepeaterMode selected, RepeaterMode current, uint32_t remainMs);

} // namespace display
