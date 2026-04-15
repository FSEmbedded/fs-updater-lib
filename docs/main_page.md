# F&S Update Framework Library

The **F&S Update Framework (FSUF)** provides A/B firmware and application updates for embedded Linux systems using RAUC and SquashFS.

See [README](../README.md) for quick start, build instructions, API usage, and component overview.
See [Architecture](architecture.md) for component design, data flow, and bundle format.

## Key Technologies

**RAUC** — lightweight update client handling A/B slot management and boot confirmation on the device, and bundle creation on the host. [Documentation](https://rauc.io/)

**SquashFS** — read-only compressed filesystem used for application images; minimises storage and provides fast random access. [Documentation](https://www.kernel.org/doc/html/latest/filesystems/squashfs.html)

## License

Proprietary — F&S Elektronik Systeme GmbH
