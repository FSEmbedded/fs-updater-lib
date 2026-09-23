# API Reference

Header: `<fs_update_framework/handle_update/fsupdate.h>`. Signatures below are
copied from the installed headers; where this page and a header disagree, the
header wins.

The state machine the calls move through is described once, in
[State Machine](../state-machine.md); this page links to it instead of
repeating the per-state rules.

---

## `fs::FSUpdate`

The entry point for every update operation. Not copyable, not movable, not
thread-safe: one instance per thread.

### Construction

```cpp
explicit FSUpdate(const std::shared_ptr<logger::LoggerHandler>& logger);
FSUpdate(std::shared_ptr<UBoot::IUBootEnv> env,
         const std::shared_ptr<logger::LoggerHandler>& logger);
```

The first form opens the device's U-Boot environment through `libubootenv`,
using the configuration compiled in as `UBOOT_CONFIG_PATH` (default
`/etc/fw_env.config`); it throws `UBoot::UBootEnv` when `libubootenv` cannot
be initialised or the configuration cannot be read. The second form takes any
`UBoot::IUBootEnv` implementation — the seam the unit tests use to run against
an in-memory environment.

### Work directory

```cpp
bool        create_work_dir();
std::string get_work_dir();
```

`create_work_dir()` creates `TEMP_ADU_WORK_DIR` (default `/tmp/adu/.work`)
with mode `0777` if it does not exist. Returns `true` if it created the
directory, `false` if it already existed; throws `fs::GenericException` with
the `errno` of the failing call otherwise. The directory is for marker files
that callers running as different users share; the install calls do not need
it.

### Install

```cpp
void setInstallProgressCallback(updater::ProgressCb callback);   // std::function<void(int)>

void update_image(std::string&       path_to_update_image,
                  std::string&       update_type,
                  uint8_t&           installed_update_type,
                  const std::string& rauc_scratch_path = {});

void update_firmware(const std::string& path_to_firmware);
void update_application(const std::string& path_to_application);
void update_firmware_and_application(const std::string& path_to_firmware,
                                     const std::string& path_to_application);
```

