#include "esp_startup/startup.h"

#include <Arduino.h>

#include <freertos/task.h>

void ESPStartup::loop() {
    for( size_t sectionIndex = 1; sectionIndex < sections.size(); sectionIndex++ ){
        if( !running.load() ){
            return;
        }

        if( !waitForSection(sectionIndex) ){
            return;
        }

        if( !runSection(sectionIndex, true) ){
            running.store(false);
            return;
        }

        sectionCompleted[sectionIndex] = true;
        updateReadyFlags();
    }

    setStatus(StartupStatusKind::Ready);

    if( config.onReady ){
        config.onReady();
    }

    running.store(false);
}

bool ESPStartup::waitForSection(size_t sectionIndex) {
    if( sectionIndex >= sections.size() ){
        return false;
    }

    const SectionDefinition& sectionDefinition = sections[sectionIndex];
    if( !sectionDefinition.readyCheck ){
        return true;
    }

    bool waitingPublished = false;

    while( running.load() ){
        if( sectionDefinition.readyCheck() ){
            return true;
        }

        if( !waitingPublished ){
            setStatus(waitingStatusForSection(sectionDefinition.name));
            waitingPublished = true;
        }

        if( sectionDefinition.waitCallback ){
            sectionDefinition.waitCallback(config.waitTicks);
        } else {
            vTaskDelay(config.waitTicks);
        }
    }

    return false;
}

bool ESPStartup::runSection(size_t sectionIndex, bool deferredFailure) {
    if( sectionIndex >= sectionOrder.size() ){
        setFailure("startup_invalid", "section index out of bounds", nullptr);
        if( config.onFailed ){
            config.onFailed();
        }
        running.store(false);
        return false;
    }

    const std::vector<size_t>& order = sectionOrder[sectionIndex];
    for( size_t index = 0; index < order.size(); index++ ){
        if( !running.load() ){
            return false;
        }

        if( !runStep(order[index], deferredFailure) ){
            return false;
        }
    }

    return true;
}

bool ESPStartup::runStep(size_t stepIndex, bool deferredFailure) {
    if( stepIndex >= steps.size() ){
        setFailure("startup_invalid", "step index out of bounds", nullptr);
        if( config.onFailed ){
            config.onFailed();
        }
        running.store(false);
        return false;
    }

    const StepDefinition& step = steps[stepIndex];
    setStatusWithActiveStep(StartupStatusKind::Running, step.name.c_str());

    if( step.callback() ){
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            startupSnapshot.completedUnits++;
            startupSnapshot.updatedAtMs = millis();
        }
        publishSnapshot();
        return true;
    }

    setFailure(
        deferredFailure ? "deferred_init_failed" : "core_init_failed",
        "startup step failed",
        step.name.c_str()
    );

    if( config.onFailed ){
        config.onFailed();
    }

    if( deferredFailure && config.onDeferredFailure ){
        config.onDeferredFailure();
    }

    running.store(false);
    return false;
}

void ESPStartup::publishSnapshot() {
    if( !config.onSnapshot ){
        return;
    }

    config.onSnapshot(snapshot());
}

void ESPStartup::setStatus(StartupStatusKind status) {
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        startupSnapshot.status = status;
        startupSnapshot.activeUnit[0] = '\0';
        startupSnapshot.updatedAtMs = millis();
    }
    publishSnapshot();
}

void ESPStartup::setStatusWithActiveStep(StartupStatusKind status, const char* stepName) {
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        startupSnapshot.status = status;
        startupCopyText(stepName, startupSnapshot.activeUnit, sizeof(startupSnapshot.activeUnit));
        startupSnapshot.updatedAtMs = millis();
    }
    publishSnapshot();
}

void ESPStartup::updateReadyFlags() {
    const bool coreExists = hasSection("core");
    const bool networkExists = hasSection("network");
    const bool dateTimeExists = hasSection("datetime") || hasSection("dateTime");

    bool fullReady = true;
    for( size_t index = 0; index < sectionCompleted.size(); index++ ){
        if( !sectionCompleted[index] ){
            fullReady = false;
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        startupSnapshot.readyFlags.core =
            coreExists ? isSectionCompleted("core") : (!sectionCompleted.empty() && sectionCompleted[0]);
        startupSnapshot.readyFlags.network = networkExists ? isSectionCompleted("network") : true;
        startupSnapshot.readyFlags.dateTime =
            dateTimeExists ? (isSectionCompleted("datetime") || isSectionCompleted("dateTime")) : true;
        startupSnapshot.readyFlags.full = fullReady;
        startupSnapshot.updatedAtMs = millis();
    }

    publishSnapshot();
}

void ESPStartup::setFailure(const char* code, const char* message, const char* stepName) {
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        startupSnapshot.status = StartupStatusKind::Failed;
        startupSnapshot.error.hasError = true;
        startupCopyText(code, startupSnapshot.error.code, sizeof(startupSnapshot.error.code));
        startupCopyText(message, startupSnapshot.error.message, sizeof(startupSnapshot.error.message));
        startupCopyText(stepName, startupSnapshot.error.unit, sizeof(startupSnapshot.error.unit));
        startupSnapshot.updatedAtMs = millis();
    }

    publishSnapshot();
}

bool ESPStartup::hasSection(const char* sectionName) const {
    if( sectionName == nullptr ){
        return false;
    }

    for( size_t index = 0; index < sections.size(); index++ ){
        if( sections[index].name == sectionName ){
            return true;
        }
    }

    return false;
}

bool ESPStartup::isSectionCompleted(const char* sectionName) const {
    if( sectionName == nullptr ){
        return false;
    }

    for( size_t index = 0; index < sections.size(); index++ ){
        if( sections[index].name == sectionName ){
            return index < sectionCompleted.size() ? sectionCompleted[index] : false;
        }
    }

    return false;
}

StartupStatusKind ESPStartup::waitingStatusForSection(const std::string& sectionName) const {
    if( sectionName == "network" ){
        return StartupStatusKind::WaitingNetwork;
    }
    if( sectionName == "datetime" || sectionName == "dateTime" ){
        return StartupStatusKind::WaitingDateTime;
    }
    return StartupStatusKind::WaitingSection;
}
