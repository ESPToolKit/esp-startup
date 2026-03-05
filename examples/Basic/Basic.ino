#include <Arduino.h>

#include <ESPStartup.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPStartup startup;

bool initLogger() {
    Serial.println("logger initialized");
    return true;
}

bool initSensors() {
    Serial.println("sensors initialized");
    return true;
}

bool initNetworking() {
    Serial.println("network initialized");
    return true;
}

void setup() {
    Serial.begin(115200);

    worker.init(ESPWorker::Config{});

    ESPStartupConfig cfg{};
    cfg.worker = &worker;
    cfg.onReady = []() { Serial.println("startup ready"); };
    cfg.onFailed = []() { Serial.println("startup failed"); };

    startup.configure(cfg);
    startup.init({"core", "network"});

    startup.addTo("core", "logger", initLogger);
    startup.addTo("core", "sensors", initSensors).after("logger");
    startup.addTo("network", "wifi", initNetworking).after("sensors");

    if( !startup.start() ) {
        Serial.println("failed to start startup flow");
    }
}

void loop() {
    delay(1000);
}
