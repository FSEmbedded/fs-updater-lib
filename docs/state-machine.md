# Update State Machine — Implementation Reference

Canonical implementation reference for the `update_reboot_state` machine.
This document is the **single source of truth** for the state diagram; other
documents (README.md, architecture.md) link here rather than copying the diagram.

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
    FAILED_APP_UPDATE               = 6,   // APP install failed (installer-level)
    ROLLBACK_FW_REBOOT_PENDING      = 7,   // FW rollback requested, reboot pending
    ROLLBACK_APP_REBOOT_PENDING     = 8,   // APP rollback requested, reboot pending
    ROLLBACK_APP_FW_REBOOT_PENDING  = 9,   // Both rollbacks requested, reboot pending
    INCOMPLETE_FW_ROLLBACK          = 10,  // FW rolled back, awaiting commit
    INCOMPLETE_APP_ROLLBACK         = 11,  // APP rolled back, awaiting commit
    INCOMPLETE_APP_FW_ROLLBACK      = 12,  // Both rolled back, awaiting commit
    UNKNOWN_STATE                   = 13,  // Recovery state: never written
};
```

State 1 is **distinct** from state 5 in intent: state 5 means the installer
failed; state 1 would mean the installer succeeded but the bootloader fell back
to the old slot. **Nothing writes state 1 any more.** It is not a corruption
value: the initial commit wrote it at two sites and both were removed when the
failure was made recognisable from the boot order and the budgets instead, so a
device flashed before that change carries it in from a real, superseded flow.
It appears only in read, compare and convert positions now. A bootloader
fallback leaves the state it already had (2 or 4);
the commit's failed-reboot branch is what recognises it, from the boot order and
the budgets rather than from a state value.

**Commit leads out of it, from any shape.** The state is kept recoverable rather
than retired: a device that carries the value in has to be able to leave, and
removing the handler would leave it with nothing that recognises the value at
all. The recovery claims only what is observable — the running slot booted, so an
uncommitted digit there is settled; nothing shows which slot failed to boot, so
no slot is condemned and the boot order stays as it is; the boot budgets are put
back, because the pending state gated the routine mark-good while it lasted.

States 10–12 do **not** describe a post-reboot window. They are what a caller
wrote before rebooting, and the two families are not interchangeable: the
commit accepts 10/11/12 unconditionally, while 7/8/9 are checked against
evidence that the reboot really happened. Writing 10/11/12 ahead of the reboot
therefore replaces a verified verdict with an assumed one, which is why nothing
in this tree does it any more.

## State table

| Value | State | Description |
|-------|-------|-------------|
| 0 | `NO_UPDATE_REBOOT_PENDING` | Normal operation, no pending updates |
| 1 | `FW_UPDATE_REBOOT_FAILED` | Legacy-inbound: FW installed but bootloader fell back. Nothing writes it; `commit_update()` recovers it |
| 2 | `INCOMPLETE_FW_UPDATE` | Firmware installed, awaiting reboot verification |
| 3 | `INCOMPLETE_APP_UPDATE` | Application installed, awaiting reboot verification |
| 4 | `INCOMPLETE_APP_FW_UPDATE` | Both installed, awaiting reboot verification |
| 5 | `FAILED_FW_UPDATE` | Firmware installation failed (installer-level) |
| 6 | `FAILED_APP_UPDATE` | Application installation failed (installer-level) |
| 7 | `ROLLBACK_FW_REBOOT_PENDING` | Firmware rollback requested, reboot pending |
| 8 | `ROLLBACK_APP_REBOOT_PENDING` | Application rollback requested, reboot pending |
| 9 | `ROLLBACK_APP_FW_REBOOT_PENDING` | Both rollbacks requested, reboot pending |
| 10 | `INCOMPLETE_FW_ROLLBACK` | Legacy-inbound: firmware rolled back, awaiting commit. Nothing writes it |
| 11 | `INCOMPLETE_APP_ROLLBACK` | Legacy-inbound: application rolled back, awaiting commit. Nothing writes it |
| 12 | `INCOMPLETE_APP_FW_ROLLBACK` | Legacy-inbound: both rolled back, awaiting commit. Nothing writes it |
| 13 | `UNKNOWN_STATE` | Recovery state: content no reader can interpret |

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

Phase 2 — Reboot & verify (the reboot itself writes nothing)
──────────────────────────────────────────────────────────
  INCOMPLETE_FW_UPDATE (2) ──reboot──▶ bootloader selects new FW slot
                                         │
                                ┌────────┴────────┐
                          booted new        booted old (BOOT_X_LEFT = 0)
                                │                  │
                                ▼                  ▼
                        commit_update()    state stays 2 — commit_update()
                              → IDLE (0)    reads the fallback from the boot
                                            order and the budgets, marks the
                                            failed slot bad → IDLE (0)

  INCOMPLETE_APP_UPDATE (3) ──reboot──▶ the new APP slot is mounted
                                         │
                                ┌────────┴────────┐
                              mounted          not mounted
                                │                  │
                                ▼                  ▼
                        commit_update()    state stays 3 — the read answers
                              → IDLE (0)    indeterminate, and the consumer's
                                            trial budget bounds the retries

  INCOMPLETE_APP_FW_UPDATE (4) ──reboot──▶ both dimensions are checked
                                         │
                                ┌────────┴────────┐
                              both good     either one not
                                │                  │
                                ▼                  ▼
                        commit_update()    state stays 4 — settled by whichever
                              → IDLE (0)    of the two mechanisms above owns the
                                            dimension at fault

  States 5 and 6 are NOT reached from here: every writer of them sits in an
  install's error path, so they record an install that failed before any
  reboot. Nothing writes state 1 at all — see the note above the state table.

Phase 3 — Rollback initiation (from the INCOMPLETE_* states)
─────────────────────────────────────────────────────────────────────────
  INCOMPLETE_FW_UPDATE (2)        ──rollback_firmware()────▶   ROLLBACK_FW_REBOOT_PENDING (7)
  INCOMPLETE_APP_UPDATE (3)       ──rollback_application()─▶   ROLLBACK_APP_REBOOT_PENDING (8)
  INCOMPLETE_APP_FW_UPDATE (4)    ──rollback_*──────────────▶  ROLLBACK_APP_FW_REBOOT_PENDING (9)

  A rollback undoes an update that installed and is awaiting its verdict. It
  does NOT start from the FAILED_* states: a failed install never left the
  proven slot, so there is nothing to undo — those are acknowledged by commit.

  The edges above are taken after the update's reboot. Called before it, a
  single-component rollback of 2 or 3 needs no reboot: it points the boot
  order or the application variable back at the proven slot and returns to
  IDLE (0) at once. A combined update (4) is taken back by rollback_firmware(),
  which stores 9 either way.

  Two more edges reach states 7 and 8, and they do not come from an update at
  all:

  NO_UPDATE_REBOOT_PENDING (0)    ──rollback_firmware()────▶   ROLLBACK_FW_REBOOT_PENDING (7)
  NO_UPDATE_REBOOT_PENDING (0)    ──rollback_application()─▶   ROLLBACK_APP_REBOOT_PENDING (8)

  Called with nothing pending, the rollback verbs switch to the other slot and
  take the same rollback path, so a device that was settled a moment ago
  carries a rollback state afterwards. The switch is refused when the TARGET
  slot's digit says uncommitted or bad, or — for the application — when that
  slot was never provisioned; a settled slot is switchable, which is the whole
  point of the digit that gates it.

Phase 4 — Rollback verify (post-reboot)
────────────────────────────────────────
  ROLLBACK_FW_REBOOT_PENDING (7)      ──reboot──▶ 7, unchanged
  ROLLBACK_APP_REBOOT_PENDING (8)     ──reboot──▶ 8, unchanged
  ROLLBACK_APP_FW_REBOOT_PENDING (9)  ──reboot──▶ 9, unchanged
                                                         │
                                                         │ commit_update(), which
                                                         │ reads the reboot from
                                                         │ evidence, not a marker
                                                         ▼
                                                   IDLE (0)

  The reboot writes nothing. Apply writes nothing either — deliberately, so
  that the prescribed path and a reboot happening for any other reason leave
  the same durable state. States 10/11/12 are therefore not produced here.

  What the commit reads as evidence for state 8 is the mounted application
  image, and the three shapes it can find are reported apart:

    the active slot is mounted      → the reboot landed; the commit settles it
    the other slot is mounted       → the reboot is still owed; it leads out
    nothing is mounted              → unanswerable, and the commit settles it
                                      anyway: the slot switch already happened
                                      and no boot changes it back, so nothing
                                      is left to validate. The image still
                                      needs attention; the state no longer
                                      holds the device hostage to it.

  One classification, classify_app_rollback(), answers all three, and the
  commit's precondition is derived from it, so a caller that reports the
  outcome from the same call cannot disagree with what the commit accepts.

Recovery state
──────────────
  UNKNOWN_STATE (13) — what every read answers when the variable is absent,
                       unreadable, or holds content outside the alphabet,
                       including non-canonical numerals such as "0x02" or
                       "012". Not a phase: nothing transitions into it and no
                       verb transitions out of it.

                       It is never written. There is no encoding for it, so no
                       writer can persist it -- a stored value outside the
                       alphabet would make every read on an older image fail
                       after a fallback onto that image. The rollback verbs
                       refuse by name when they see it, before staging
                       anything.
```

