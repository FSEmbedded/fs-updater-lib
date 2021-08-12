## F&S-Update-Framework
The __F&S Update Framework (FSUF)__ uses several open source frameworks to update U-Boot, Kernel, Root File System and Application. FSUF follows the idea of always having a working configuration. For this we implement the concept of A/B updates.

For updating U-Boot, Kernel and RootFS we use RAUC:
RAUC is a lightweight update client that runs on your embedded device and reliably controls the procedure of updating your device with a new firmware revision. RAUC is also the tool on your host system that lets you create, inspect and modify update artifacts for your device. For more information how RAUC works, please visit the website of [RAUC](https://rauc.io/).
For minimizing the memory requirements we compress the file system by using [SquashFS](https://www.kernel.org/doc/html/latest/filesystems/squashfs.html).
