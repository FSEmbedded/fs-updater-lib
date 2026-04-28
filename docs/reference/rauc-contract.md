# RAUC Integration Contract

## Standard RAUC boot selection

RAUC's standard contract is two-part:

1. **U-Boot selector** (runs every boot) — decrements `BOOT_A_LEFT` or
   `BOOT_B_LEFT` for the chosen slot before booting. If a counter reaches 0,
   the slot is skipped; if both reach 0, both are reset to 3 and the board
   resets.

2. **`rauc-mark-good.service`** (runs after `boot-complete.target`) — calls
   `rauc status mark-good`, which resets `BOOT_x_LEFT` for the currently
   booted slot back to 3.

Together, the two halves ensure that a slot that can't reach stable userspace
is eventually abandoned after 3 attempts.

## F&S divergence

F&S replaces `rauc-mark-good.service` with a no-op stub (from
`meta-fus-updater`). Counter resets are handled exclusively by
`fs-updater-lib` during update commit.

### When counters are reset

`fs-updater-lib` resets both `BOOT_A_LEFT` and `BOOT_B_LEFT` to 3 only during
`commit_update()`, specifically inside `Bootstate::confirmPendingFirmwareUpdate()`
and `Bootstate::confirmApplicationFirmwareUpdate()`:

- After a successful firmware reboot (new slot confirmed)
- After a failed firmware reboot (reverting to old slot)

During normal operation (`update_reboot_state = 0`), `commit_update()` returns
`false` immediately and never touches the counters.

### Counter drain risk

Because there is no mark-good service, `BOOT_x_LEFT` decrements on every boot
but is only restored on update commit. On a device that never installs an
update, counters drain over time. After 3 normal reboots, the U-Boot selector
switches to the other slot; after 6, both are exhausted.

**Mitigation:** Deploy a site-specific watchdog or ensure that
`fs-updater-lib` is called to commit a synthetic state on first boot if no
update is in progress. Alternatively, configure the U-Boot selector with a
higher initial counter value (`BOOT_A_LEFT`/`BOOT_B_LEFT` = 10 or higher).

---

## `rauc status mark-good` semantics

With `bootloader=uboot` in `/etc/rauc/system.conf`, `rauc status mark-good`
calls `fw_setenv BOOT_x_LEFT <boot-attempts>` for the currently booted slot.

Relevant `system.conf` keys:

| Key | Default | Effect |
|-----|---------|--------|
| `boot-attempts` | 3 | Counter value after `mark-good` |
| `boot-attempts-primary` | 3 | Counter value after marking a slot primary (post-install) |

These keys are set per `[system]` section in `/etc/rauc/system.conf`.
See [RAUC integration](../integration/rauc-system-conf.md) for the full
`system.conf` layout.

---

## RAUC commands used by `rauc_handler`

`rauc_handler.cpp` drives RAUC by spawning the `rauc` binary as a subprocess:

| Operation | Command |
|-----------|---------|
| Install firmware bundle | `rauc install <path>` |
| Mark current slot good | `rauc status mark-good` |
| Mark other slot good | `rauc status mark-good other` |
| Activate other slot | `rauc status mark-active other` |
| Query slot status | `rauc status --output-format=json` |
| Inspect bundle | `rauc info --output-format=json <path>` |

JSON output from `rauc status` and `rauc info` is parsed with libjsoncpp.
Parse failures throw `rauc::RaucBaseException`.