## Stale and stuck states

A state is **stuck** when no automatic transition moves it forward: the caller
has to act before the next install is accepted. Every install entry point
refuses with `fs::UpdateInProgress` while any state other than 0 is stored.

| State | Value | How you got here | What happens if you do nothing | Recovery call |
|-------|------:|------------------|--------------------------------|---------------|
| `FW_UPDATE_REBOOT_FAILED` | 1 | Carried in from an older generation that still wrote it; nothing writes it today | Installs are refused | `commit_update()` — no precondition; settles the running slot's digit, keeps the boot order, resets both boot budgets |
| `INCOMPLETE_FW_UPDATE` | 2 | `update_firmware()` succeeded, no reboot yet | Installs are refused; `commit_update()` throws `updater::MissingReboot` | Reboot, then `commit_update()` |
| `INCOMPLETE_FW_UPDATE`, never activated | 2 | The install was interrupted before RAUC changed the boot order (window 1 below) | Installs are refused; `pending_update_actionable()` is `false` and `rollback_firmware()` refuses | `commit_update()` quarantines the interrupted slot; ask `has_stalled_install()` first to tell this from a real update |
| `INCOMPLETE_APP_UPDATE` | 3 | `update_application()` succeeded, no reboot yet | Installs are refused; `commit_update()` throws `updater::MissingReboot` | Reboot, then `commit_update()` |
| `INCOMPLETE_APP_FW_UPDATE` | 4 | Both installed, no reboot yet | Same as 2 and 3 | Reboot, then `commit_update()` |
| `FAILED_FW_UPDATE` | 5 | The firmware install threw — RAUC rejected or failed the bundle, or the install path failed before it | The device runs the proven slot; installs are refused | `commit_update()` — marks the target slot bad and returns to 0 |
| `FAILED_APP_UPDATE` | 6 | The application install threw, alone or as the second half of a combined install (the firmware half is then abandoned and the boot order restored) | The device runs the proven application; installs are refused | `commit_update()` — marks the target application slot bad and returns to 0 |
| `ROLLBACK_*_REBOOT_PENDING` | 7–9 | `rollback_firmware()` / `rollback_application()`, no reboot yet | `commit_update()` refuses until the evidence shows the reboot | Reboot, then `commit_update()` |
| `INCOMPLETE_*_ROLLBACK` | 10–12 | Carried in from an older generation; nothing writes them today | Installs are refused | `commit_update()` — accepted without the reboot evidence 7–9 need; for 10 and 12 the rollback commit can still refuse with `updater::MissingReboot` |
| `UNKNOWN_STATE` | 13 | `update_reboot_state` is absent, unreadable or outside `0`–`12` | `commit_update()` refuses with `fs::NotAllowedUpdateState`, both rollback verbs with `updater::RebootStateNotInterpretable`, installs with `fs::UpdateInProgress` | Nothing in the library leads out. Find out what wrote the value, then store the state that matches the slots — `0` if nothing is pending — with `fw_setenv` or `FSUpdate::update_reboot_state()` |

