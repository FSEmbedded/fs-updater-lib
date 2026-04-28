# Bundle Format Reference

`fs-updater` supports two update procedures. The new procedure is preferred
for new integrations; the old procedure remains fully supported for existing
pipelines.

## Two procedures at a glance

| Aspect | New procedure (`.fs` bundle) | Old procedure (component files) |
|--------|-----------------------------|---------------------------------|
| CLI invocation | `--update_file <file>.fs` or `--automatic` | `--update_file <file> --update_type fw\|app` |
| Library entry point | `update_image()` | `update_firmware()` / `update_application()` |
| Container | F&S header + tar.bz2 | None — raw artifact |
| Components | fw, app, or both in one file | One component per invocation |
| Type detection | From `fsupdate.json` | Caller-supplied via `--update_type` |
| Recommended for | New integrations | Existing build pipelines |

---

## New procedure: `.fs` bundle

### F&S header (`fs_header_v1_0`, 64 bytes)

| Offset | Size | Field | Value / notes |
|--------|------|-------|---------------|
| 0 | 4 B | `magic` | `"FSLX"` — F&S Linux identifier |
| 4 | 4 B | `file_size_low` | Payload size bits [31:0] |
| 8 | 4 B | `file_size_high` | Payload size bits [63:32] |
| 12 | 1 B | `flags` | Reserved |
| 13 | 1 B | `padsize` | Padding bytes at end of payload |
| 14 | 1 B | `version` | Header version: `[7:4]` major, `[3:0]` minor |
| 15 | 1 B | — | Reserved (padding to 16-byte boundary) |
| 16 | 16 B | `type` | First 4 bytes must be `"CERT"` (validated by `ExtractUpdateStore`) |
| 32 | 32 B | `param` | Union of 8/16/32/64-bit parameters; unused for update bundles |

`file_size` = `(file_size_high << 32) | file_size_low` gives the exact byte
count of the tar.bz2 payload that follows. Source: `UpdateStore.h:21–44`,
validated at `UpdateStore.cpp:148–162`.

### Payload (tar.bz2, immediately after header)

```
tar.bz2
├── fsupdate.json       mandatory manifest
├── update.fw           RAUC bundle — present if firmware update included
└── update.app          raw signed application image — present if app update included
```

At least one of `update.fw` / `update.app` must be present. Filenames are
fixed (`fw_store_name`, `app_store_name` in `UpdateStore.h:49–50`).

### `fsupdate.json` manifest schema

```json
{
  "images": {
    "updates": [
      {
        "version":  "<string>",
        "handler":  "<string>",
        "file":     "update.fw",
        "hashes":   { "sha256": "<lowercase hex>" }
      },
      {
        "version":  "<string>",
        "handler":  "<string>",
        "file":     "update.app",
        "hashes":   { "sha256": "<lowercase hex>" }
      }
    ]
  }
}
```

All four fields (`version`, `handler`, `file`, `hashes.sha256`) are required
per entry. SHA-256 is verified by `UpdateStore::CheckUpdateSha256Sum` before
any slot write; hashes are case-insensitive.

The `file` field maps to the fixed tar entry names (`update.fw`, `update.app`).
Entries not present in the archive are silently omitted from the install.

### `installed_update_type` output values

After a successful `update_image()` call, the out-parameter reports what was
installed:

| Value | Meaning | Next state |
|-------|---------|------------|
| 1 | Firmware installed | `INCOMPLETE_FW_UPDATE` |
| 2 | Application installed | `INCOMPLETE_APP_UPDATE` |
| 3 | Firmware + application installed | `INCOMPLETE_APP_FW_UPDATE` |

### Bundle creation (host side)

No bundle-creation tool ships with these repositories. `.fs` bundles are
produced by the F&S build system. Consult your BSP integration for the
producer-side tooling.

---

## Old procedure: firmware (`.raucb`)

A RAUC bundle — SquashFS image signed per your RAUC keyring configuration.
Passed directly to `update_firmware()` without any F&S header or extraction
step; `--update_type fw` in the CLI bypasses `ExtractUpdateStore` entirely.

RAUC bundle format is defined by the RAUC project:
[rauc.io](https://rauc.io/). The RAUC slot and keyring configuration for
this system is documented in
[integration/rauc-system-conf.md](../integration/rauc-system-conf.md).

---

## Old procedure: application (raw signed squashfs)

A self-describing image with embedded signature and certificate chain.
Passed directly to `update_application()` without extraction.

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8 B | `squashfs_size` (uint64, big-endian) |
| 8 | 4 B | `version` (uint32, big-endian) |
| 12 | 4 B | CRC32 over bytes 0–11 |
| 16 | squashfs_size B | SquashFS content |
| after squashfs | variable | Signing certificate (PEM) |
| — | variable | Intermediate CA certificate (PEM), optional |
| — | 26 B | Timestamp |
| — | variable | PSSR(SHA-256) signature |

The certificate chain is validated by `CertificateVerifier` (X.509 path
validation, codeSigning EKU `OID 1.3.6.1.5.5.7.3.3`). The signature covers
squashfs content + timestamp.
