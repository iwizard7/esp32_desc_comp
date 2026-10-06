#pragma once

#include <Arduino.h>

void displayBegin();
void displaySetFlip(bool flip);
void displaySetContrast(uint8_t contrast);
void displayApplyTheme();
void displayMessage(const char* line1, const char* line2 = "", const char* line3 = "");
void displayApInfo(const char* ssid, const char* pass);
void displayRebuildPlaylist();
void displayAdvance(bool animate = true);
uint8_t displayCurrentSlideId();
uint32_t displayCurrentDurationMs();
void displayShowCurrent();
void displayClock();