States 5 and 6 are **not** rolled back: a failed install never left the proven
slot, so there is nothing to undo. `commit_update()` checks that the slot
bitfield matches the stored state — for 5 and 6 an uncommitted digit on the
target slot — and refuses with `fs::NotAllowedUpdateState`, naming what it
expected, when it does not.

### Handling a stuck state

```cpp
using update_definitions::UBootBootstateFlags;

switch (updater.get_update_reboot_state()) {
case UBootBootstateFlags::FW_UPDATE_REBOOT_FAILED:
case UBootBootstateFlags::FAILED_FW_UPDATE:
case UBootBootstateFlags::FAILED_APP_UPDATE:
case UBootBootstateFlags::INCOMPLETE_FW_ROLLBACK:
case UBootBootstateFlags::INCOMPLETE_APP_ROLLBACK:
case UBootBootstateFlags::INCOMPLETE_APP_FW_ROLLBACK:
    updater.commit_update();   // settles the state, see the table above
    break;
case UBootBootstateFlags::UNKNOWN_STATE:
    // nothing in the library leads out; alert and inspect the environment
    break;
default:
    // 0: idle; 2-4 and 7-9: reboot first, then commit_update()
    break;
}
```

### Commit before the reboot

If the process that installed the update restarts without a reboot, the state
stays at 2, 3 or 4. `commit_update()` then throws `updater::MissingReboot` (a
subclass of `fs::NotAllowedUpdateState`), because the evidence it reads — the
booted slot and the mounted application image — still shows the old slot.
**Reboot into the new slot before committing.** For state 3,
`commit_update()` refuses with `updater::GetLoopDevices` when no application
image is mounted at all.

