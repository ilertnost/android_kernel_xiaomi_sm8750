# Changelog

## v1.1

Base: GKI `6.6.139`, `branch common-android15-6.6`. Device verified: Xiaomi 15
(`dada`, SM8750), Android 16, stock vendor modules.

`uname -r` reports `6.6.139-4k-APEX-Foundry-v1.1`.

### SukiSU Ultra moves from `builtin` to `main`

The root implementation now comes from the `main` branch, which reports KSU UAPI
**5** where `builtin` is stuck at **2**. That was the point: users reported `2`
as too old. Verified on device, the manager now shows `40959-5`.

`main` does not build SUSFS in, so it comes from
`apex-patches/sukisu-ultra/main/`. That set is upstream `susfs4ksu`
`10_enable_susfs_for_ksu.patch` plus five corrections, each documented with the
reason in `apex-patches/sukisu-ultra/main/README.md`:

1. `kernel/arch.h` from `builtin` shadowed `main`'s `kernel/include/arch.h`.
2. `selinux_hide.c` tested two functions as if they were function pointers.
3. The patch drops `infra/symbol_resolver.o` while keeping two users of it.
4. Three `core/init.c` hunks do not apply; the hooks were wired by hand.
5. The version pin lives in `kernel/Kbuild` on `main`, not `kernel/Makefile`.

Both patches apply to a clean `origin/main` with no failures, and the result was
compared file by file against the tree that produced the tested image: 28 files,
no differences.

### BORE

Burst-Oriented Response Enhancer. Keeps a task's cache and TLB warm across short
sleeps instead of letting it migrate.

**No measurable effect on Geekbench 7.** Measured on the same device, median of
three runs, stock 1578/7195 against v1.0's 1750/7900, roughly +10%. Adding BORE
moved that to 1730/7792, within run-to-run noise. Geekbench is a
single-threaded throughput benchmark and BORE only acts when tasks contend, so
this is the measurement's blind spot rather than proof of absence. It targets
container and translated workloads, Droidspaces and Winlator, where the wakeup
pattern is what the feature is for.

It is runtime-switchable at `/proc/sys/kernel/sched_bore`, so keeping it costs
nothing if it turns out not to help. Set to `0` to disable without reflashing.

### memfd ashmem shim

Services ashmem-style ioctls on `memfd` files. This is the full version: the
shim itself, plus `CONFIG_TLS=m` and `net/tls/tls.ko` in `modules.bzl`.

### Build ISA

`-march=armv8-a+dotprod+fp16+i8mm`. Previously the kernel carried **no**
`-march` at all, so clang defaulted every translation unit to `armv8-a`.

The baseline deliberately stays at `armv8-a`. ARMv9.2 mandates SVE2 and clang
treats that as already enabled rather than something to opt out of: given
`-march=armv9.2-a` it vectorises an ordinary float loop into `ptrue`, `addvl`,
`ld1w { z12.s }` and `fadda`. Kernel code must never emit those, it has no
mandate to save the z registers. `-mno-sve` and `-mno-sve2` are rejected by this
clang, so keeping the baseline low is the mechanism rather than a compromise.

Verified two ways: `-dM -E` shows exactly `DOTPROD`, `FP16_SCALAR_ARITHMETIC`,
`FP16_VECTOR_ARITHMETIC` and `MATMUL_INT8` enabled with SVE, SVE2, BF16, SHA3,
RCPC3 and LSE off; and the finished `vmlinux` disassembles to zero SVE
instructions under `llvm-objdump --mattr=-sve,-sve2`.

Expect little from this. These extensions are barely represented in kernel hot
paths. The `Image` is **38 382 080 B**, unchanged to the byte. The binary
differs, so the rebuild is real, but there is no size and no code movement to
show for it.

### Version string is now computed, not hardcoded

Through v1.0 the version came from a hardcoded `echo` on the last line of
`scripts/setlocalversion`, replacing the upstream one. It reported
`6.6.139-android15-8-maybe-dirty-4k-APEX-Foundry-v1.0-Tempered` regardless of
what was built, and it spelled out `maybe-dirty` unconditionally. On a clean tree
that was a false claim.

`scripts/setlocalversion` is stock again and the version comes from
`CONFIG_LOCALVERSION` in `gki_defconfig`. Output is now:

```
6.6.139-android15-8-4k-APEX-Foundry-v1.1
```

No commit hash and no `-dirty` suffix, and reproducible.

The `android15-8` component survives, and it is not cosmetic. kleaf writes a
`localversion` file into the source tree containing `-${android_release}-${KMI_GENERATION}`,
derived from the workspace `BRANCH` and AOSP's GKI KMI generation
(`build/kernel/kleaf/impl/stamp.bzl`, `_write_localversion`). `-4k` is
`CONFIG_LOCALVERSION`, which is where `-APEX-Foundry-v1.1` was added.

That same file is where `maybe-dirty` comes from, and this is worth being precise
about: without `--config=stamp`, kleaf writes `-maybe-dirty` in place of the
`STABLE_SCMVERSIONS` lookup. So the suffix was not invented by the hardcoded
`echo`, it is kleaf's fallback path. The hardcoded string simply asserted it
always, even when the tree was clean and the build did pass `--config=stamp`.

### Module KMI: stop refusing ten stock modules

Every boot refused ten stock modules, and modprobe reported it as
"Permission denied", which reads like a file permission or signature problem.
The kernel said otherwise:

```
rtl8150: Protected symbol: usb_check_int_endpoints (err -13)
```

