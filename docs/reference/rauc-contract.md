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
- **Update or rollback commit:** the commit of a booted firmware update or a
  firmware rollback sets both `BOOT_A_LEFT` and `BOOT_B_LEFT` to 3.
- **Acknowledged failed firmware update:** the commit of a firmware update
  whose slot never ran (`get_update_reboot_state()` reports
  `FAILED_FW_UPDATE` or `FW_UPDATE_REBOOT_FAILED`) refills only the running
  slot's counter; the written slot keeps `BOOT_x_LEFT = 0`.

A slot that keeps failing before `boot-complete.target` never reaches the
mark-good service, so its counter still drains and the selector moves on.

---

## `rauc install` and the bad marker

With the U-Boot backend, `rauc install` marks its target slot bad **before**
writing it: the slot is removed from `BOOT_ORDER` (leaving a single-slot
order such as `"B"`) and `BOOT_x_LEFT` is set to 0. Only a successful
install marks it active again (first in `BOOT_ORDER`, `BOOT_x_LEFT = 3`);
nothing restores the order on error. The single-slot order is therefore
RAUC's persistent "bad", not a state in passing.

fs-updater stages `update_reboot_state = INCOMPLETE_FW_UPDATE` and
`BOOT_ORDER_OLD = "<running> <other>"` before `rauc install` starts, so that
after a reboot the boot variables tell what became of the target: shut out of
`BOOT_ORDER` (write never completed), in the order with no attempts left
(written, did not boot) or still the pre-install order (never touched). The
acknowledging commit runs `rauc status mark-bad other` for the first two
shapes **before** opening its own environment transaction: the library holds
libubootenv's file lock for the whole transaction, and RAUC's `fw_setenv`
would block on it.

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
| Mark other slot bad | `rauc status mark-bad other` |
| Activate other slot | `rauc status mark-active other` |
| Query slot status | `rauc status --output-format=json` |
| Inspect bundle | `rauc info --output-format=json <path>` |

JSON output from `rauc status` and `rauc info` is parsed with libjsoncpp.
Parse failures throw `rauc::RaucBaseException`.
