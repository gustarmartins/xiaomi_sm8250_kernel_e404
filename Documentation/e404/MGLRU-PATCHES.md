# MGLRU on munch

Repair documentation for MGLRU backport in this tree (written with the assistance of GPT Astra). Can be used to adapt MGLRU properly for munch devices and other related-kernel trees...
The fork also carries scheduler, charging, ZRAM and KernelSU work, most targetting my "CryoManager" project and private preferences; therefore MGLRU and related memory-management additions were cherry-picked in this document.

## Where to start

The final accounting fixes are in [5078e7f1ed46](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/5078e7f1ed46). That commit preserves a previously built dirty source snapshot and also contains unrelated NFLOG/boot changes. For a narrower review, use these path-filtered patches, retaining the original author and provenance:

- [MGLRU accounting and bounded diagnostic history](patches/k108-mglru-accounting.patch): `mm/vmscan.c`, `include/linux/mm_inline.h`, `mm/mglru_history.h`.
- [UFFD charge before LRU insertion and PTE publication](patches/k106-uffd-charge-order.patch): `mm/userfaultfd.c`, `mm/shmem.c`, `include/linux/userfaultfd_k.h`.

Both are extracted relative to `9f2791203eeb`, which already includes the preceding repair series. The accounting patch still groups several historical repairs and diagnostics; split it further for a formal upstream submission.

### The userfaultfd bug

`mcopy_atomic_pte()` used to commit the anonymous memcg charge after `mfill_atomic_install_pte()` inserted the page into the LRU and published its PTE. A full pagevec could drain before the charge, crediting the root LRU while a later operation debited the app's memcg. The repaired helper commits the pending charge after rmap setup, before LRU insertion and PTE publication. Failed installation still cancels the pending charge; shared/cache callers do not commit twice.

The host regression exercises all 15 pagevec occupancies, PTE collision, failed copy/charge, and cache callers. It also reproduces the original broken ordering. VM fixtures do not prove real kernel concurrency correctness.

## Earlier repair series

Read these in historical order and check dependencies against your target tree. This is a navigation index, not a promise that every commit can be cherry-picked alone.

| Commit | Area |
|---|---|
| [396c9f5c4f43](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/396c9f5c4f43) | Empty direct-reclaim loops |
| [1c476c19b1ba](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/1c476c19b1ba) | Bounded sort progress |
| [a27c788ad5d0](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/a27c788ad5d0) | Address-space lifetime through aging |
| [22ef90fce9f1](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/22ef90fce9f1) | Generation integrity |
| [0ebee38c6664](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/0ebee38c6664) | Aging and reclaim progress |
| [6a38ba71ce81](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/6a38ba71ce81) | Bounded ARM64 page-table walks |
| [6a95c821939b](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/6a95c821939b) | PMD-walk bounds and PFN validation |
| [b4ffd44be4c0](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/b4ffd44be4c0) | Secondary-MMU notifications when clearing young PTEs |
| [3aa7392b048f](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/3aa7392b048f) | Avoid pinning dying address spaces |
| [fa4ac1579bc8](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/fa4ac1579bc8) | Preserve page type through generation CAS |
| [9f65aaf7e169](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/9f65aaf7e169) | Finish direct-reclaim aging batches |
| [0cf5a0391d64](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/0cf5a0391d64) | MGLRU generations in 4.19 memcg readers |
| [da16f3324d27](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/da16f3324d27) | Compaction LRU ownership and deactivation |
| [f01671cab612](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/f01671cab612) | Respect another isolator's existing LRU claim |

The TTL behavior is also fork-specific: see [81970276fde8](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/81970276fde8) and [6f672ae55bf3](https://github.com/gustarmartins/xiaomi_sm8250_kernel_e404/commit/6f672ae55bf3). `min_ttl_ms` is not a per-app protection guarantee. Source availability does not establish that a particular TTL is appropriate for every device/workload.

## Run the focused host checks

Needs Python 3 and Clang with ASan/UBSan, from the repository root:

```sh
python3 tools/testing/selftests/e404/mglru_accounting.py
python3 tools/testing/selftests/e404/mglru_history.py
python3 tools/testing/selftests/e404/uffd_charge_order.py
python3 tools/testing/selftests/vm/mglru_memcg_readers.py
python3 tools/testing/selftests/vm/mglru_isolation_claim.py
python3 tools/testing/selftests/vm/mglru_deactivate.py
```

All six passed on the October 8 publication snapshot. They cover deterministic source-level cases. Boot, sustained camera/audio pressure, teardown/migration races, power and another maintainer's ROM/device need their own qualification.

Preserve original attribution when reusing commits. For a formal submission, describe each defect, prerequisites and evidence, follow the recipient's rules, and have the human contributor verify any Developer Certificate of Origin sign-off. See the [Linux patch submission guide](https://docs.kernel.org/process/submitting-patches.html).
