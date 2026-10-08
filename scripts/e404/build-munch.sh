#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Public source-build entry point. Does not package, flash or contact Telegram.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ ${1:-} == --help ]]; then
    cat <<'HELP'
Usage: scripts/e404/build-munch.sh [--prepare-only]
Environment: OUT (new output dir), JOBS (default 2), CC (default clang),
CONFIG (default archived K112 config), LOCALVERSION (default -recoveryfix1),
KERNEL_BUILD_LOCK (default shared cryomgr-build-budget.lock).
Provide LLVM tools and aarch64-linux-gnu-/arm-linux-gnueabi- tools on PATH.
HELP
    exit 0
fi
[[ $# == 0 || ( $# == 1 && $1 == --prepare-only ) ]] || { echo 'Unknown arguments' >&2; exit 2; }
OUT=${OUT:-"$ROOT/out-public"}
CONFIG=${CONFIG:-"$ROOT/Documentation/e404/configs/k112-recoveryfix1.config"}
JOBS=${JOBS:-2}
[[ $JOBS =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive' >&2; exit 2; }
[[ -f "$ROOT/KernelSU/kernel/feature/selinux_query.c" ]] || { echo 'Get the pinned KernelSU submodule or complete-source archive first.' >&2; exit 1; }
[[ -f "$CONFIG" ]] || { echo "Missing config: $CONFIG" >&2; exit 1; }
LOCK=${KERNEL_BUILD_LOCK:-"${XDG_RUNTIME_DIR:-/tmp}/cryomgr-build-budget.lock"}
exec 9>"$LOCK"
flock -n 9 || { echo 'Compiler lock occupied; no build launched.' >&2; exit 75; }
mkdir -p -- "$OUT"
OUT=$(cd -- "$OUT" && pwd)
[[ ! -e "$OUT/.config" ]] || { echo 'Output already has a config; use a new OUT to preserve it.' >&2; exit 1; }
cp -- "$CONFIG" "$OUT/.config"
export ARCH=arm64 SUBARCH=arm64 KSU_VERSION=32473
export KBUILD_BUILD_USER=${KBUILD_BUILD_USER:-builder}
export KBUILD_BUILD_HOST=${KBUILD_BUILD_HOST:-munch}
args=(-C "$ROOT" O="$OUT" LLVM=1 LLVM_IAS=1 "CC=${CC:-clang}"
      CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_COMPAT=arm-linux-gnueabi-
      "LOCALVERSION=${LOCALVERSION:--recoveryfix1}")
make "${args[@]}" olddefconfig
[[ ${1:-} == --prepare-only ]] && exit 0
make "${args[@]}" -j"$JOBS" Image
sha256sum "$OUT/arch/arm64/boot/Image" "$OUT/.config"
cat "$OUT/include/config/kernel.release"
