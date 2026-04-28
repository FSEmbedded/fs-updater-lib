# RAUC system.conf

`fs-updater-lib` calls `rauc install`, `rauc status mark-good`, and
`rauc status --output-format=json`. All three require a correctly configured
`/etc/rauc/system.conf` on the target device.

## Minimal working configuration

```ini
[system]
compatible=<board-compatible-string>
bootloader=uboot
bundle-formats=verity

[keyring]
path=<path-to-keyring.pem>
; Absolute paths are used as-is.
; Relative paths are prefixed with /etc/rauc/.

[slot.rootfs.0]
device=/dev/mmcblk0p2
type=ext4
bootname=A

[slot.rootfs.1]
device=/dev/mmcblk0p3
type=ext4
bootname=B
```

`bootname` values (`A` / `B`) must match what U-Boot writes to `rauc_cmd`
(`rauc.slot=A` / `rauc.slot=B`). RAUC reads the booted slot from the kernel
cmdline at runtime.

## Counter configuration

```ini
[system]
boot-attempts=3
boot-attempts-primary=3
```

`boot-attempts` controls how many boot attempts RAUC grants when calling
`rauc status mark-good`. Increase this value if the counter drain
risk described in [rauc-contract.md](../reference/rauc-contract.md) is
a concern for your deployment.

## Keyring notes

`CertificateVerifier` reads the keyring path from `system.conf` using the
same parsing logic as RAUC. The keyring file must contain the root CA
certificate (and optionally an intermediate CA certificate) in PEM format.

If you use intermediate CAs:

```
; Option A – separate files
[keyring]
path=/etc/rauc/root.pem

; Option B – bundle (root + intermediate in one file)
[keyring]
path=/etc/rauc/keyring-bundle.pem
```

`CertificateVerifier` loads the keyring once and caches it. Replacing the
keyring file on a running device requires restarting the process that holds
the `FSUpdate` instance.

## Yocto / meta-rauc

Add `FILESEXTRAPATHS:prepend` in your layer's `.bbappend` for
`rauc` to deploy your `system.conf`:

```bitbake
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://system.conf"
```

The `dynamic-overlay` component generates a symlink
`/etc/rauc/system.conf → /etc/rauc/system.conf.<board>` at boot time when
multiple board variants share the same rootfs. See
[dynamic-overlay integration](https://github.com/fsembedded/dynamic-overlay/blob/main/docs/integration/boot-sequence.md)
for details.
