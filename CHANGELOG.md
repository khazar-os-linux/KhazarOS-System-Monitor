# Changelog

All notable changes to KhazarOS System Monitor will be documented in this file.

## [Alpha 0.1.5] - 2026-06-06

### Added
- Apps tab with process grouping, search, and process management
- Global hotkey support (Ctrl+Shift+Esc) via GNOME GSettings
- `.gitignore` for version control
- `CHANGELOG.md`

### Changed
- **Binary Rename**: Executable named `system-monitor` renamed to `khos-system-monitor`
- **Modular Architecture**: Refactored monolithic `main.c` into separate UI modules:
  - `src/ui/ui_cpu.c` / `ui_cpu.h`
  - `src/ui/ui_memory.c` / `ui_memory.h`
  - `src/ui/ui_disk.c` / `ui_disk.h`
  - `src/ui/ui_network.c` / `ui_network.h`
  - `src/ui/ui_gpu.c` / `ui_gpu.h`
  - `src/ui/ui_app.c` / `ui_app.h`
  - `src/ui/ui_about.c` / `ui_about.h`
- **Common Utilities**: Extracted shared graph drawing code to `src/ui/utils/icon_cache.c` and `src/utils/hotkey.c`
- **Data Modules**: Separated data collection logic from UI:
  - `src/cpu/cpu_data.c`
  - `src/memory/memory_data.c`
  - `src/disk/disk_data.c`
  - `src/network/network_data.c`
  - `src/gpu/gpu_data.c`
- **Build System**: Updated `Makefile` to include new source files and `json-glib-1.0`
- **Configuration**: Centralized constants (MAX_POINTS, MAX_CPU_CORES, etc.) in `include/config.h`
- **Desktop Entry**: Aligned `Exec`, `Icon`, `StartupWMClass` in `.desktop` file for proper taskbar icon matching (KDE/GNOME)

### Fixed
- **Disk Data Parsing**: Replaced fragile manual `lsblk --json` parsing with robust `json-glib` library
- **Memory Calculation**: Corrected `used_memory` calculation from `total - free - buffers - cached` to `total - available`
- **Data Accuracy**: Improved CPU usage tracking with per-process jiffies tracking via `GHashTable`
- **App ID Consistency**: Fixed `g_application_id_is_valid` assertion crash by keeping D-Bus compatible app ID

### Removed
- **Dead Code**: Cleaned up `#if 0` blocks and unused Startup/Services tab remnants from `main.c`
- **Legacy Files**: Removed `src/minimal.c` and `src/cpu_monitor.c`
- **Tests**: Removed the `tests/test_gpu.c` standalone test (GPU test is now integrated)

## [0.1.0 beta] - Pre-Alpha

### Initial Release
- Basic system monitoring functionality
- CPU, Memory, Disk, Network, and GPU monitoring
- GTK3-based user interface
- Real-time performance graphs
