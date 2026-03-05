#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

enum class StartupStatusKind : uint8_t {
    Starting = 0,
    WaitingNetwork = 1,
    WaitingDateTime = 2,
    WaitingSection = 3,
    Running = 4,
    Ready = 5,
    Failed = 6,
    Shutdown = 7,
};

struct StartupReadyFlags {
    bool core = false;
    bool network = false;
    bool dateTime = false;
    bool full = false;
};

struct StartupErrorInfo {
    bool hasError = false;
    char code[48] = {};
    char message[96] = {};
    char unit[48] = {};
};

struct StartupStatusSnapshot {
    StartupStatusKind status = StartupStatusKind::Starting;
    char activeUnit[48] = {};
    uint16_t completedUnits = 0;
    uint16_t totalUnits = 0;
    StartupReadyFlags readyFlags = {};
    StartupErrorInfo error = {};
    uint32_t updatedAtMs = 0;
};

[[nodiscard]] const char* startupStatusToString(StartupStatusKind status);
void startupCopyText(const char* sourceText, char* destination, size_t destinationSize);
JsonDocument startupStatusToJson(const StartupStatusSnapshot& snapshot);
