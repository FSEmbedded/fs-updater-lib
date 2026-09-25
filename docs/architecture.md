# F&S Updater Library Architecture

## Overview

The library keeps an A/B embedded Linux system updatable: it installs firmware
and application updates through RAUC, records every step in the U-Boot
environment so the state survives a reboot or a power loss, and decides after
the reboot whether an update is kept or reverted.

Design rules the code follows:

1. **A/B, always a fallback** — an update is written to the slot that is not
   running; the running one stays bootable until the new one is committed.
2. **The U-Boot environment is the truth** — the state, the slot bitfield and
   the boot order live there, never only in memory.
3. **Verdicts from evidence** — where the outcome of a reboot is in question,
   a commit decides it from the booted slot, the boot order and counters, or
   the mounted application image rather than from the stored state. For a few
   states the commit's precondition is the stored value or the slot bitfield
   alone; they are listed in
   [Stale and stuck states](state-machine.md#stale-and-stuck-states).
4. **One RAUC backend** — RAUC is driven over its D-Bus interface only.

## Callers

```
fs-updater-cli ──── D-Bus (de.fsembedded.fsupdate1) ───▶ fs-updater-service
      │                                                        │
      │ commit, apply, rollback,                               │ update_image(),
      │ state and version queries, bad marks                   │ apply_pending_update(), …
      ▼                                                        ▼
┌───────────────────────────────── fs-updater-lib ─────────────────────────────────┐
```

`fs-updater-service` owns the install: it calls `update_image()` (always with
an empty `update_type`) and `apply_pending_update()` on behalf of the CLI and
the Azure Device Update handler. The CLI never installs through the library
itself; it calls it directly for commit, apply, rollback, state and version
queries, the stalled-install and reboot checks, and the slot bad marks. The
service's D-Bus protocol belongs to those two components and is documented in
`fs-updater-cli` (`docs/integration/dbus-session-protocol.md`).

## Components

```
┌──────────────────────────────────────────────────────────────────────────┐
│ fs::FSUpdate                     public API (fsupdate.h)                 │
│   install · commit · apply · rollback · state and version queries        │
└──────┬───────────────────────┬──────────────────────────┬────────────────┘
       │                       │                          │
       ▼                       ▼                          ▼
┌───────────────┐   ┌──────────────────────┐   ┌─────────────────────────────┐
│ UpdateSource  │   │ updater::Bootstate   │   │ updater::firmwareUpdate     │
│ layer         │   │ (handleUpdate.cpp)   │   │ updater::applicationUpdate  │
│ format sniff, │   │ state predicates,    │   │ (RaucApplicationUpdate)     │
│ .fs container │   │ commit, rollback,    │   │ install through RAUC; raw   │
│ extraction    │   │ reboot evidence      │   │ app image verify and copy   │
└───────┬───────┘   └──────────┬───────────┘   └───────┬───────────┬─────────┘
        │                      │                       │           │
        │                      ▼                       ▼           │
        │           ┌──────────────────────┐  ┌──────────────────┐ │
        │           │ UBoot::UBoot         │  │ rauc::           │ │
        │           │ (IUBootEnv)          │  │ rauc_dbus_client │ │
        │           └──────────┬───────────┘  └────────┬─────────┘ │
        ▼                      ▼                       ▼           ▼
   staging dir           libubootenv          RAUC daemon      app image store
   (persistent)          /etc/fw_env.config   (system bus)     (persistent)
```

### `fs::FSUpdate` — `handle_update/fsupdate.{h,cpp}`

The only class a caller needs. It checks the stored state before every
install, writes the pending state and the slot digit **before** the install
starts, and writes the failed state if the install throws. Commit dispatches on
the stored state, one arm per value, and hands the evidence checks to
`Bootstate`. Every call and exception: [API Reference](reference/api.md).

### Update sources — `handle_update/sources/`

`make_update_source()` reads the first 64 bytes of the input and picks a source:
the `.fs` v2.0 container, a raw RAUC bundle, or a raw F&S application image.
The container source streams each member to the staging directory, hashing it
on the way, and returns the paths to install. Formats, layout and staging:
[Bundle Format](reference/bundle-format.md).

### `updater::Bootstate` — `handle_update/handleUpdate.{h,cpp}`

The state machine's evidence side. Its predicates combine the stored state with
the slot bitfield; its confirm methods settle a state after reading the booted
slot (`rauc_cmd`), `BOOT_ORDER` against `BOOT_ORDER_OLD`, the boot counters,
and which application image the loop devices carry. States and transitions:
[State Machine](state-machine.md).

### `updater::firmwareUpdate` and `updater::applicationUpdate`

`firmwareUpdate` hands a firmware bundle to RAUC and waits for it.
`applicationUpdate` does the same for a RAUC application bundle, then moves the
image the bundle's install hook staged (`.incoming.squashfs` and its verity
sidecars) onto the inactive slot's files (`app_a.squashfs` /
`app_b.squashfs`). A raw F&S application image is verified by the library
itself — certificate chain against RAUC's keyring, header CRC, signature — and
copied into place. Both paths end by flipping the `application` variable.
Keyring and config lookup: [RAUC system.conf](integration/rauc-system-conf.md).

