#include <Arduino.h>
#include <freertos/task.h>

#include <ESPStartup.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPStartup startup;

unsigned long bootAtMs = 0;

bool coreInit() {
    Serial.println("core init done");
    return true;
}

bool cloudInit() {
    Serial.println("cloud init done");
    return true;
}

void setup() {
    Serial.begin(115200);
    bootAtMs = millis();

    worker.init(ESPWorker::Config{});

    ESPStartupConfig cfg{};
    cfg.worker = &worker;
    cfg.waitTicks = pdMS_TO_TICKS(200);
    cfg.onSnapshot = [](const StartupStatusSnapshot& snapshot) {
        Serial.printf(
            "status=%s completed=%u/%u\n",
            startupStatusToString(snapshot.status),
            snapshot.completedUnits,
            snapshot.totalUnits
        );
    };

    startup.configure(cfg);
    startup.init({"core", "network"});

    startup.section("network").readiness(
        []() { return millis() - bootAtMs >= 3000; },
        [](TickType_t waitTicks) { vTaskDelay(waitTicks); }
    );

    startup.addTo("core", "core", coreInit);
    startup.addTo("network", "cloud", cloudInit).after("core");

    startup.start();
}

void loop() {
    delay(1000);
}
