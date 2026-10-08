# Distribution and attribution

Linux kernel distribution is governed by its licenses; there is no single mandatory Android custom-kernel ZIP format. The kernel is GPL-2.0-only with the syscall exception in `COPYING`; compatible per-file licenses and third-party notices remain applicable. See [Linux licensing rules](https://docs.kernel.org/process/license-rules.html) and the included [GPLv2 text](../../LICENSES/preferred/GPL-2.0), especially sections 1–3.

This publication provides the modified source, original history/attribution, license texts, a pinned public KernelSU dependency, full build configs, build/repacking helpers, checksums and a source tag. The complete-source archive includes KernelSU; GitHub's automatic archives do not include submodules. Existing historical branches contain intermediate and superseded work.

Distributing kernel binaries also requires providing the corresponding source under GPLv2's permitted terms, including the source and scripts needed to build/install the distributed work. A public repo containing a different or older revision is not a substitute. For Internet downloads, make the matching source available alongside the binary. Preserve notices, identify modifications, and do not impose additional restrictions inconsistent with the license. Any included installer/tool/firmware has its own licensing requirements.

The October 8 release is source-only. It does not newly distribute a boot image, vendor ramdisk, firmware, installer executable or purported universal AnyKernel package. Therefore this audit does not certify the source correspondence, licenses or compatibility of every older privately shared ZIP. Historical K108 source export had an untracked MGLRU header without an independent build-time hash; the preserved header is published, and that provenance limitation remains explicit.

Before a future binary release, record the exact source and submodule SHAs, final config, toolchain, build/packaging inputs and output hashes. Include the device/ROM compatibility statement, installation and rollback steps, and separate source/build/boot/runtime/workload evidence. Keep author and license notices from E404, Linux, Android/Qualcomm/Xiaomi, KernelSU and AnyKernel where applicable.

This is a technical publication audit, not a certification of every third-party copyright or every historical binary. Upstream acceptance is also separate: focused commits, clear prerequisites and testing evidence help the maintainer review the patches.
