# U-Boot Variables

This is the one place the library's U-Boot environment variables are
described. Other documents link here instead of repeating the table.

The library reads and writes the environment through `libubootenv`, using the
configuration file compiled in as `UBOOT_CONFIG_PATH` (default
`/etc/fw_env.config`, see [Contributing](../contributing.md#cmake-options) for
the override). The boot device itself is whatever that file names; the library
has no NAND/eMMC switch of its own.

---

## Variable reference

| Variable | Values the library accepts | Written by | Purpose |
|----------|----------------------------|------------|---------|
| `update` | 4 digits, each `0`–`3` (see below) | `fs-updater-lib` | Per-slot commit/bad state for firmware and application |
| `update_reboot_state` | `0`–`12` | `fs-updater-lib` | Position in the update state machine; see [State Machine](../state-machine.md) |
| `BOOT_ORDER` | `"A B"`, `"B A"`, `"A"`, `"B"` | RAUC's U-Boot backend during an install; `fs-updater-lib` on commit, rollback, slot switch and install failure | Boot slot priority; the leading field is the preferred slot |
| `BOOT_ORDER_OLD` | `"A B"`, `"B A"`, `"A"`, `"B"` | `fs-updater-lib` only | The last committed boot order; what a failed or abandoned update is reverted to |
| `BOOT_A_LEFT` | `0`–`3` | U-Boot (decrement per boot attempt); RAUC's U-Boot backend during an install; `fs-updater-lib` on commit and rollback | Remaining boot attempts for slot A |
| `BOOT_B_LEFT` | `0`–`3` | same as `BOOT_A_LEFT` | Remaining boot attempts for slot B |
| `rauc_cmd` | `"rauc.slot=A"`, `"rauc.slot=B"` | the boot script, not this library | The firmware slot that was booted; the library reads the part after `=` |
| `application` | `A`, `B` | `fs-updater-lib` | Active application slot, read at boot to pick the application image |

Every read is validated against the column "Values the library accepts". A
value outside it raises `UBoot::UBootEnvVarNotAllowedContent` instead of being
interpreted — which means, for example, that a board configured with more than
three boot attempts per slot cannot be read by this library. An
`update_reboot_state` that is absent or outside `0`–`12` is not an error: it
reads as the recovery state `UNKNOWN_STATE` (13), which is never written back.

`BOOT_ORDER` may hold a single slot. RAUC's backend writes that shape when it
takes a slot out of the rotation, for example while it writes the image into
it.

---

## `update` variable format

Four characters, one per slot:

| Position | Slot |
|----------|------|
| 0 | FW_A |
| 1 | APP_A |
| 2 | FW_B |
| 3 | APP_B |

Each character is a two-bit value:

| Bit | Meaning |
|-----|---------|
| bit 0 (`1`) | Uncommitted — installed, not yet confirmed by a commit |
| bit 1 (`2`) | Bad — refused as a rollback or switch target |

So `0` is committed, `1` uncommitted, `2` bad, and `3` uncommitted and bad at
once. The two facts are kept apart on purpose: committing an update clears
only the uncommitted bit, and a bad mark survives it. Only a new install into a
slot clears the bad bit, because the image it condemned is being replaced.

At most one firmware position and at most one application position may carry
the uncommitted bit at a time. Every write of the variable is checked against
that rule and refused if it breaks it.

**Example:** `"0010"` — FW_A committed, APP_A committed, FW_B uncommitted,
APP_B committed.

---

## Boot-attempt counters

`BOOT_A_LEFT` and `BOOT_B_LEFT` have three writers:

- **U-Boot** decrements the counter of the slot it tries to boot.
- **RAUC's U-Boot backend** sets them during an install: it zeroes the target
  while it writes the image and gives it a full budget when it makes the target
  primary.
- **`fs-updater-lib`** puts them back:
  - every `commit_update()` that settles a pending state resets both to `3`;
  - `commit_update()` with nothing pending resets the **running** slot's counter
    to `3`, and only if it is below `3` — this is the routine mark-good;
  - `rollback_firmware()` after a successful update reboot sets the running
    slot's counter to `0`, so the next boot falls back; before the reboot it
    restores both to `3`.

The library never asks RAUC to mark a slot good for this; see
[RAUC Integration Contract](rauc-contract.md#who-resets-the-counters).

---

## Write batching

A caller outside the library should not need to write these variables. For
code inside it, and for anything built on `UBoot::IUBootEnv`, the rule is:

```cpp
UBoot::EnvTransaction txn(env);                 // one open environment
env.addVariable("update_reboot_state", "2");    // staged, not written
env.addVariable("update", "0010");
env.flushEnvironment();                         // one libubootenv store for all
```

`flushEnvironment()` writes every staged variable and stores the environment
once. Variables still staged when the outermost `EnvTransaction` closes are
discarded, not written. Writing variables one flush at a time leaves an
inconsistent pair behind if power fails in between.
