# RAUC system.conf

The RAUC daemon needs a `system.conf` to install anything the library hands
it. The library reads the same file for two things of its own: it needs the
`[keyring]` entry to construct its application-update handler, and it verifies
raw F&S application images against that keyring.

## Which file is used

The library follows RAUC's own search order; the first file that exists wins:

1. `/etc/rauc/system.conf`
2. `/run/rauc/system.conf`
3. `/usr/lib/rauc/system.conf`

If none exists, the calls that need it — `update_application()`,
`update_firmware_and_application()`, `rollback_application()`,
`get_application_version()`, and `update_image()` when it installs an
application — fail with a `std::runtime_error` that names all three paths.
The file must contain `[keyring] path=`, even in a library built without
legacy image support.

The library resolves the file again every time it constructs its
application-update handler, which it does per call. The RAUC daemon reads its
configuration once, at start-up. A config placed in a higher-priority
directory after the daemon started therefore splits the two until the daemon
restarts.

## Minimal working configuration

```ini
[system]
compatible=<board-compatible-string>
bootloader=uboot
bundle-formats=verity

[keyring]
path=<keyring.pem>

[slot.rootfs.0]
device=/dev/mmcblk0p2
type=ext4
bootname=A

[slot.rootfs.1]
device=/dev/mmcblk0p3
type=ext4
bootname=B
```

`bootname` values must be `A` and `B`: the library reads the booted slot from
the U-Boot variable `rauc_cmd` (`rauc.slot=A` / `rauc.slot=B`) and accepts
nothing else.

An application bundle is recognised by its manifest `compatible` ending in
`-appfs`; see [Bundle Format](../reference/bundle-format.md#raw-rauc-bundle).

## Boot attempts

```ini
[system]
boot-attempts=3
boot-attempts-primary=3
```

The library accepts only `0`–`3` in `BOOT_A_LEFT` and `BOOT_B_LEFT`. Do not
configure more than 3 attempts; see
[RAUC Integration Contract](../reference/rauc-contract.md#counter-drain).

## Keyring

`[keyring] path=` is resolved the way RAUC resolves it: an absolute path is
used as-is, a relative path is taken relative to the directory of the
`system.conf` that was found — `/etc/rauc/`, `/run/rauc/` or `/usr/lib/rauc/`.

Who uses the keyring:

- **RAUC** verifies every RAUC bundle, firmware or application, against it.
- **The library** verifies raw F&S application images against it (only with
  `FUS_LEGACY_IMAGE_SUPPORT=ON`). The keyring is the only trust store:
  certificates embedded in the image are chain candidates, never anchors. The
  file must hold the root CA certificate, plus the intermediate CA certificate
  if images are signed through one, in PEM format — one file, since the entry
  names a single path.

The library reads the keyring file once per install call, so a replaced
keyring applies to its next raw-image install without restarting the caller.
When RAUC picks up a replaced keyring is RAUC's behaviour, not the library's.

## Yocto / meta-rauc

Deploy your `system.conf` from a `rauc` `.bbappend` in your layer:

```bitbake
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://system.conf"
```
