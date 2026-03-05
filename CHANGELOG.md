# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- Standard ESPToolKit repository layout (metadata, workflows, issue templates, examples).
- Single public include entrypoint via `src/ESPStartup.h`.
- `ESPStartupConfig` as the canonical startup configuration type.

### Changed
- Internal sources moved under `src/esp_startup` to match ESPToolKit layout conventions.

## [1.0.0] - 2026-03-05
### Added
- Initial ESPStartup orchestration engine for staged boot flows.
- Section-based startup ordering with dependency validation and cycle checks.
- Deferred startup execution through ESPWorker.
- Startup status snapshot helpers and JSON serialization.
