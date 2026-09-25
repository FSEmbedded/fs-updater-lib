# RAUC Integration Contract

How the library drives RAUC, what RAUC writes on its own, and which of the
boot-selection duties the library takes over.

---

## The backend: RAUC's D-Bus interface

The library talks to the RAUC daemon over the D-Bus **system bus**, through
`rauc::rauc_dbus_client` (`src/dbus/rauc_dbus_client.h`). It never runs the
`rauc` command line. The peer is RAUC's own service:

| | |
|---|---|
| Bus name | `de.pengutronix.rauc` |
| Object path | `/` |
| Interface | `de.pengutronix.rauc.Installer` |

What the library's flows use of that interface:

| Purpose | Used |
|---------|------|
| Install a firmware or application bundle | `InstallBundle(path, {})`, then wait for the `Completed` signal; `Progress` feeds the install progress callback, `LastError` is added to the failure report |
| Tell an application bundle from a firmware bundle | `InspectBundle(path, {})`, reading the manifest `compatible` (raw `.raucb` input only) |
| Notice a daemon that disappears mid-install | `org.freedesktop.DBus.NameOwnerChanged` for `de.pengutronix.rauc` |

The client also wraps `Mark` and `GetSlotStatus`, but no `FSUpdate` call uses
them: marking slots good or active is not part of the library's update or
rollback flows (see [below](#who-resets-the-counters)).

This is RAUC's interface, not one the library offers. The library itself
exposes no D-Bus interface. The `de.fsembedded.fsupdate1` service that the
CLI and the Azure Device Update handler talk to is `fs-updater-service`, a
separate component built on this library; its protocol is documented in
`fs-updater-cli` (`docs/integration/dbus-session-protocol.md`).

Failures surface as `rauc::RaucBaseException` subclasses —
`RaucInstallBundle` for a rejected or failed install (with RAUC's report),
`RaucGetArtifactInformation` for `InspectBundle`, `RaucServiceUnavailable` when
the daemon is not on the bus or leaves it during an install. The firmware path
rewraps them as `updater::FirmwareUpdateInstall`; see the
[API Reference](api.md#exceptions) for which call throws what.

When an install fails, the client puts `BOOT_ORDER` back to `BOOT_ORDER_OLD`
if the two differ, so a half-written target is not left at the head of the
boot order.

---

## What RAUC writes during an install

RAUC is configured with `bootloader=uboot` (see
[RAUC system.conf](../integration/rauc-system-conf.md)). Before starting a
firmware install, the library itself sets `BOOT_ORDER` and `BOOT_ORDER_OLD`
to the same value, running slot first — the anchor that makes "the two
orders differ" mean "an install moved the order" for a later commit, rather
than an order an earlier fallback already left non-preferring (see the
library's
[state-machine reference](../state-machine.md#transition-diagram), Phase 1).
During `InstallBundle` RAUC's U-Boot backend then writes the boot-selection
variables itself: it takes the target slot out of `BOOT_ORDER` and zeroes its
counter while it writes the image, then makes the target primary — head of
`BOOT_ORDER`, full budget in `BOOT_x_LEFT` — before `Completed` arrives. The
next boot is the trial boot of the new slot whether or not the caller calls
`apply_pending_update()`.

`BOOT_ORDER_OLD` is not RAUC's: the library keeps it as its record of the last
committed order. The variables and their accepted values are listed in
[U-Boot Variables](uboot-variables.md).

---

## Who resets the counters

RAUC's usual contract has two halves: U-Boot decrements `BOOT_x_LEFT` on every
boot attempt and skips a slot whose counter reached 0, and a mark-good step
after a successful boot (`rauc status mark-good`, typically from
`rauc-mark-good.service`) restores the booted slot's counter.

This library does not call RAUC's `Mark` for the second half. It resets the
counters itself, in the U-Boot environment, from `commit_update()`:

- **Nothing pending (state 0):** the running slot's counter is reset to `3` if
  it is below `3`. This is the routine mark-good; `commit_update()` returns
  `true` when it wrote the reset and `false` when there was nothing to do.
- **A pending state it settles:** some settle paths reset both counters to
  `3`, others write no counter at all.

Which settle path resets which counter, and the counter writes
`rollback_firmware()` makes when it prepares a rollback, are listed under
[Boot-attempt counters](uboot-variables.md#boot-attempt-counters).

### Counter drain

If nothing calls a mark-good step — neither `commit_update()` nor RAUC's —
every boot costs the booted slot one attempt. After three boots U-Boot falls
back to the other slot. A system that uses this library for boot confirmation
therefore calls `commit_update()` once per boot, after it has decided the
system is healthy.

Raising the budget above 3 is not an option with this library: it accepts only
`0`–`3` in `BOOT_A_LEFT` and `BOOT_B_LEFT` and refuses to read anything else.
Keep RAUC's `boot-attempts` and `boot-attempts-primary` at 3 or less.
