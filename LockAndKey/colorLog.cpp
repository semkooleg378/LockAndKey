#include "colorLog.h"
#include <SPIFFS.h>
#include <Arduino.h>

static SemaphoreHandle_t logMutex = nullptr;

// Custom log prefix function with colors and timestamps
static void logPrefix(Print *_logOutput, int logLevel) {
    const char *colorReset = "\x1B[0m";
    const char *colorFatal = "\x1B[31m";    // Red
    const char *colorError = "\x1B[91m";    // Light Red
    const char *colorWarning = "\x1B[93m";  // Yellow
    const char *colorNotice = "\x1B[94m";   // Light Blue
    const char *colorTrace = "\x1B[92m";    // Green
    const char *colorVerbose = "\x1B[96m";  // Light Cyan

    switch (logLevel) {
        case 0: _logOutput->print("S: "); break;
        case 1: _logOutput->print(colorFatal); _logOutput->print("F: "); break;
        case 2: _logOutput->print(colorError); _logOutput->print("E: "); break;
        case 3: _logOutput->print(colorWarning); _logOutput->print("W: "); break;
        case 4: _logOutput->print(colorNotice); _logOutput->print("N: "); break;
        case 5: _logOutput->print(colorTrace); _logOutput->print("T: "); break;
        case 6: _logOutput->print(colorVerbose); _logOutput->print("V: "); break;
        default: _logOutput->print("?: "); break;
    }

    _logOutput->print(millis());
    _logOutput->print(": ");
}

static void logSuffix(Print *_logOutput, int logLevel) {
    const char *colorReset = "\x1B[0m";
    _logOutput->print(colorReset); // Reset color
    _logOutput->println(); // Add newline
}

static void initializeLogMutex() {
    if (logMutex == nullptr) {
        logMutex = xSemaphoreCreateMutex();
        if (logMutex == nullptr) {
            Serial.println(F("Failed to create log mutex"));
        }
    }
}

void logColor(LColor color, const __FlashStringHelper *format, ...) {
    std::string colorCode;
    auto time = millis();
    initializeLogMutex();

    switch (color) {
        case LColor::Reset:
            colorCode = "\x1B[0m";
            break;
        case LColor::Red:
            colorCode = "\x1B[31m";
            break;
        case LColor::LightRed:
            colorCode = "\x1B[91m";
            break;
        case LColor::Yellow:
            colorCode = "\x1B[93m";
            break;
        case LColor::LightBlue:
            colorCode = "\x1B[94m";
            break;
        case LColor::Green:
            colorCode = "\x1B[92m";
            break;
        case LColor::LightCyan:
            colorCode = "\x1B[96m";
            break;
        default:
            colorCode = "\x1B[0m";
            break;
    }
    xSemaphoreTake(logMutex, portMAX_DELAY);
    Serial.print(time);
    Serial.print("ms: ");
    Serial.print(colorCode.c_str());

    // Create a buffer to hold the formatted string
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf_P(buffer, sizeof(buffer), reinterpret_cast<const char *>(format), args);
    va_end(args);

    Serial.print(buffer);
    Serial.print("\x1B[0m");  // Reset color
    Serial.println();
    xSemaphoreGive(logMutex);
}

void logColorBegin ()
{
    SPIFFS.begin ();
    Serial.begin(115200);
    while (!Serial);

    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    Log.setPrefix(&logPrefix);
    Log.setSuffix(&logSuffix);

}

int lastMemoryLog = 0;

void logMemory(const std::string &prefix) {
    int currentMemory = esp_get_free_heap_size();
    int diff = currentMemory - lastMemoryLog;
    logColor(LColor::Yellow, F("%s Current free heap memory: %d bytes, change: %d bytes"), prefix.c_str(), currentMemory, diff);
    lastMemoryLog = currentMemory;
}
