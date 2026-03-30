# F&S Update Framework Library

The **F&S Update Framework (FSUF)** uses several open source frameworks to update U-Boot, Kernel, Root File System and Application. FSUF follows the idea of always having a working configuration through A/B updates.

## Overview

This library provides the core functionality for:

- **Firmware Updates**: Using RAUC for reliable A/B slot management
- **Application Updates**: SquashFS image deployment with rollback support
- **State Management**: Persistent update state tracking via U-Boot environment
- **Logging**: Thread-safe logging framework with pluggable sinks

## Documentation

- [README](../README.md) - Quick start, build instructions, and usage examples
- [Architecture](architecture.md) - Detailed component design and data flow

## Key Technologies

### RAUC (Robust Auto-Update Controller)

RAUC is a lightweight update client that runs on your embedded device and reliably controls the procedure of updating your device with a new firmware revision. RAUC is also the tool on your host system that lets you create, inspect and modify update artifacts for your device.

For more information: [RAUC Documentation](https://rauc.io/)

### SquashFS

For minimizing memory requirements, we compress the file system using SquashFS. This provides:
- Read-only compressed filesystem
- High compression ratios
- Fast random access

For more information: [SquashFS Documentation](https://www.kernel.org/doc/html/latest/filesystems/squashfs.html)

## A/B Update Concept

```
┌─────────────────────────────────────────────────────────────────┐
│                        Storage Layout                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Firmware (managed by RAUC):                                    │
│  ┌─────────────────┐    ┌─────────────────┐                     │
│  │    Slot A       │    │    Slot B       │                     │
│  │  (rootfs_a)     │    │  (rootfs_b)     │                     │
│  │                 │    │                 │                     │
│  │  U-Boot         │    │  U-Boot         │                     │
│  │  Kernel         │    │  Kernel         │                     │
│  │  RootFS         │    │  RootFS         │                     │
│  └─────────────────┘    └─────────────────┘                     │
│          ▲                                                       │
│          │ active (example)                                      │
│                                                                  │
│  Application (managed by library):                              │
│  ┌─────────────────┐    ┌─────────────────┐                     │
│  │  app_a.squashfs │    │  app_b.squashfs │                     │
│  └─────────────────┘    └─────────────────┘                     │
│          ▲                                                       │
│          │ current symlink                                       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Update Flow

1. **Install**: New image written to inactive slot
2. **Mark**: Inactive slot marked as bootable
3. **Reboot**: System boots into new slot
4. **Verify**: Update success verified
5. **Commit**: New slot marked as good (or rollback on failure)

## Components

| Component | Purpose |
|-----------|---------|
| `FSUpdate` | Main API for update operations |
| `Bootstate` | Update state machine management |
| `rauc_handler` | RAUC command integration |
| `applicationUpdate` | Application image handling |
| `UBoot` | U-Boot environment access |
| `LoggerHandler` | Thread-safe logging |

## Related Projects

- [dynamic-overlay](../../dynamic-overlay/) - Overlay filesystem mounting at boot
- [fs-updater-cli](../../fs-updater-cli/) - Command-line interface for updates

## License

Proprietary - F&S Elektronik Systeme GmbH
