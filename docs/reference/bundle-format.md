# Bundle Format Reference

What `FSUpdate::update_image()` accepts, how it tells the formats apart, and
the byte layout of the `.fs` container. The call itself is described in the
[API Reference](api.md#install).

No bundle-creation tool ships with this repository; `.fs` containers are
produced by the F&S build system.

---

## Accepted inputs

With an empty `update_type`, `update_image()` reads the first 64 bytes of the
file and routes on them. No file extension is consulted.

| Leading bytes | Format | Result |
|---------------|--------|--------|
| `"FSLX"`, byte 15 = `0x20`, bytes 16–21 = `"FSUPv2"` | `.fs` container, v2.0 | Members are extracted and installed (layout below) |
| `"FSLX"`, byte 15 = `0x10` | `.fs` container, v1.0 (tar.bz2 payload) | Refused: `fs::UpdateFormatNotSupported` |
| `"FSLX"`, anything else | — | Refused: `fs::UnknownUpdateFormat` |
| `"hsqs"` | Raw RAUC bundle (`.raucb`) | Installed directly, as application if the bundle's `compatible` ends in `-appfs`, otherwise as firmware |
| 4-byte big-endian value `1` at offset 8 | Raw F&S application image | Installed directly as application (layout below) |
| anything else | — | Refused: `fs::UnknownUpdateFormat` |

The v1.0 container is recognised only to be refused with a clear error; it
cannot be installed by this release. Both refusals derive from
`fs::GenericException`.

A library built with `FUS_LEGACY_IMAGE_SUPPORT=OFF` refuses the `.fs`
container and the raw application image as well, with
`fs::UpdateFormatNotSupported`. Only raw RAUC bundles remain installable.

### Explicit `update_type`

`update_type = "fw"` or `"app"` skips the detection above and installs the file
as that single component, from its original path. Any other non-empty value
installs nothing and throws `fs::GenericException` (`EPERM`, "Invalid update").
`fs-updater-service` always passes an empty `update_type`.

### `installed_update_type`

| Value | Installed | Next state |
|-------|-----------|------------|
| 1 | Firmware | `INCOMPLETE_FW_UPDATE` (2) |
| 2 | Application | `INCOMPLETE_APP_UPDATE` (3) |
| 3 | Firmware and application | `INCOMPLETE_APP_FW_UPDATE` (4) |

---

## `.fs` container, v2.0

```
offset 0      F&S header                  64 bytes
offset 64     descriptor length           4 bytes, little-endian, at most 64 KiB
offset 68     descriptor                  JSON, <length> bytes
...           members                     raw bytes at the offsets the descriptor names
```

### F&S header (`fs_header_v1_0`, 64 bytes)

| Offset | Size | Field | Value |
|-------:|-----:|-------|-------|
| 0 | 4 | `magic` | `"FSLX"` |
| 4 | 4 | `file_size_low` | Byte count after the header, bits 31–0 |
| 8 | 4 | `file_size_high` | Byte count after the header, bits 63–32 |
| 12 | 2 | `flags` | `uint16`, not interpreted |
| 14 | 1 | `padsize` | Not interpreted |
| 15 | 1 | `version` | `0x20` (`[7:4]` major, `[3:0]` minor) |
| 16 | 16 | `type` | `"FSUPv2"`, NUL-padded |
| 32 | 32 | `param` | Not interpreted |

`file_size` counts everything after the 64-byte header — the length prefix,
the descriptor and the members — so the container ends at `64 + file_size`.

### Descriptor

```json
{
  "version": "2.4.0",
  "fw_version": "2.4.0",
  "app_version": "1.7.2",
  "members": [
    { "name": "update.fw",  "type": "firmware", "offset": 1024,    "size": 52428800, "sha256": "<64 lowercase hex digits>" },
    { "name": "update.app", "type": "app",      "offset": 52429824, "size": 10485760, "sha256": "<64 lowercase hex digits>" }
  ]
}
```

| Key | Required | Meaning |
|-----|:--------:|---------|
| `version` | yes | Bundle version string |
| `fw_version`, `app_version` | no | Component versions, reported by `fs::inspect_bundle()` |
| `members` | yes | Non-empty array |
| `members[].name` | yes | Non-empty, unique, no control characters; used in logs and errors |
| `members[].type` | yes | `"firmware"`, `"app"` or `"manifest"`; any other value is skipped |
| `members[].offset` | yes | Absolute byte offset from the start of the file |
| `members[].size` | yes | Byte count |
| `members[].sha256` | yes | Lowercase hex digest of the member's bytes |

The container is rejected before anything is extracted when the descriptor is
malformed, when there is more than one `firmware` or more than one `app`
member, or when a non-empty member lies outside the region after the
descriptor or overlaps another one. `manifest` members are skipped.

Each `firmware` and `app` member is streamed to disk and hashed on the way;
the comparison with `sha256` is exact, so the digest has to be lowercase. A
container without any `firmware` or `app` member installs nothing and throws
`fs::GenericException` (`EPERM`).

### Payloads

| Member type | Staged as | Content |
|-------------|-----------|---------|
| `firmware` | `update.fw` | A RAUC firmware bundle |
| `app` | `update.app` | A RAUC application bundle, or a raw F&S application image |

### Staging directory

Members are written to the directory that contains the RAUC scratch path —
`/rw_fs/.cache/` for the default `FSUP_RAUC_SCRATCH=/rw_fs/.cache/update.fw`
(see [Contributing](../contributing.md#cmake-options)). A caller can redirect it
per call with `update_image()`'s `rauc_scratch_path` argument unless the
library was built with `BUILD_RAUC_SCRATCH_OVERRIDE=OFF`.

Each member streams into `update.fw.tmp` or `update.app.tmp`, whatever its
`name`, and is renamed to `update.fw` or `update.app` when complete. Before
extracting, `update_image()` removes the previous install's `update.fw`,
`update.app` and their `.tmp` leftovers, so a small persistent partition does
not have to hold two bundles at once.

---

## Raw F&S application image

Accepted on its own or as the `app` member of a container. Requires
`FUS_LEGACY_IMAGE_SUPPORT=ON`.

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8 | SquashFS size (`uint64`, big-endian) |
| 8 | 4 | Header version (`uint32`, big-endian), must be `1` |
| 12 | 4 | CRC32 over bytes 0–11 |
| 16 | SquashFS size | SquashFS content |
| after SquashFS | 26 | Signing timestamp |
| after timestamp | variable | PSSR(SHA-256) signature |
| after signature | variable | Signing certificate, then optional intermediate CA certificate (PEM, each starting on a new line) |

The signature covers the SquashFS content and the timestamp. The certificate
chain is validated against the keyring named in RAUC's `system.conf` (X.509
path validation, codeSigning EKU `1.3.6.1.5.5.7.3.3`, certificate valid at the
signing time) — see [RAUC system.conf](../integration/rauc-system-conf.md#keyring).
A RAUC application bundle takes none of these steps: RAUC verifies it against
its own keyring.

---

## Raw RAUC bundle

A standard RAUC bundle, format defined by the RAUC project
([rauc.io](https://rauc.io/)). The library hands it to the RAUC daemon; see
[RAUC Integration Contract](rauc-contract.md). Whether it is a firmware or an
application bundle is decided by its manifest `compatible`: a value ending in
`-appfs` marks an application bundle.
