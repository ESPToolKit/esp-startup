#include "esp_startup/startup.h"

#include <Arduino.h>

#include <freertos/task.h>
#include <utility>

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
    if( sectionIndex >= sectionBatches.size() ){
        setFailure("startup_invalid", "section index out of bounds", nullptr);
        if( config.onFailed ){
            config.onFailed();
        }
        running.store(false);
        return false;
    }

    const std::vector<std::vector<size_t>>& batches = sectionBatches[sectionIndex];
    for( size_t batchIndex = 0; batchIndex < batches.size(); batchIndex++ ){
        if( !running.load() ){
            return false;
        }

        const std::vector<size_t>& batch = batches[batchIndex];
        if( !config.enableParallelInit ){
            for( size_t index = 0; index < batch.size(); index++ ){
                if( !running.load() ){
                    return false;
                }
                if( !runStep(batch[index], deferredFailure) ){
                    return false;
                }
            }
            continue;
        }

        std::vector<size_t> parallelEligible;
        std::vector<size_t> sequentialOnly;
        parallelEligible.reserve(batch.size());
        sequentialOnly.reserve(batch.size());

        for( size_t index = 0; index < batch.size(); index++ ){
            const size_t stepIndex = batch[index];
            if( stepIndex >= steps.size() ){
                setFailure("startup_invalid", "step index out of bounds", nullptr);
                if( config.onFailed ){
                    config.onFailed();
                }
                running.store(false);
                return false;
            }

            const StepDefinition& step = steps[stepIndex];
            if( step.dependencies.empty() || step.parallelSafe ){
                parallelEligible.push_back(stepIndex);
            } else {
                sequentialOnly.push_back(stepIndex);
            }
        }

        if( parallelEligible.size() >= 2 ){
            if( config.worker == nullptr ){
                setFailure("startup_invalid", "parallel init requires worker", nullptr);
                if( config.onFailed ){
                    config.onFailed();
                }
                running.store(false);
                return false;
            }

            if( !runParallelBatch(parallelEligible, deferredFailure) ){
                return false;
            }
        } else if( parallelEligible.size() == 1 ){
            if( !runStep(parallelEligible[0], deferredFailure) ){
                return false;
            }
        }

        for( size_t index = 0; index < sequentialOnly.size(); index++ ){
            if( !running.load() ){
                return false;
            }

            if( !runStep(sequentialOnly[index], deferredFailure) ){
                return false;
            }
        }
    }

    return true;
}

bool ESPStartup::runParallelBatch(const std::vector<size_t>& batch, bool deferredFailure) {
    if( batch.empty() ){
        return true;
    }

    if( config.worker == nullptr ){
        setFailure("startup_invalid", "parallel init requires worker", nullptr);
        if( config.onFailed ){
            config.onFailed();
        }
        running.store(false);
        return false;
    }

    setStatus(StartupStatusKind::Running);

    std::atomic<bool> failureTriggered = false;
    std::mutex resultMutex;
    std::vector<std::pair<size_t, bool>> results;
    results.reserve(batch.size());
    std::vector<std::shared_ptr<WorkerHandler>> handlers;
    handlers.reserve(batch.size());

    const char* failureCode = deferredFailure ? "deferred_init_failed" : "core_init_failed";

    auto triggerFailure = [&](const char* code, const char* message, const char* stepName) {
        bool expected = false;
        if( !failureTriggered.compare_exchange_strong(expected, true) ){
            return;
        }

        setFailure(code, message, stepName);
        if( config.onFailed ){
            config.onFailed();
        }
        if( deferredFailure && config.onDeferredFailure ){
            config.onDeferredFailure();
        }

        running.store(false);
    };

    for( size_t index = 0; index < batch.size(); index++ ){
        if( !running.load() || failureTriggered.load() ){
            break;
        }

        const size_t stepIndex = batch[index];
        if( stepIndex >= steps.size() ){
            triggerFailure("startup_invalid", "step index out of bounds", nullptr);
            break;
        }

        WorkerConfig workerConfig;
        workerConfig.stackSizeBytes = config.workerStackSizeBytes;
        if( config.workerName != nullptr ){
            workerConfig.name = std::string(config.workerName) + "-parallel";
        } else {
            workerConfig.name = "startup-parallel";
        }

        WorkerResult workerResult = config.worker->spawnExt(
            [this, stepIndex, &resultMutex, &results, &triggerFailure, failureCode]() {
                bool stepOk = false;
                if( stepIndex < steps.size() && steps[stepIndex].callback ){
                    stepOk = steps[stepIndex].callback();
                }

                {
                    std::lock_guard<std::mutex> lock(resultMutex);
                    results.push_back({stepIndex, stepOk});
                }

                if( !stepOk ){
                    const char* stepName = stepIndex < steps.size() ? steps[stepIndex].name.c_str() : nullptr;
                    triggerFailure(failureCode, "startup step failed", stepName);
                }
            },
            workerConfig
        );

        if( workerResult.error != WorkerError::None || workerResult.handler == nullptr ){
            const char* stepName = steps[stepIndex].name.c_str();
            triggerFailure("startup_start_failed", "failed to start parallel startup step", stepName);
            break;
        }

        handlers.push_back(workerResult.handler);
    }

    for( size_t index = 0; index < handlers.size(); index++ ){
        std::shared_ptr<WorkerHandler> handler = handlers[index];
        if( handler == nullptr ){
            continue;
        }

        const bool workerStopped = handler->wait(pdMS_TO_TICKS(1000));
        if( !workerStopped ){
            handler->destroy();
            handler->wait(pdMS_TO_TICKS(250));
        }
    }

    std::vector<std::pair<size_t, bool>> collected;
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        collected = results;
    }

    for( size_t index = 0; index < collected.size(); index++ ){
        if( !collected[index].second ){
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            startupSnapshot.completedUnits++;
            startupSnapshot.updatedAtMs = millis();
        }
        publishSnapshot();
    }

    if( failureTriggered.load() ){
        return false;
    }

    return running.load();
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
