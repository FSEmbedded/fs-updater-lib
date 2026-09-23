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
[RAUC system.conf](../integration/rauc-system-conf.md)). During
`InstallBundle` its U-Boot backend writes the boot-selection variables itself:
it takes the target slot out of `BOOT_ORDER` and zeroes its counter while it
writes the image, then makes the target primary — head of `BOOT_ORDER`, full
budget in `BOOT_x_LEFT` — before `Completed` arrives. The library does not
write the boot order for an install; the next boot is the trial boot of the
new slot whether or not the caller calls `apply_pending_update()`.

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
- **Any pending state it settles:** both counters are reset to `3`. While a
  state is pending the routine mark-good does not run, so the boots in between
  may have drained them.

`rollback_firmware()` writes the counters as part of preparing a rollback; the
exact writes are listed under
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