`update_image()` is the one entry point for a whole bundle. It detects the
input format, extracts a `.fs` container's members, and dispatches to one of
the three component calls below it. What it accepts, how `update_type`
changes that, and the values of `installed_update_type` are in
[Bundle Format](bundle-format.md#accepted-inputs). `rauc_scratch_path`
redirects the staging directory for this call; it is ignored in a library
built with `BUILD_RAUC_SCRATCH_OVERRIDE=OFF`.

`update_firmware()` installs a RAUC firmware bundle. `update_application()`
installs a RAUC application bundle or a raw F&S application image.
`update_firmware_and_application()` does both, firmware first.

The progress callback receives 0–100 during any of the four calls. Register it
before the install; replacing it while an install runs is not safe. Under
`update_image()` a container's extraction takes the first 20 %.

All four refuse with `fs::UpdateInProgress` unless the stored state is 0. The
state check is not the first thing they do:

- `update_image()` first creates the staging directory and deletes the previous
  install's `update.fw`, `update.app` and their `.tmp` files from it, whatever
  the state. With an empty `update_type` it then detects the format and
  extracts a whole `.fs` container before the component call it dispatches to
  checks the state.
- `update_application()` and `update_firmware_and_application()` construct the
  application handler before the check, so without a RAUC `system.conf` they
  throw `std::runtime_error` rather than `fs::UpdateInProgress`.

With state 0 they record the pending state **before** handing the bundle to
RAUC; if the install then fails they record the failed state (5 or 6) and
rethrow. See [State Machine](../state-machine.md#transition-diagram).

### Commit

```cpp
bool commit_update();
```

Settles whatever the stored state says is pending. For most states it first
checks the evidence — the booted slot, the boot order and counters, the mounted
application image; it enters 1 and 10–12 on the stored value alone, and 5 and 6
after checking only the slot bitfield. Returns `true` when it wrote anything: a settled state, or,
with nothing pending, the running slot's boot counter restored to 3 (the
routine mark-good, see
[RAUC Integration Contract](rauc-contract.md#who-resets-the-counters)).
Returns `false` when nothing was pending and nothing needed restoring.

It throws when the evidence does not support the stored state:

| Exception | When |
|-----------|------|
| `updater::MissingReboot` (derives from `fs::NotAllowedUpdateState`) | The update or rollback needs a reboot that has not happened |
| `fs::NotAllowedUpdateState` | The state has no commit (13), or the slot bitfield does not match it; the message names what was expected |
| `updater::GetLoopDevices` | State 3 and no application image is mounted |
| `updater::FirmwareRebootStateNotDefined` | A firmware state whose boot variables fit none of the known outcomes |

Which states it settles, and with what effect, is listed per state in
[Stale and stuck states](../state-machine.md#stale-and-stuck-states).

```cpp
bool has_stalled_install();
bool pending_update_actionable();
```

`has_stalled_install()` is `true` when the stored state records a firmware
install that was interrupted before RAUC changed the boot order.
`commit_update()` settles that state and returns `true` like any other commit
— but nothing new is running, so a caller that reports the outcome asks this
**before** committing. `pending_update_actionable()` is `false` when a pending
firmware state (2 or 4) names an install whose slot is not at the head of the
boot order — the stalled install, or one interrupted while RAUC wrote the
image — and `true` otherwise. Such an install can be neither committed as an
update nor rolled back; `rollback_firmware()` refuses it.

### Apply

```cpp
[[nodiscard]] bool apply_pending_update();
```

Answers whether the pending state still needs a reboot; writes nothing.
Returns `true` for states 2, 3, 4, 7, 8 and 9; throws
`fs::ApplyUpdateInvalidState` for every other state. See
[Apply writes nothing](../state-machine.md#apply-writes-nothing).

It calls nothing in RAUC either: for 2 and 4 the install already made the new
slot primary, so there is nothing left to mark.

### Rollback

```cpp
void rollback_firmware();
void rollback_application();
```

With an update pending (2, 3 or 4), undo it:

- **Before the update's reboot** the new slot never ran, so the rollback takes
  effect at once: the boot order (firmware) or the `application` variable
  points back at the proven slot, the abandoned slot's digit is settled, and
  the state returns to 0. No reboot is needed.
- **After the update's reboot** the device runs the new slot, so the rollback
  is prepared instead: the state becomes 7 or 8, and a reboot followed by
  `commit_update()` completes it.

A combined update (4) is rolled back with `rollback_firmware()`, which takes
back both components and always stores 9. Before the update's reboot that 9
cannot be committed; see
[Rolling back a combined update before its reboot](../state-machine.md#rolling-back-a-combined-update-before-its-reboot).
`rollback_application()` on state 4 returns without writing anything.

After a bootloader fallback on state 2, `rollback_firmware()` only logs: the
fallback already undid the update, and `commit_update()` settles it.

With nothing pending, switch to the other slot: the state becomes 7 or 8, and
again a reboot and `commit_update()` complete it.

| Exception | When |
|-----------|------|
| `updater::RebootStateNotInterpretable` | The stored state is 13 |
| `fs::GenericException`, "Commit for rollback required" | `pendingUpdateRollback()` is `true`: a prepared rollback that the evidence shows is waiting for its commit |
| `fs::GenericException`, `errno` `ECANCELED` | The target slot is uncommitted, or (firmware) the pending install never reached the boot order |
| `fs::GenericException`, `errno` `EPERM` | The target slot is marked bad |
| `fs::GenericException`, `errno` `ENOENT` | (application) The target slot was never provisioned |

The rollback verbs refuse 13 and 10–12 on the stored value alone; their other
refusals come from the evidence. They do not check for 1, 5 or 6:
with one of those stored, a rollback that is not refused by the target slot's
digit takes the slot-switch path and overwrites the state. Commit 1, 5 and 6
before rolling anything back.

A prepared rollback whose reboot is still outstanding is not refused either,
because `pendingUpdateRollback()` is still `false` for it. On a slot switch
(7 or 8 stored from state 0) a second `rollback_firmware()` stages the same
switch again, and a second `rollback_application()`, while the image it
switched away from is still mounted, switches `application` back and leaves 8
stored.

`rollback_application()` with nothing pending does not check whether the other
slot was itself just rolled back away from; called twice it switches back.
Callers that act on application health decide from the stored state first.

### State queries

```cpp
update_definitions::UBootBootstateFlags get_update_reboot_state();
RebootCompleteState                     is_reboot_complete(bool firmware);
updater::Bootstate::AppRollbackOutcome  classify_app_rollback();
bool                                    pendingUpdateRollback();
```

`get_update_reboot_state()` returns the stored state; an absent or
unreadable value reads as `UNKNOWN_STATE` rather than throwing. The enum is
documented in [State Machine](../state-machine.md#state-encoding).

`is_reboot_complete()` returns `fs::RebootCompleteState`:

| Value | Meaning |
|-------|---------|
| `COMPLETE` | The expected slot is live |
| `PENDING` | The reboot has not happened yet |
| `INDETERMINATE` | Unanswerable: for the application, no image is mounted at all; for the firmware, an install is in flight for a slot the boot order does not prefer |

`classify_app_rollback()` classifies a prepared application rollback without
writing anything: `REBOOT_OUTSTANDING` (the other slot is still mounted),
`COMMIT_REQUESTED` (the active slot is mounted, or the bitfield already settled
it) or `INDETERMINATE` (nothing mounted; the commit is still owed).
`commit_update()` derives its precondition for state 8 from the same call.

`pendingUpdateRollback()` is `true` when a prepared rollback is waiting for its
commit: always for 10–12; for 7 and 9 when the slot bitfield or the boot order
and counters show it; for 8 unless `classify_app_rollback()` answers
`REBOOT_OUTSTANDING`.

```cpp
void update_reboot_state(update_definitions::UBootBootstateFlags flag);
```

Stores `flag` as the state, unconditionally. Refuses `UNKNOWN_STATE` with
`updater::RebootStateNotInterpretable`. It bypasses every check the other
calls make; it exists for recovery, not for driving updates.

### Versions

```cpp
version_t get_firmware_version();
version_t get_application_version();
```

Read the first line of `/etc/fw_version` and `/etc/app_version`. `version_t`
is `std::string`, or `uint64_t` when the library is built with
`update_version_type=uint64`. Failures throw `updater::GetFirmwareVersion` and
a `std::runtime_error` respectively. `get_application_version()` constructs the
application handler first, so it also needs RAUC's `system.conf` (see
[RAUC system.conf](../integration/rauc-system-conf.md#which-file-is-used)).

### Slot bad marks

```cpp
int  set_update_state_bad(const char& state, uint32_t update_id);
bool is_update_state_bad(const char& state, uint32_t update_id);
```

`state` is `'A'` or `'B'` (either case); `update_id` is `0` for firmware, `1`
for application. `set_update_state_bad()` sets the bad bit of that slot's
digit in the `update` variable and returns `0`, or returns `EINVAL` for an
invalid argument. A bad slot is refused as a rollback or switch target. The
bitfield is described in
[U-Boot Variables](uboot-variables.md#update-variable-format).

### Other

```cpp
std::string& getTempAppPath();
```

`<FSUP_APP_IMG_STORE>/tmp.app`, recorded when an application install is
constructed; empty before the first one. It is the staging path for raw F&S
application images only: a RAUC application bundle's install hook stages
`.incoming.squashfs` in the same directory instead.

---

## Free functions

```cpp
#include <fs_update_framework/handle_update/inspect_bundle.h>
[[nodiscard]] fs::BundleInfo fs::inspect_bundle(std::string_view path) noexcept;

#include <fs_update_framework/library_source_id.h>
const char* fs::library_source_id();
```

`inspect_bundle()` reads a `.fs` container's header and descriptor without
installing it and never throws. `BundleInfo::valid` says the file could be
`stat`ed; `update_type` is `"fw"`, `"app"`, `"fw+app"`, or empty when the file
is not a readable v2.0 container; `version` is the descriptor's `version`,
falling back to the component version; `size` is the file size. A library
built with `FUS_LEGACY_IMAGE_SUPPORT=OFF` has no container reader, so every
file gets that stat-only answer: `update_type` and `version` stay empty.

`library_source_id()` returns the source revision the library was built from,
or `"unknown"`; never null.

---

## `logger`

```cpp
#include <fs_update_framework/logger/LoggerHandler.h>

static std::shared_ptr<LoggerHandler>
    LoggerHandler::initLogger(const std::shared_ptr<LoggerSinkBase>& sink);
void LoggerHandler::setLogEntry(const std::shared_ptr<LogEntry>& msg);

LogEntry(const std::string& domain, const std::string& message, logLevel level);
```

`initLogger()` returns the one handler for that sink, creating it on first
use. Entries are queued and handed to the sink on a worker thread.

A sink implements one method:

```cpp
class LoggerSinkBase {
public:
    virtual void setLogEntry(const std::shared_ptr<logger::LogEntry>& entry) = 0;
};
```

| Sink | Header | Behaviour |
|------|--------|-----------|
| `LoggerSinkStdout(logLevel level)` | `LoggerSinkStdout.h` | Writes entries up to `level` to stdout |
| `LoggerSinkEmpty(logLevel level)` | `LoggerSinkEmpty.h` | Discards everything |

`logLevel` is `ERROR`, `WARNING`, `INFO`, `DEBUG`, from least to most verbose.

---

## `UBoot::IUBootEnv`

The environment seam the library works through. Reads take the list of
accepted values or a validator and throw
`UBoot::UBootEnvVarNotAllowedContent` for anything else; writes are staged
with `addVariable()` and written by `flushEnvironment()`. The variables and
the batching rule are in [U-Boot Variables](uboot-variables.md).

---

## Exceptions

Not everything the library throws shares one base. Catch in this order:

| Base | Namespace | Thrown for |
|------|-----------|------------|
| `fs::BaseFSUpdateException` | `fs::`, `updater::` and the image classes | State refusals, install and rollback failures, format and verification errors, firmware version read |
| `rauc::RaucBaseException` | `rauc::` | RAUC D-Bus failures on the application path (`RaucInstallBundle`, `RaucServiceUnavailable`, …); `report()` returns RAUC's own text, which is also part of `what()` |
| `UBoot::UBootError` | `UBoot::` | Environment open/read/write failures and values outside the accepted set |
| `std::runtime_error` | — | Missing RAUC `system.conf`, application version read, raw application image verification |

The firmware path rewraps RAUC failures as `updater::FirmwareUpdateInstall`
(a `fs::BaseFSUpdateException`); the application path lets
`rauc::RaucBaseException` through. A catch-all therefore ends with
`std::exception`:

```cpp
try {
    updater.update_image(path, type, installed);
} catch (const fs::UpdateInProgress& e) {        // an earlier update is not settled
} catch (const fs::BaseFSUpdateException& e) {
} catch (const rauc::RaucBaseException& e) {     // e.report(): RAUC's text
} catch (const UBoot::UBootError& e) {
} catch (const std::exception& e) {
}
```

`fs::GenericException` carries an `errno` value in its public member
`errorno`; the rollback refusals above are told apart by it. The exit codes
`fs-updater-cli` maps these to are that component's contract, documented in
its `docs/reference/return-codes.md`.

---

## Thread safety

| Component | Thread safety |
|-----------|---------------|
| `logger::LoggerHandler` | Safe; mutex-protected queue with a worker thread |
| `UBoot::UBoot` | Each call is mutex-protected |
| `fs::FSUpdate` | None — one instance per thread |
| `rauc::rauc_dbus_client` | None; blocks while it waits for RAUC |
