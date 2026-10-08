> **Experimental PPS/LN8000 candidate:** built October 3, not installed or hardware-qualified in the retained evidence. For the preserved installed source use [`public/munch-current`](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/tree/public/munch-current).

# E404 for Poco F4 / munch

My E404-based kernel work for Poco F4, developed alongside CryoManager/CryoD. The main focus is memory management: making the Linux 4.19 MGLRU backport behave correctly, improving targeted reclaim and ZRAM, and keeping the phone responsive with more apps alive.

Built on [E404 by kvsnr113 and contributors](https://github.com/kvsnr113/xiaomi_sm8250_kernel_e404), Linux/Android/Qualcomm/Xiaomi sources, and the work credited in the original commits. This is an independent fork. Backports remain credited to their original authors.

**Here for the MGLRU fixes? Start with the [patch guide](Documentation/e404/MGLRU-PATCHES.md).** It links the reclaim/accounting repairs, explains the userfaultfd charge-order bug, and includes focused patches and regression commands.

## Source versions

- **`public/munch-current`**: preserved K112 `recoveryfix1` source, the corresponding KernelSU integration, build configuration and regression tests. This is the last installed kernel identified in the retained device evidence.
- **`experimental/munch-charging`**: the later PPS/LN8000 recovery candidate. Built locally on October 3; not installed or hardware-qualified in the available receipt.
- Older branches preserve the development history. They are intermediate experiments, not a list of recommended kernels.

The October 8 publication is a **source release for review and reuse**. It does not introduce a newly qualified flashable package. Munch was unavailable for a fresh kernel readback during publication; retained October 3 evidence identifies K112 recoveryfix1. Owner reports describe good daily behavior, while the tests here cover specific defects rather than every workload or every ROM.

## Get the complete source

```sh
git clone --recurse-submodules -b public/munch-current https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404.git
cd xiaomi_sm8250_kernel_e404
```

Use the release's **complete-source tarball** for an offline copy including KernelSU. GitHub's automatic source ZIP omits submodule contents.

[Build instructions](Documentation/e404/BUILDING.md) · [Source provenance](Documentation/e404/SOURCE-PROVENANCE.json) · [Distribution notes](Documentation/e404/DISTRIBUTION.md)

## License

The kernel is GPL-2.0-only, with the Linux syscall exception described in [COPYING](COPYING). Per-file and third-party notices remain in place; see [LICENSES](LICENSES) and the pinned [KernelSU source](KernelSU). Modifications and their dates are recorded in Git history. The source is available for review, modification and redistribution under its applicable licenses.
