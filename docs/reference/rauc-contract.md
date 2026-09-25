# RAUC Integration Contract

## Standard RAUC boot selection

RAUC's standard contract is two-part:

1. **U-Boot selector** (runs every boot) — decrements `BOOT_A_LEFT` or
   `BOOT_B_LEFT` for the chosen slot before booting. If a counter reaches 0,
   the slot is skipped; if both reach 0, both are reset to 3 and the board
   resets. The boot selector of this board support package does not reset
   them: with both at 0 it prints "Boot failed" and sets `boot_failed`.

2. **`rauc-mark-good.service`** (runs after `boot-complete.target`) — calls
   `rauc status mark-good`, which resets `BOOT_x_LEFT` for the currently
   booted slot back to 3.

Together, the two halves ensure that a slot that can't reach stable userspace
is eventually abandoned after 3 attempts.

## F&S divergence

`rauc-mark-good.service` (from `meta-fus-updater`) does not call
`rauc status mark-good`. Its `ExecCondition` (`check-fsup-state.sh`) lets it
run only when `fs-updater --update_reboot_state` answers 27 (no update
pending), and it then runs `fs-updater --commit_update`
(`SuccessExitStatus=16 17`).

### When counters are reset

- **Idle boot:** `commit_update()` restores `BOOT_x_LEFT` of the running slot
  to 3 when it is below 3 (answer 16), otherwise it changes nothing (17).
- **Update or rollback commit:** the commit of a firmware update or a
  firmware rollback sets both `BOOT_A_LEFT` and `BOOT_B_LEFT` to 3.

A slot that keeps failing before `boot-complete.target` never reaches the
mark-good service, so its counter still drains and the selector moves on.

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
