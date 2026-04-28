# API Reference

Header: `<fs_update_framework/handle_update/fsupdate.h>`

All public types are in the `fs::` and `update_definitions::` namespaces.
Exceptions are thrown on error; see the [Exception hierarchy](#exception-hierarchy) below.

---

## `fs::FSUpdate`

The single entry point for all update operations. Not thread-safe — use one
instance per thread.

### Construction

```cpp
explicit FSUpdate(const std::shared_ptr<logger::LoggerHandler>& logger);
```

Initialises the U-Boot interface, Bootstate handler, and work-directory path.
Throws `UBoot::UBootError` if the U-Boot environment cannot be opened.

### Work directory

```cpp
bool        create_work_dir();
std::filesystem::path get_work_dir();
```

`create_work_dir()` creates `/tmp/adu/.work/` (or the value of
`TEMP_ADU_WORK_DIR` set at build time) if it does not already exist.
Returns `true` if created, `false` if it already existed.
Throws `fs::GenericException` if creation fails.

Must be called before any `update_*` method.

### Install — new procedure (`.fs` bundle)

```cpp
void update_image(std::string& path_to_update_image,
                  std::string& update_type,
                  uint8_t&     installed_update_type);
```

Primary entry point for `.fs` bundles. Extracts the tar.bz2 payload, parses
`fsupdate.json`, verifies SHA-256 checksums, then dispatches to the
appropriate `update_*` method below.

| `update_type` | Effect |
|---------------|--------|
| `""` (empty) | Install all components declared in the manifest |
| `"fw"` | Install only the firmware payload from the manifest |
| `"app"` | Install only the application payload from the manifest |

`installed_update_type` out-parameter values: `1` = firmware, `2` = application,
`3` = both.

Throws `fs::UpdateInProgress` if `update_reboot_state != 0`.

### Install — old procedure (component files)

```cpp
void update_firmware(const std::string& path_to_firmware);
void update_application(const std::string& path_to_application);
void update_firmware_and_application(const std::string& path_to_firmware,
                                     const std::string& path_to_application);
```

Lower-level methods that operate on already-extracted files. Called internally
by `update_image()`. Can also be called directly when working with raw RAUC
bundles (`.raucb`) or raw application images.

All three throw `fs::UpdateInProgress` if `update_reboot_state != 0`.

### Commit / rollback

```cpp
bool commit_update();
```

Confirms the active update or rollback. Reads the current `update_reboot_state`
and calls the appropriate `Bootstate::confirm*()` method.

Returns `true` if an update was committed, `false` if already idle (state 0).
Throws `fs::NotAllowedUpdateState` if the state is not a valid commit target.

```cpp
void rollback_firmware();
void rollback_application();
```

Initiate a rollback of the respective component. Sets `update_reboot_state` to
`ROLLBACK_FW_REBOOT_PENDING` (7), `ROLLBACK_APP_REBOOT_PENDING` (8), or
`ROLLBACK_APP_FW_REBOOT_PENDING` (9) depending on what is pending.
A reboot is required to complete the rollback.

### State query

```cpp
update_definitions::UBootBootstateFlags get_update_reboot_state();
```

Returns the current state machine position. See
[State Machine](../state-machine.md) for the full table.

```cpp
bool is_reboot_complete(bool firmware);
bool pendingUpdateRollback();
```

`is_reboot_complete(true)` — returns `true` if the firmware reboot has completed
(new slot booted).
`is_reboot_complete(false)` — same for application.
`pendingUpdateRollback()` — returns `true` if a rollback is pending reboot.

### Version query

```cpp
version_t get_firmware_version();
version_t get_application_version();
```

Reads firmware and application version strings from the U-Boot environment.
Always exit 0 on success (throw on U-Boot access error).

### Slot-state management

```cpp
int  set_update_state_bad(const char& state, uint32_t update_id);
bool is_update_state_bad (const char& state, uint32_t update_id);
```

Mark or query a slot as bad (unbootable). `state` is `'A'` or `'B'`.
`update_id`: `0` = firmware, `1` = application. Bad slots are excluded from
rollback targets.

---

## `update_definitions::UBootBootstateFlags`

Defined in `<fs_update_framework/handle_update/updateDefinitions.h>`.

```cpp
enum class UBootBootstateFlags : unsigned char {
    NO_UPDATE_REBOOT_PENDING        = 0,
    FW_UPDATE_REBOOT_FAILED         = 1,
    INCOMPLETE_FW_UPDATE            = 2,
    INCOMPLETE_APP_UPDATE           = 3,
    INCOMPLETE_APP_FW_UPDATE        = 4,
    FAILED_FW_UPDATE                = 5,
    FAILED_APP_UPDATE               = 6,
    ROLLBACK_FW_REBOOT_PENDING      = 7,
    ROLLBACK_APP_REBOOT_PENDING     = 8,
    ROLLBACK_APP_FW_REBOOT_PENDING  = 9,
    INCOMPLETE_FW_ROLLBACK          = 10,
    INCOMPLETE_APP_ROLLBACK         = 11,
    INCOMPLETE_APP_FW_ROLLBACK      = 12,
    UNKNOWN_STATE                   = 13,
};
```

See [State Machine](../state-machine.md) for the full description and
transition diagram.

---

## `UBoot::UBoot`

Defined in `<fs_update_framework/uboot_interface/UBoot.h>`. Thread-safe (all
operations are mutex-protected). Batch writes to minimise flash wear.

```cpp
// Read a variable directly from the U-Boot environment
std::string getVariable(const std::string& name);

// Typed overloads — throws UBootEnvValueNotAllowed if the value is not in allowed
uint8_t     getVariable(const std::string& name, const std::vector<uint8_t>& allowed);
std::string getVariable(const std::string& name, const std::vector<std::string>& allowed);
char        getVariable(const std::string& name, const std::vector<char>& allowed);

// Stage a variable for writing (does NOT touch flash)
void addVariable(const std::string& key, const std::string& value);

// Write all staged variables to flash in a single operation
void flushEnvironment();

// Discard staged variables without writing
void freeVariables();
```

**Write pattern — always batch:**

```cpp
uboot.addVariable("update",               "0010");
uboot.addVariable("update_reboot_state",  "2");
uboot.flushEnvironment();   // single flash write
```

Never call `addVariable()` without a following `flushEnvironment()`. Staged
variables are lost on process exit if not flushed.

---

## `logger::LoggerHandler`

Defined in `<fs_update_framework/logger/LoggerHandler.h>`. Thread-safe.

```cpp
static std::shared_ptr<LoggerHandler>
    initLogger(std::shared_ptr<LoggerSinkBase>& sink);

void setLogEntry(const std::shared_ptr<LogEntry>& msg);
```

Available sinks:

| Class | Header | Behaviour |
|-------|--------|-----------|
| `LoggerSinkStdout` | `LoggerSinkStdout.h` | Writes to stdout |
| `LoggerSinkEmpty` | `LoggerSinkEmpty.h` | Discards all messages (null sink) |

---

## Exception hierarchy

All exceptions derive from `std::exception` and provide `what()`.

| Base class | Thrown by | Cause |
|------------|-----------|-------|
| `fs::BaseFSUpdateException` | `fs::*`, `updater::*` | Update, commit, rollback, verification, state-transition errors |
| `rauc::RaucBaseException` | `rauc_handler` | RAUC install, mark, status, JSON parse failures |
| `UBoot::UBootError` | `UBoot::UBoot` | Environment read/write, value-validation failures |

Catch `fs::BaseFSUpdateException` as a safe catch-all for the update framework.
`rauc::RaucBaseException` provides an additional `report()` method returning
the RAUC JSON output on install failures.
