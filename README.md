# ESPStartup

ESPStartup is a startup orchestration library for ESP32 applications built with ESPToolKit. It lets you split boot logic into ordered sections, validate step dependencies, and move deferred initialization to `ESPWorker`.

## CI / Release / License
[![CI](https://github.com/ESPToolKit/esp-startup/actions/workflows/ci.yml/badge.svg)](https://github.com/ESPToolKit/esp-startup/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/ESPToolKit/esp-startup?sort=semver)](https://github.com/ESPToolKit/esp-startup/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.md)

## Features
- Section-based startup flow (`core`, `network`, `datetime`, or custom).
- Dependency ordering via `.after("step")` with validation.
- Optional parallel init inside a section for independent or explicitly safe steps.
- Blocking first section, deferred sections run in `ESPWorker`.
- Readiness gates for deferred sections.
- Snapshot and JSON export helpers for diagnostics.

## Installation
- PlatformIO: add `https://github.com/ESPToolKit/esp-startup.git` to `lib_deps`.
- Arduino IDE: install as ZIP from this repository.

Dependencies:
- `ESPWorker`
- `ArduinoJson`

## Single Include
Use a single public include:

```cpp
#include <ESPStartup.h>
```

## Quick Start

```cpp
#include <ESPStartup.h>
#include <ESPWorker.h>

ESPWorker worker;
ESPStartup startup;

void setup() {
    worker.init(ESPWorker::Config{});

    ESPStartupConfig cfg{};
    cfg.worker = &worker;
    cfg.enableParallelInit = true;
    cfg.onReady = []() { Serial.println("startup ready"); };
    cfg.onFailed = []() { Serial.println("startup failed"); };

    startup.configure(cfg);
    startup.init({"core", "network"});

    startup.addTo("core", "logger", []() { return true; }).parallelSafe();
    startup.addTo("core", "storage", []() { return true; }); // no deps => auto parallel eligible
    startup.addTo("network", "wifi", []() { return true; }).after("logger");

    startup.start();
}
```

## Readiness Gate Example

```cpp
startup.section("network").readiness(
    []() { return networkIsConnected(); },
    [](TickType_t waitTicks) { vTaskDelay(waitTicks); }
);
```

If no wait callback is supplied, ESPStartup uses `vTaskDelay(waitTicks)`.

## Parallel Init Rules
- Parallelism is section-scoped only.
- A step is parallel-eligible if it has no dependencies, or it is marked with `.parallelSafe(true)`.
- If parallel init is enabled and a wave has 2+ eligible steps, `ESPStartupConfig::worker` is required.
- On failure, ESPStartup stops scheduling new steps, waits for already-started parallel steps, then returns failure.

## API Summary
- `bool configure(const ESPStartupConfig& config)`
- `bool init(std::initializer_list<const char*> sectionNames = {})`
- `SectionHandle section(const char* sectionName)`
- `StepHandle addTo(const char* sectionName, const char* stepName, const StepCallback& callback)`
- `StepHandle add(const char* stepName, const StepCallback& callback)`
- `StepHandle::parallelSafe(bool enabled = true)`
- `bool start()`
- `void stop()`
- `StartupStatusSnapshot snapshot() const`
- `JsonDocument snapshotJson() const`

`ESPStartupConfig` fields:
- `ESPWorker* worker`
- `TickType_t waitTicks`
- `const char* workerName`
- `size_t workerStackSizeBytes`
- `bool enableParallelInit`
- `std::function<void()> onStarted`
- `std::function<void()> onReady`
- `std::function<void()> onFailed`
- `std::function<void()> onDeferredFailure`
- `std::function<void(const StartupStatusSnapshot&)> onSnapshot`

## Validation Rules
- Step names must be unique.
- Missing dependencies fail startup validation.
- Dependency cycles fail startup validation.
- A step cannot depend on a step in a future section.

## Examples
- `examples/Basic` - basic multi-section startup flow.
- `examples/SectionReadiness` - delayed section execution with readiness checks.
- `examples/ParallelInit` - parallel init of independent and safe steps.

## License
MIT - see [LICENSE.md](LICENSE.md).

## ESPToolKit
- Repositories: <https://github.com/orgs/ESPToolKit/repositories>
- Discord: <https://discord.gg/WG8sSqAy>
- Support: <https://ko-fi.com/esptoolkit>
- Website: <https://www.esptoolkit.hu/>
