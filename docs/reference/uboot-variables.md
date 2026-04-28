# U-Boot Variables

All variables are read and written by `fs-updater-lib` via `libubootenv`.
Config path: `/etc/fw_env.config` (overridable at build time via
`UBOOT_CONFIG_PATH`).

---

## Variable reference

| Variable | Type | Written by | Purpose |
|----------|------|-----------|---------|
| `update` | 4-char string | `fs-updater-lib` | Per-slot state (see below) |
| `update_reboot_state` | `uint8_t` (0–13) | `fs-updater-lib` / `dynamic-overlay` | 13-state machine position |
| `BOOT_ORDER` | `"A B"` / `"B A"` | `fs-updater-lib` / U-Boot | Boot slot priority |
| `BOOT_ORDER_OLD` | `"A B"` / `"B A"` | `fs-updater-lib` | Previous boot order; rollback reference |
| `BOOT_A_LEFT` | 0–3 | U-Boot | Remaining boot attempts for slot A |
| `BOOT_B_LEFT` | 0–3 | U-Boot | Remaining boot attempts for slot B |
| `rauc_cmd` | `"rauc.slot=A"` / `"rauc.slot=B"` | U-Boot | Currently booted slot (from kernel cmdline) |
| `application` | `A` / `B` | `fs-updater-lib` | Active application slot |

---

## `update` variable format

4-character string. Each position encodes one slot's state:

| Position | Slot |
|----------|------|
| 0 | FW_A |
| 1 | APP_A |
| 2 | FW_B |
| 3 | APP_B |

State values per position:

| Char | Meaning |
|------|---------|
| `0` | Committed (good, verified) |
| `1` | Uncommitted (freshly installed, not yet confirmed) |
| `2` | Bad (unbootable; excluded from rollback targets) |

**Example:** `"0010"` = FW_A committed, APP_A committed, FW_B uncommitted, APP_B committed.

---

## `update_reboot_state` lifecycle

The variable is the canonical 13-state machine; see
[State Machine](../state-machine.md) for the full transition diagram and
`UBootBootstateFlags` enum.

| Value | Written when |
|-------|-------------|
| `0` | `commit_update()` succeeds; idle baseline |
| `1` | U-Boot detects `BOOT_X_LEFT` drained after FW install |
| `2` | `update_firmware()` completes successfully |
| `3` | `update_application()` completes successfully |
| `4` | `update_firmware_and_application()` completes |
| `5` | `update_firmware()` fails at the RAUC install step |
| `6` | `update_application()` fails, or post-reboot mount fails |
| `7–9` | `rollback_firmware()` / `rollback_application()` called |
| `10–12` | Post-reboot rollback verification window |
| `13` | Not written; sentinel for an invalid value read from env |

---

## Write batching contract

All writes to U-Boot variables **must** use the batch pattern:

```cpp
uboot.addVariable("update_reboot_state", "2");
uboot.addVariable("update",              "0110");
uboot.flushEnvironment();   // single flash write
```

`flushEnvironment()` writes all staged variables atomically via libubootenv's
redundant environment copies. Never write individual variables one at a time —
a power failure between writes leaves the environment inconsistent.

---

## Platform configuration

The MTD / block device used for U-Boot environment storage is set at build time:

| CMake option | Default | Used for |
|--------------|---------|---------|
| `UBOOT_ENV_NAND` | `mtd5` | NAND flash boot devices |
| `UBOOT_ENV_MMC` | `mmcblk2boot0` | eMMC boot devices |

`dynamic-overlay` detects the boot device at runtime via `/sys/bdinfo/boot_dev`
or `/proc/cmdline` and generates the matching `/etc/fw_env.config` from a
board-specific template.
