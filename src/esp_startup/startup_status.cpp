#include "esp_startup/startup_status.h"

#include <cstdio>

const char* startupStatusToString(StartupStatusKind status) {
    if( status == StartupStatusKind::Starting ){
        return "starting";
    }
    if( status == StartupStatusKind::WaitingNetwork ){
        return "waiting_network";
    }
    if( status == StartupStatusKind::WaitingDateTime ){
        return "waiting_datetime";
    }
    if( status == StartupStatusKind::WaitingSection ){
        return "waiting_section";
    }
    if( status == StartupStatusKind::Running ){
        return "running";
    }
    if( status == StartupStatusKind::Ready ){
        return "ready";
    }
    if( status == StartupStatusKind::Failed ){
        return "failed";
    }
    if( status == StartupStatusKind::Shutdown ){
        return "shutdown";
    }
    return "unknown";
}

void startupCopyText(const char* sourceText, char* destination, size_t destinationSize) {
    if( destination == nullptr || destinationSize == 0 ){
        return;
    }

    if( sourceText == nullptr ){
        destination[0] = '\0';
        return;
    }

    std::snprintf(destination, destinationSize, "%s", sourceText);
}

JsonDocument startupStatusToJson(const StartupStatusSnapshot& snapshot) {
    JsonDocument document;
    document["status"] = startupStatusToString(snapshot.status);

    if( snapshot.activeUnit[0] == '\0' ){
        document["activeUnit"] = nullptr;
    } else {
        document["activeUnit"] = snapshot.activeUnit;
    }

    document["completedUnits"] = snapshot.completedUnits;
    document["totalUnits"] = snapshot.totalUnits;

    JsonObject readyFlags = document["readyFlags"].to<JsonObject>();
    readyFlags["core"] = snapshot.readyFlags.core;
    readyFlags["network"] = snapshot.readyFlags.network;
    readyFlags["dateTime"] = snapshot.readyFlags.dateTime;
    readyFlags["full"] = snapshot.readyFlags.full;

    if( !snapshot.error.hasError ){
        document["error"] = nullptr;
    } else {
        JsonObject error = document["error"].to<JsonObject>();
        error["code"] = snapshot.error.code;
        error["message"] = snapshot.error.message;
        error["unit"] = snapshot.error.unit;
    }

    document["updatedAtMs"] = snapshot.updatedAtMs;
    return document;
}