### `rauc::rauc_dbus_client` — `dbus/`

The RAUC backend: `InstallBundle` with its `Completed` signal and progress,
and `InspectBundle` to classify raw bundles. What RAUC writes on its own during
an install, and why the library does not use RAUC's mark-good:
[RAUC Integration Contract](reference/rauc-contract.md).

### `UBoot::UBoot` — `uboot_interface/`

`libubootenv` behind the `UBoot::IUBootEnv` seam. Reads are validated against
an allow-list per variable; writes are staged and flushed once. The variables:
[U-Boot Variables](reference/uboot-variables.md).

### `logger::LoggerHandler` — `logger/`

A queue with one worker thread per sink; callers from any thread add entries,
the worker hands them to the sink. Sinks and levels:
[API Reference](reference/api.md#logger).

## Install flow

```
update_image(path, type="", installed)
  │
  ├─ remove the previous install's staged files
  ├─ make_update_source(path)          sniff the format, refuse unknown or v1.0
  ├─ source->prepare()                 container: stream + hash members into staging
  ├─ dispatch on what was resolved
  │     firmware only  → update_firmware()                  installed = 1
  │     application    → update_application()               installed = 2
  │     both           → update_firmware_and_application()  installed = 3
  │
  └─ each of those:
        refuse unless state == 0                  (fs::UpdateInProgress)
        write target digit "uncommitted" + state 2/3 (4 once the firmware half is in)
        RAUC InstallBundle, wait for Completed    (RAUC makes the target primary)
        on failure: state 5/6, rethrow
```

The state check is not the first step: the staging cleanup and the container
extraction above run whatever the state is; see
[API Reference](reference/api.md#install).

The reboot writes nothing, and neither does `apply_pending_update()`: RAUC has
already made the new slot primary, so the next boot is the trial boot whatever
triggers it.

## After the reboot

`commit_update()` reads the evidence and either keeps the update — settles the
digit and, for firmware, adopts the boot order as `BOOT_ORDER_OLD` and restores
both boot counters — or, if the bootloader fell back, marks the new slot bad
and restores the old order. Either way the state returns to 0. The per-state rules, the rollback
paths and the recovery of stuck states are in
[State Machine](state-machine.md).

## Runtime paths

| Path | Default | Set by |
|------|---------|--------|
| U-Boot environment config | `/etc/fw_env.config` | `UBOOT_CONFIG_PATH` |
| RAUC config | `/etc/rauc/system.conf`, `/run/rauc/system.conf`, `/usr/lib/rauc/system.conf` (first found) | fixed, RAUC's order |
| Container staging directory | `/rw_fs/.cache/` | `FSUP_RAUC_SCRATCH`, or `update_image()`'s `rauc_scratch_path` |
| Application image store | `/rw_fs/root/application/` | `FSUP_APP_IMG_STORE` |
| Firmware version file | `/etc/fw_version` | fixed |
| Application version file | `/etc/app_version` | `FSUP_APP_VERSION_FILE` |
| Caller work directory | `/tmp/adu/.work` | `TEMP_ADU_WORK_DIR` |

Build options: [Contributing](contributing.md#cmake-options).
