# Building the published Munch source

This is a non-GKI, device-specific Linux 4.19 kernel for Poco F4 (`munch`). An Android version string or a changed `uname` string does not turn it into a 5.10/GKI kernel.

## Inputs

Clone recursively, or extract the complete-source release tarball. The required KernelSU revision is pinned in the parent Git tree and recorded in `SOURCE-PROVENANCE.json`.

The preserved September 27 build used Clang/LLD 22.1.8, ARM64, LLVM integrated assembler, GNU cross prefixes `aarch64-linux-gnu-` and `arm-linux-gnueabi-`, and `KSU_VERSION=32473`. The October 3 charging candidate used Clang/LLD 23.1.1. Compiler artifacts were not archived with cryptographic provenance, so these records are not a bit-for-bit reproducibility claim. LLVM source/releases are available from [llvm-project](https://github.com/llvm/llvm-project).

Install your distribution's normal kernel build prerequisites: make, Python 3, Perl, bc, bison, flex, OpenSSL development headers, libelf development headers, Clang/LLD/LLVM tools, and the above GNU cross toolchains. Set `PATH` to the desired toolchain. `ccache` is optional.

## Build the raw kernel

```sh
JOBS=2 scripts/e404/build-munch.sh
```

The helper uses the archived full configuration, runs `olddefconfig`, then builds `Image`. It has no Telegram dependency, downloads, device commands or automatic flashing. `--prepare-only` stops after configuration; `--help` lists overrides. It uses an advisory compiler lock and a separate output directory. A changed compiler may update configuration metadata; inspect the generated `.config`.

The equivalent historical make options were:

```sh
make O=out ARCH=arm64 SUBARCH=arm64 LLVM=1 LLVM_IAS=1   CC=clang CROSS_COMPILE=aarch64-linux-gnu-   CROSS_COMPILE_COMPAT=arm-linux-gnueabi-   LOCALVERSION=-recoveryfix1 -j2 Image
```

The archived config is `configs/k112-recoveryfix1.config` next to this document. Kernel release: `4.19.404R-K112-A17-SELSCOPE-WALT-CASS-RKSU32473-recoveryfix1`.

## Packaging

`Image` is a raw kernel, not a flashable ZIP or boot image. The historical installed build replaced the kernel inside ROM-specific boot images, preserving that ROM's ramdisk/header/DTB inputs. This source release does not redistribute those ROM images or claim a universal installation procedure.

[Source-only boot repacker](../../scripts/e404/repack-boot.sh) accepts an existing boot image and a newly built Image, using AOSP `unpack_bootimg`/`mkbootimg` to retain its header arguments. It produces a candidate only; AVB signing, device layout and boot/recovery qualification remain release-specific. It never writes a partition.

[AnyKernel3](https://github.com/osm0sis/AnyKernel3) is another common packaging option; the legacy E404 `compile.sh` expects a sibling AnyKernel3 checkout and private Telegram configuration. Do not use that legacy wrapper as the public build entry point. Before distributing a flashable release, include the exact installer source/version, device/ROM limits, checksum, matching source tag and tested rollback procedure. See [DISTRIBUTION.md](DISTRIBUTION.md).
