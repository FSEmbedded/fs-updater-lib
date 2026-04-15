# Update State Machine — Implementation Reference

Canonical implementation reference for the `update_reboot_state` machine.
This document is the **single source of truth** for the state diagram; other
documents (`README.md`, `architecture.md`, `docs/cli/diagrams/state-machine.md`)
link here rather than copying the diagram.

## Requirements

Formal requirements and traceability matrix: [`prd.md` §3.2 and §11.2](../../prd.md#32-fs-updater-lib).

## State encoding

`update_reboot_state` is a `uint8_t` U-Boot environment variable holding one
of the following values (`UBootBootstateFlags` enum in
`src/handle_update/updateDefinitions.h`):

```cpp
enum class UBootBootstateFlags : unsigned char {
    NO_UPDATE_REBOOT_PENDING        = 0,   // Normal operation (IDLE)
    FW_UPDATE_REBOOT_FAILED         = 1,   // FW installed but bootloader fell back
    INCOMPLETE_FW_UPDATE            = 2,   // FW installed, awaiting reboot
    INCOMPLETE_APP_UPDATE           = 3,   // APP installed, awaiting reboot
    INCOMPLETE_APP_FW_UPDATE        = 4,   // Both installed, awaiting reboot
    FAILED_FW_UPDATE                = 5,   // FW install failed (installer-level)
    FAILED_APP_UPDATE               = 6,   // APP install or post-reboot mount failed
    ROLLBACK_FW_REBOOT_PENDING      = 7,   // FW rollback requested, reboot pending
    ROLLBACK_APP_REBOOT_PENDING     = 8,   // APP rollback requested, reboot pending
    ROLLBACK_APP_FW_REBOOT_PENDING  = 9,   // Both rollbacks requested, reboot pending
    INCOMPLETE_FW_ROLLBACK          = 10,  // FW rolled back, awaiting commit
    INCOMPLETE_APP_ROLLBACK         = 11,  // APP rolled back, awaiting commit
    INCOMPLETE_APP_FW_ROLLBACK      = 12,  // Both rolled back, awaiting commit
    UNKNOWN_STATE                   = 13,  // Sentinel: invalid value
};
```

State 1 is **distinct** from state 5: state 5 means the installer failed; state 1
means the installer succeeded but the bootloader fell back to the old slot
because `BOOT_X_LEFT` drained to 0. States 10–12 cover the post-reboot rollback
verification window.

## State table

| Value | State | Description |
|-------|-------|-------------|
| 0 | `NO_UPDATE_REBOOT_PENDING` | Normal operation, no pending updates |
| 1 | `FW_UPDATE_REBOOT_FAILED` | FW installed but bootloader fell back (`BOOT_X_LEFT` drained) |
| 2 | `INCOMPLETE_FW_UPDATE` | Firmware installed, awaiting reboot verification |
| 3 | `INCOMPLETE_APP_UPDATE` | Application installed, awaiting reboot verification |
| 4 | `INCOMPLETE_APP_FW_UPDATE` | Both installed, awaiting reboot verification |
| 5 | `FAILED_FW_UPDATE` | Firmware installation failed (installer-level) |
| 6 | `FAILED_APP_UPDATE` | Application installation or post-reboot mount failed |
| 7 | `ROLLBACK_FW_REBOOT_PENDING` | Firmware rollback requested, reboot pending |
| 8 | `ROLLBACK_APP_REBOOT_PENDING` | Application rollback requested, reboot pending |
| 9 | `ROLLBACK_APP_FW_REBOOT_PENDING` | Both rollbacks requested, reboot pending |
| 10 | `INCOMPLETE_FW_ROLLBACK` | Firmware rolled back, awaiting commit |
| 11 | `INCOMPLETE_APP_ROLLBACK` | Application rolled back, awaiting commit |
| 12 | `INCOMPLETE_APP_FW_ROLLBACK` | Both rolled back, awaiting commit |
| 13 | `UNKNOWN_STATE` | Sentinel: invalid `update_reboot_state` value |

## Transition diagram

```
Phase 1 — Install (from IDLE)
──────────────────────────────
                           NO_UPDATE_REBOOT_PENDING (0)  [IDLE]
                                      │
           ┌──────────────────────────┼──────────────────────────┐
  update_firmware()          update_application()     update_firmware_and_application()
           │                          │                          │
           ▼                          ▼                          ▼
  INCOMPLETE_FW_UPDATE (2)   INCOMPLETE_APP_UPDATE (3)   INCOMPLETE_APP_FW_UPDATE (4)
           │ install fail             │ install fail             │ install fail
           ▼                          ▼                          ▼
  FAILED_FW_UPDATE (5)       FAILED_APP_UPDATE (6)       FAILED_FW / FAILED_APP (5/6)

Phase 2 — Reboot & verify (INCOMPLETE_* → commit or fail)
──────────────────────────────────────────────────────────
  INCOMPLETE_FW_UPDATE (2) ──reboot──▶ bootloader selects new FW slot
                                         │
                                ┌────────┴────────┐
                          booted new        booted old (BOOT_X_LEFT = 0)
                                │                  │
                                ▼                  ▼
                        commit_update()   FW_UPDATE_REBOOT_FAILED (1)
                              → IDLE (0)

  INCOMPLETE_APP_UPDATE (3) ──reboot──▶ dynamic-overlay mounts new APP slot
                                         │
                                ┌────────┴────────┐
                              success            failure
                                │                  │
                                ▼                  ▼
                        commit_update()    FAILED_APP_UPDATE (6)
                              → IDLE (0)

  INCOMPLETE_APP_FW_UPDATE (4) ──reboot──▶ both verifications run
                                         │
                                ┌────────┴────────┐
                              success      any component fails
                                │                  │
                                ▼                  ▼
                        commit_update()    FAILED_FW / FAILED_APP (5/6)
                              → IDLE (0)

Phase 3 — Rollback initiation (from FAILED_* or FW_UPDATE_REBOOT_FAILED)
─────────────────────────────────────────────────────────────────────────
  FAILED_FW_UPDATE (5)            ──rollback_firmware()────▶   ROLLBACK_FW_REBOOT_PENDING (7)
  FAILED_APP_UPDATE (6)           ──rollback_application()─▶   ROLLBACK_APP_REBOOT_PENDING (8)
  combined (5 + 6)                ──rollback_*──────────────▶  ROLLBACK_APP_FW_REBOOT_PENDING (9)
  FW_UPDATE_REBOOT_FAILED (1)     ──rollback_firmware()────▶   ROLLBACK_FW_REBOOT_PENDING (7)

Phase 4 — Rollback verify (post-reboot)
────────────────────────────────────────
  ROLLBACK_FW_REBOOT_PENDING (7)      ──reboot──▶ INCOMPLETE_FW_ROLLBACK (10)
  ROLLBACK_APP_REBOOT_PENDING (8)     ──reboot──▶ INCOMPLETE_APP_ROLLBACK (11)
  ROLLBACK_APP_FW_REBOOT_PENDING (9)  ──reboot──▶ INCOMPLETE_APP_FW_ROLLBACK (12)
                                                         │
                                                         │ commit_update()
                                                         ▼
                                                   IDLE (0)

Sentinel
────────
  UNKNOWN_STATE (13) — returned when update_reboot_state holds an invalid value.
                       Not a reachable phase; indicates environment corruption.
```

## Related documents

- [`prd.md` §3.2](../../prd.md#32-fs-updater-lib) — requirements (authoritative)
- [`prd.md` §5](../../prd.md#5-rollback-flow) — rollback flow detail (bootloader-automatic vs. caller-initiated)
- [`prd.md` §11.2](../../prd.md#112-fs-updater-lib--state-machine-fr-lib-sm) — FR-LIB-SM traceability matrix
- [`architecture.md`](architecture.md) — component design; links here for the diagram
- [`../README.md`](../README.md) — quick-start API guide; links here for the diagram
- [`docs/cli/diagrams/state-machine.md`](../../docs/cli/diagrams/state-machine.md) — CLI-oriented view: Mermaid diagram, update and rollback lifecycle sequences
- [`docs/safety-rules.md`](../../docs/safety-rules.md) — critical rules and flush contracts
- [`docs/uboot-selector-analysis.md`](../../docs/uboot-selector-analysis.md) — bootloader side of the handshake
- [`docs/rauc-integration.md`](../../docs/rauc-integration.md) — RAUC mark-good contract and counter drain
