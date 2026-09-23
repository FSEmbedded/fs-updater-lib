# F&S Update Framework Library

The **F&S Update Framework (FSUF)** provides A/B firmware and application updates for embedded Linux systems using RAUC and SquashFS.

See [README](../README.md) for an overview and the documentation index.
See [Architecture](architecture.md) for component design and data flow, and
[Bundle Format](reference/bundle-format.md) for what an update file looks like.

## Key Technologies

**RAUC** — update client that installs bundles into A/B slots on the device, and creates the bundles on the host. This library drives it over D-Bus. [Documentation](https://rauc.io/)

**SquashFS** — read-only compressed filesystem used for application images; minimises storage and provides fast random access. [Documentation](https://www.kernel.org/doc/html/latest/filesystems/squashfs.html)

## License

MIT License
