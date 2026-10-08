#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Source-only adaptation of the ROM-specific recoveryfix1 repacking step.
set -euo pipefail
if [[ ${1:-} == --help || $# != 3 ]]; then
    echo 'Usage: repack-boot.sh INPUT_BOOT_IMAGE RAW_KERNEL_IMAGE NEW_OUTPUT_DIRECTORY'
    echo 'Needs AOSP unpack_bootimg and mkbootimg. Builds an unsigned candidate only.'
    [[ ${1:-} == --help ]] && exit 0
    exit 2
fi
python3 - "$@" <<'PY'
import hashlib, json, pathlib, shlex, subprocess, sys
boot,kernel,out=map(lambda x:pathlib.Path(x).resolve(),sys.argv[1:])
assert boot.is_file() and kernel.is_file(), 'Missing input'
with kernel.open('rb') as f:
    f.seek(56)
    assert f.read(4)==b'ARM\x64', 'Expected raw ARM64 Image'
out.mkdir(exist_ok=False)
args=shlex.split(subprocess.check_output(['unpack_bootimg','--boot_img',str(boot),'--out',str(out/'unpacked'),'--format=mkbootimg'],text=True))
assert '--kernel' in args, 'No kernel field in boot image'
args[args.index('--kernel')+1]=str(kernel)
subprocess.run(['mkbootimg',*args,'-o',str(out/'boot-candidate.img')],check=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
(out/'SHA256.json').write_text(json.dumps({'input_boot':sha(boot),'kernel':sha(kernel),'output':sha(out/'boot-candidate.img'),'status':'unsigned, unflashed, unqualified'},indent=2)+'\n')
print(out/'boot-candidate.img')
PY
