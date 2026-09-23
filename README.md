# F&S Updater Library

Core library of the F&S Update Framework: A/B firmware and application updates
for embedded Linux, installed through RAUC and tracked in the U-Boot
environment so an update survives reboots and power loss and is kept or
reverted on evidence. Its callers are `fs-updater-service`, which runs the
installs, and `fs-updater-cli`, which commits, rolls back and queries state.
The `application` variable it writes is what `dynamic-overlay` reads on the
next boot to mount the application image.

## Overview

The library (`fs_updater`, C++17) provides:

- **Installs** of a `.fs` container, a raw RAUC bundle or a raw F&S
  application image, with format detection and streamed, hash-checked
  extraction
- **RAUC over D-Bus** as the only install backend, for firmware and
  application bundles
- **A persistent update state machine** in the U-Boot environment, whose
  commit and rollback read the outcome of a reboot from the booted slot, the
  boot order and counters, and the mounted application image
- **Boot confirmation** — `commit_update()` doubles as the routine mark-good
  that keeps the running slot's boot counter full
- **Logging** through pluggable sinks on a worker thread

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Applications                                │
│               (fs-updater-service · fs-updater-cli)                 │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  fs::FSUpdate — install · commit · apply · rollback · queries       │
└─────────────────────────────────────────────────────────────────────┘
          │                                          │
          ▼                                          ▼
   U-Boot environment (libubootenv)        RAUC daemon (D-Bus system bus)
```

The components behind `FSUpdate` are described in
[docs/architecture.md](docs/architecture.md).

## Quick start

```cpp
#include <fs_update_framework/handle_update/fsupdate.h>
#include <fs_update_framework/logger/LoggerSinkStdout.h>
#include <iostream>

auto sink   = std::make_shared<logger::LoggerSinkStdout>(logger::logLevel::INFO);
auto logger = logger::LoggerHandler::initLogger(sink);

fs::FSUpdate updater(logger);

std::string path = "/mnt/usb/update.fs", type;
uint8_t installed = 0;
try {
    updater.update_image(path, type, installed);
    // reboot here
} catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
}

// after the reboot
updater.commit_update();
```

[docs/getting-started.md](docs/getting-started.md) walks through the whole
sequence, including linking, rollback and the exceptions to expect.

## Build

```bash
./scripts/build.sh release   # cross-compile with the F&S Yocto SDK
./scripts/build.sh test      # native unit tests
```

Every target, CMake option and dependency is listed in
[docs/contributing.md](docs/contributing.md).

## Documentation

| Document | Contents |
|----------|----------|
| [docs/getting-started.md](docs/getting-started.md) | Build, link, first update, rollback |
| [docs/architecture.md](docs/architecture.md) | Callers, components, install flow, runtime paths |
| [docs/state-machine.md](docs/state-machine.md) | `update_reboot_state` values, transition diagram, stuck-state recovery |
| [docs/reference/api.md](docs/reference/api.md) | Every `FSUpdate` call, exceptions, thread safety |
| [docs/reference/bundle-format.md](docs/reference/bundle-format.md) | Accepted inputs, `.fs` container layout, staging |
| [docs/reference/uboot-variables.md](docs/reference/uboot-variables.md) | U-Boot variables, slot bitfield, boot counters, write batching |
| [docs/reference/rauc-contract.md](docs/reference/rauc-contract.md) | RAUC D-Bus backend, what RAUC writes, counter ownership |
| [docs/integration/rauc-system-conf.md](docs/integration/rauc-system-conf.md) | RAUC `system.conf` lookup and keyring |
| [docs/contributing.md](docs/contributing.md) | Build targets and options, dependencies, tests, coding standard |

## Related Projects

- [RAUC](https://rauc.io/) — Robust Auto-Update Controller
- [dynamic-overlay](https://github.com/fsembedded/dynamic-overlay) — Overlay filesystem mounting
- [fs-updater-cli](https://github.com/fsembedded/fs-updater-cli) — Command-line interface

## License

MIT License