### Power loss during a firmware install

The state and the target's digit are written before RAUC starts. What a power
loss leaves behind depends on how far RAUC got; both cases are described in
[The install's two interruption windows](#the-installs-two-interruption-windows).

## Apply writes nothing

`apply_pending_update()` performs no durable transition in any branch. It reads
the state and answers one question: does this state still need a reboot to take
effect? It returns `true` for 2, 3, 4, 7, 8 and 9 and throws
`fs::ApplyUpdateInvalidState` for every other state, including 10–12, whose
next step is a commit rather than a reboot.

That is not an omission. The install already activates its target — the
bootloader backend's set-primary writes the boot order **and** the target's
budget in one step, before control returns — so the next boot, prescribed or
accidental, is the trial boot either way. Anything apply wrote on top would be
a transition the accidental path does not get, and the two must stay
indistinguishable.

The one write it used to perform, marking the other slot good, was also unsafe:
"other" is relative to the running slot, and the pending state survives a
bootloader fallback. After a fallback it named the slot that had just failed,
and re-arming that slot erased the evidence the commit reads to recognise the
failure.

## The install's two interruption windows

An interrupted install does not leave one state but two, and only one of them
was ever handled:

1. **Before the backend deactivates the target.** The state and digit are
   flushed first, so the boot orders are still equal. All three reboot
   predicates require them to differ, so none holds — commit threw, rollback
   wrote nothing, a further install was refused, and the counter gate kept
   eroding the proven slot. A fourth classifier arm now settles this: it
   quarantines the slot carrying the uncommitted digit — found from the digits,
   never from the running slot, which after the decay may already be the
   healthy one — re-arms both budgets and returns to idle.

2. **During the image write.** The target is out of the rotation and its budget
   is zeroed, so the orders differ and a budget is zero: the failed-reboot
   branch already owns this one and restores the full order.

A commit that settles the first window still returns `true`, like an ordinary
commit. The caller is not running the update it just committed — that update
was discarded and its slot quarantined — so a caller that reports the outcome
upward asks `has_stalled_install()` **before** committing and reports the
settle as distinct from success; otherwise it would tell a fleet backend the
device runs a version it never booted.

The combined install writes the firmware digit and state 2 first and the
combined state only after the activation, so window 1 applies there too, and
the combined state with equal orders cannot occur.

## Related documents

- [`architecture.md`](architecture.md) — component design; links here for the diagram
- [`reference/api.md`](reference/api.md) — the `FSUpdate` calls named in this document
- [`reference/uboot-variables.md`](reference/uboot-variables.md) — the variables behind the states, the slot bitfield and the boot counters
- [`reference/rauc-contract.md`](reference/rauc-contract.md) — what RAUC writes during an install and who resets the counters
- [fs-updater-cli CLI Reference](https://github.com/fsembedded/fs-updater-cli/blob/main/docs/reference/cli.md) — exit codes that map to each state value