`kernel/module/main.c` lets a module that is not signed with the GKI key import
symbols only from other unsigned modules or from the generated unprotected list.
Anything else is `-EACCES`. Affected: `bluetooth`, `hci_uart`, `btbcm`,
`btqca`, `btsdio`, `hidp`, `rfcomm`, `rtl8150`, `9pnet_fd`.

Not caused by anything done here. The KMI lists arrived with the AOSP 6.6.139
base commit `00c3c12bc` and no commit since has touched them. The stock modules
are built for 6.6.118 and the 6.6.139 KMI list no longer carries every symbol
they import. Any clean AOSP 6.6.139 build on this device logs the same ten
failures.

`CONFIG_APEX_RELAX_GKI_MODULE_KMI=y` lifts the restriction on import. It is a
Kconfig switch, not an edit to the check, so it is visible in `.config` and
reverting is one line in `gki_defconfig`. The export side
(`gki_is_module_protected_export`) is deliberately untouched: an unsigned module
still cannot re-export a symbol owned by a protected GKI module.

Two things this does not do. `ptp_kvm` still fails with "Operation not supported
on transport endpoint" and will keep failing; there is no hypervisor on the
phone, that one is correct behaviour. And Bluetooth was the risk worth watching,
since it worked without these modules and a second stack landing on the vendor
driver could have broken it.

Verified on device, Xiaomi 15 / dada, HyperOS 3.0.307.0.WOCCNXM:

| | v1.0 | v1.1 |
|---|---|---|
| `Protected symbol` refusals | 9 modules | 0 |
| `Failed to load module` | 10 | 1 |
| `/proc/modules` entries | 593 | 602 |

All nine now load: `bluetooth`, `hci_uart`, `hidp`, `rfcomm`, `btbcm`,
`btqca`, `btsdio`, `rtl8150`, `9pnet_fd`. The entry count is exactly +9, and
`rtl8150` shows up in `Modules linked in`.

Bluetooth did not break. `state: ON`, same address, no conflict. So the switch
removes a permanent tail of boot log failures and costs nothing that shows up in
use.

The one `WARNING` left in the boot log is `Unbalanced IRQ 420 wake disable` from
the Qualcomm `cnss2` WiFi driver, after `invalid GPIO -22`. Not ours: v1.0 shows
the same counts, one and five respectively. WiFi, cellular, Bluetooth and root
all confirmed working after flashing.

Cost of the switch: the `Image` is 589 824 B smaller than v1.0. That is
`gki_unprotected_symbols` being garbage collected once nothing references it,
confirmed with `llvm-nm`: the array is absent from vmlinux while
`gki_protected_exports_symbols`, still used by the export side, remains.

This does not disturb module loading. `kasan_flag_enabled` and `__kfence_pool`
each lose one `strings` hit from the Image because the dropped table held the
second copy of their names. Both remain defined and exported, verified against
vmlinux rather than `strings`:

```
ffffffffc0824539c0 B kasan_flag_enabled
ffffffffc0816febf8 r __crc_kasan_flag_enabled
ffffffffc0816e70ac r __ksymtab_kasan_flag_enabled
ffffffffc081719702 r __kstrtab_kasan_flag_enabled
```

That matters because 22 stock vendor modules import `kasan_flag_enabled`. Count
exported symbols with `llvm-nm`, not with `strings` on the Image.

### KASAN and KFENCE

Not a change in v1.1, but worth stating because an attempt to disable them
during this cycle caused an early bootloop and the reason was not KASAN.

Disabling KASAN removed `kasan_flag_enabled` from the build. Removing it from
the KMI export lists broke **22** stock vendor modules in `system_dlkm` and
`vendor_boot` that import it, including `zram.ko`, `tls.ko`, `cfg80211.ko`,
`zsmalloc.ko`, `r8152.ko`, `virtio_blk.ko`, `mem_buf.ko` and `bootmonitor.ko`.

KASAN and KFENCE stay compiled in. Runtime behaviour is unchanged, the stock
command line passes `kasan=off`, and the symbols stay exported so the stock
modules keep resolving.

### Considered and rejected

| Idea | Why not |
|---|---|
| `-march=armv9.2-a` | Pulls in SVE, see above. |
| `-fstrict-aliasing` | `Makefile:583` sets `-fno-strict-aliasing` deliberately. `KCFLAGS` is appended later, so it would override that protection. |
| `-O3` | `CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE` already gives `-O2`. `-O3` grows the text and mobile I-cache is small enough to measure negative. |
| ZRAM lz4/zstd | Measured about 1%, inconsistent between runs. |
| Fengchi / HMBIRD | Needs an OPPO DT node, `/soc/oplus,hmbird/version_type`, absent on Xiaomi. 626 KB across 73 files, and drops `sched_ext`. |
| PAN emulation | Oryon has hardware PAN and `system_uses_ttbr0_pan()` requires `!system_uses_hw_pan()`, so there is nothing to gain. |

---

## v1.0 (Tempered)

First flavour cleared for daily use. SukiSU Ultra `builtin` with SUSFS, ADIOS,
NTsync, 24 optimization patches, the Droidspaces fixes, and KMI symbol list
changes that let stock vendor modules load unmodified.

Benchmarked against stock on the same device, median of three runs each, governor
`walt`:

| | Single | Multi |
|---|---|---|
| Stock | 1578 | 7195 |
| v1.0 | 1750 | 7900 |
| Delta | +10.9% | +9.8% |

The gain comes from the base, the patch set and ThinLTO, not from any single
patch. One run produced 1200/6700 and was discarded as an outlier.

Known cosmetic issue: the manager reports a version mismatch against the kernel.
The interface is compatible and root works. Fixed on the kernel side in v1.1,
see above.