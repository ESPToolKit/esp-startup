#include <Arduino.h>

#include <ESPStartup.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPStartup startup;

bool initStorage() {
    Serial.println("storage ready");
    delay(400);
    return true;
}

bool initLogger() {
    Serial.println("logger ready");
    delay(300);
    return true;
}

bool initCache() {
    Serial.println("cache ready");
    delay(250);
    return true;
}

bool initTelemetry() {
    Serial.println("telemetry ready");
    return true;
}

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});

    ESPStartupConfig cfg{};
    cfg.worker = &worker;
    cfg.enableParallelInit = true;
    cfg.onReady = []() { Serial.println("startup ready"); };
    cfg.onFailed = []() { Serial.println("startup failed"); };

    startup.configure(cfg);
    startup.init({"core"});

    // No dependencies: eligible for parallel init when enabled.
    startup.addTo("core", "storage", initStorage);
    startup.addTo("core", "logger", initLogger);

    // Has dependency but explicitly safe to run in parallel once dependency is resolved.
    startup.addTo("core", "cache", initCache).after("storage").parallelSafe();

    // Remains sequential because it has a dependency and is not marked safe.
    startup.addTo("core", "telemetry", initTelemetry).after("storage");

    if( !startup.start() ) {
        Serial.println("failed to start startup flow");
    }
}

void loop() {
    delay(1000);
}
