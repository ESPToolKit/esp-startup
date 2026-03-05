#include "esp_startup/startup.h"

#include <Arduino.h>

ESPStartup::StepHandle::StepHandle(ESPStartup* startupReference, size_t stepIndexValue)
    : startup(startupReference), stepIndex(stepIndexValue) {}

ESPStartup::StepHandle& ESPStartup::StepHandle::after(const char* dependencyStepName) {
    if( startup != nullptr ){
        startup->addDependency(stepIndex, dependencyStepName);
    }
    return *this;
}

ESPStartup::SectionHandle::SectionHandle(ESPStartup* startupReference, size_t sectionIndexValue)
    : startup(startupReference), sectionIndex(sectionIndexValue) {}

ESPStartup::SectionHandle& ESPStartup::SectionHandle::readiness(
    const ReadyCheck& readyCheck,
    const WaitCallback& waitCallback
) {
    if( startup != nullptr ){
        startup->setSectionReadiness(sectionIndex, readyCheck, waitCallback);
    }
    return *this;
}

bool ESPStartup::configure(const ESPStartupConfig& configValue) {
    config = configValue;
    return true;
}

void ESPStartup::clear() {
    stop();

    sections.clear();
    steps.clear();
    sectionOrder.clear();
    sectionCompleted.clear();

    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        startupSnapshot = StartupStatusSnapshot{};
        startupSnapshot.updatedAtMs = millis();
    }
    publishSnapshot();
}

bool ESPStartup::init(std::initializer_list<const char*> sectionNames) {
    if( running.load() ){
        return false;
    }

    sections.clear();
    sectionOrder.clear();
    sectionCompleted.clear();

    for( const char* sectionName : sectionNames ){
        ensureSection(sectionName);
    }

    if( sections.empty() ){
        ensureSection("default");
    }

    return true;
}

ESPStartup::SectionHandle ESPStartup::section(const char* sectionName) {
    const size_t sectionIndex = ensureSection(sectionName);
    return SectionHandle(this, sectionIndex);
}

ESPStartup::StepHandle ESPStartup::addTo(
    const char* sectionName,
    const char* stepName,
    const StepCallback& callback
) {
    const size_t sectionIndex = ensureSection(sectionName);

    StepDefinition definition;
    definition.sectionIndex = sectionIndex;
    if( stepName != nullptr ){
        definition.name = stepName;
    }
    definition.callback = callback;

    steps.push_back(definition);
    return StepHandle(this, steps.size() - 1);
}

ESPStartup::StepHandle ESPStartup::add(const char* stepName, const StepCallback& callback) {
    if( sections.empty() ){
        ensureSection("default");
    }
    return addTo(sections[0].name.c_str(), stepName, callback);
}

bool ESPStartup::start() {
    stop();

    std::string validationError;
    if( !validateAndResolve(validationError) ){
        setFailure("startup_invalid", validationError.c_str(), nullptr);
        if( config.onFailed ){
            config.onFailed();
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        startupSnapshot = StartupStatusSnapshot{};
        startupSnapshot.status = StartupStatusKind::Starting;
        startupSnapshot.totalUnits = static_cast<uint16_t>(steps.size());
        startupSnapshot.updatedAtMs = millis();
    }
    publishSnapshot();

    sectionCompleted.assign(sections.size(), false);
    running.store(true);

    if( !runSection(0, false) ){
        running.store(false);
        return false;
    }

    sectionCompleted[0] = true;
    updateReadyFlags();

    if( config.onStarted ){
        config.onStarted();
    }

    if( sections.size() == 1 ){
        setStatus(StartupStatusKind::Ready);
        if( config.onReady ){
            config.onReady();
        }
        running.store(false);
        return true;
    }

    if( config.worker == nullptr ){
        setFailure("startup_invalid", "worker is not configured", nullptr);
        if( config.onFailed ){
            config.onFailed();
        }
        running.store(false);
        return false;
    }

    WorkerConfig workerConfig;
    workerConfig.name = config.workerName == nullptr ? "startup-flow" : config.workerName;
    workerConfig.stackSizeBytes = config.workerStackSizeBytes;

    WorkerResult workerResult = config.worker->spawnExt(
        std::bind(&ESPStartup::loop, this),
        workerConfig
    );

    if( workerResult.error != WorkerError::None || workerResult.handler == nullptr ){
        workerHandler.reset();
        running.store(false);
        setFailure("startup_start_failed", "failed to start startup worker", nullptr);
        if( config.onFailed ){
            config.onFailed();
        }
        return false;
    }

    workerHandler = workerResult.handler;
    return true;
}

void ESPStartup::stop() {
    const bool wasRunning = running.exchange(false);
    const bool hasWorker = workerHandler != nullptr;

    if( !wasRunning && !hasWorker ){
        return;
    }

    if( workerHandler != nullptr ){
        const bool workerStopped = workerHandler->wait(pdMS_TO_TICKS(1000));
        if( !workerStopped ){
            workerHandler->destroy();
            workerHandler->wait(pdMS_TO_TICKS(250));
        }
        workerHandler.reset();
    }

    setStatus(StartupStatusKind::Shutdown);
}

StartupStatusSnapshot ESPStartup::snapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    return startupSnapshot;
}

JsonDocument ESPStartup::snapshotJson() const {
    return startupStatusToJson(snapshot());
}

size_t ESPStartup::ensureSection(const char* sectionName) {
    const std::string name = sectionName == nullptr ? "" : sectionName;

    for( size_t index = 0; index < sections.size(); index++ ){
        if( sections[index].name == name ){
            return index;
        }
    }

    SectionDefinition definition;
    definition.name = name;
    sections.push_back(definition);
    return sections.size() - 1;
}

void ESPStartup::setSectionReadiness(
    size_t sectionIndex,
    const ReadyCheck& readyCheck,
    const WaitCallback& waitCallback
) {
    if( sectionIndex >= sections.size() ){
        return;
    }

    sections[sectionIndex].readyCheck = readyCheck;
    sections[sectionIndex].waitCallback = waitCallback;
}

void ESPStartup::addDependency(size_t stepIndex, const char* dependencyStepName) {
    if( dependencyStepName == nullptr || stepIndex >= steps.size() ){
        return;
    }

    steps[stepIndex].dependencies.emplace_back(dependencyStepName);
}
