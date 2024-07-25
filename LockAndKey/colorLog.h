#ifndef COLORLOG_H
#define COLORLOG_H


#include <Arduino.h>
#include "ArduinoLog.h"
#include <string>

enum class LColor {
    Reset,
    Red,
    LightRed,
    Yellow,
    LightBlue,
    Green,
    LightCyan
};

void logColor(LColor color, const __FlashStringHelper* format, ...);
void logColorBegin ();
void logMemory(const std::string &prefix = "");
#endif


