#pragma once

#include <Arduino.h>

void portalBeginAp();
void portalBeginSta();
void portalLoop();
bool portalApActive();
String portalApSsid();
String portalApPass();
void portalStartConfigAp();
