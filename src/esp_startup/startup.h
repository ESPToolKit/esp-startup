#pragma once

#include <ArduinoJson.h>

#include <ESPWorker.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "startup_status.h"

struct ESPStartupConfig {
    ESPWorker* worker = nullptr;
    TickType_t waitTicks = pdMS_TO_TICKS(500);
    const char* workerName = "startup-flow";
    size_t workerStackSizeBytes = 6 * 1024;
    std::function<void()> onStarted;
    std::function<void()> onReady;
    std::function<void()> onFailed;
    std::function<void()> onDeferredFailure;
    std::function<void(const StartupStatusSnapshot& snapshot)> onSnapshot;
};

class ESPStartup {
   public:
    using StepCallback = std::function<bool()>;
    using ReadyCheck = std::function<bool()>;
    using WaitCallback = std::function<void(TickType_t waitTicks)>;
    using Config = ESPStartupConfig;

    class StepHandle {
       public:
        StepHandle() = default;
        StepHandle(ESPStartup* startupReference, size_t stepIndexValue);
        StepHandle& after(const char* dependencyStepName);

       private:
        ESPStartup* startup = nullptr;
        size_t stepIndex = 0;
    };

    class SectionHandle {
       public:
        SectionHandle() = default;
        SectionHandle(ESPStartup* startupReference, size_t sectionIndexValue);
        SectionHandle& readiness(const ReadyCheck& readyCheck, const WaitCallback& waitCallback = {});

       private:
        ESPStartup* startup = nullptr;
        size_t sectionIndex = 0;
    };

    bool configure(const ESPStartupConfig& configValue);
    void clear();
    bool init(std::initializer_list<const char*> sectionNames = {});

    SectionHandle section(const char* sectionName);
    StepHandle addTo(const char* sectionName, const char* stepName, const StepCallback& callback);
    StepHandle add(const char* stepName, const StepCallback& callback);

    bool start();
    void stop();

    StartupStatusSnapshot snapshot() const;
    JsonDocument snapshotJson() const;

   private:
    struct SectionDefinition {
        std::string name;
        ReadyCheck readyCheck;
        WaitCallback waitCallback;
    };

    struct StepDefinition {
        std::string name;
        size_t sectionIndex = 0;
        StepCallback callback;
        std::vector<std::string> dependencies;
    };

    ESPStartupConfig config = {};
    std::vector<SectionDefinition> sections = {};
    std::vector<StepDefinition> steps = {};
    std::vector<std::vector<size_t>> sectionOrder = {};
    std::vector<bool> sectionCompleted = {};

    std::atomic<bool> running = false;
    std::shared_ptr<WorkerHandler> workerHandler = nullptr;

    mutable std::mutex snapshotMutex;
    StartupStatusSnapshot startupSnapshot = {};

    size_t ensureSection(const char* sectionName);
    void setSectionReadiness(size_t sectionIndex, const ReadyCheck& readyCheck, const WaitCallback& waitCallback);

    void addDependency(size_t stepIndex, const char* dependencyStepName);
    bool dependencyExists(const char* dependencyName) const;
    int sectionRank(size_t sectionIndex) const;

    bool validateAndResolve(std::string& outErrorMessage);
    bool buildSectionOrder(
        size_t sectionIndex,
        std::vector<size_t>& outOrder,
        std::string& outErrorMessage
    ) const;

    void loop();
    bool waitForSection(size_t sectionIndex);
    bool runSection(size_t sectionIndex, bool deferredFailure);
    bool runStep(size_t stepIndex, bool deferredFailure);

    void publishSnapshot();
    void setStatus(StartupStatusKind status);
    void setStatusWithActiveStep(StartupStatusKind status, const char* stepName);
    void updateReadyFlags();
    void setFailure(const char* code, const char* message, const char* stepName);

    bool hasSection(const char* sectionName) const;
    bool isSectionCompleted(const char* sectionName) const;
    StartupStatusKind waitingStatusForSection(const std::string& sectionName) const;
};
