# Patch provenance

These are the exact patch sources applied in this branch. They are included so
every source change in the tree can be traced back to an upstream patch instead
of being taken on trust.

## optimizations/

24 patches from `wildkernels_patches/common/`, maintained and curated by
[palazik](https://github.com/palazik). They are not maintained here and carry
their original authors in the `Signed-off-by:` trailers.

An earlier revision of this file recorded `clear_page_16bytes_align.patch` as
excluded because `arch/arm64/lib/clear_page.S` was believed to differ between
6.6 and 6.7. That was incorrect: the patch applies cleanly to this 6.6.139
tree with no offset or fuzz, inserting `.p2align 4` ahead of
`SYM_FUNC_START(__pi_clear_page)`. All 24 are now applied.

## ntsync/

`ntsync_base.patch` and `ntsync_compat_android15-6.6.patch`, from
[palazik/kernel_patches](https://github.com/palazik/kernel_patches).
NTsync emulates Windows NT synchronization primitives and is required by Wine
and Proton. Driver by Elizabeth Figura (CodeWeavers), GPL-2.0. Enabled with
`CONFIG_NTSYNC=y`.

## droidspaces/

Two compatibility fixes from `wildkernels_patches/common/droidspaces/`:

- `fix_sysvipc_kabi_6_7_8.patch`
- `0001-Return-ghost-task-if-task-is-null-and-is-requested-b.patch`

The matching `CONFIG_` options were added to `arch/arm64/configs/gki_defconfig`
in the same commit that applies the source changes.

## adios/

The ADIOS I/O scheduler, two patches. Predicts a device class per I/O rather
than applying one static policy. Enabled with `CONFIG_MQ_IOSCHED_ADIOS=y`.

## unicode/

Empty UTF-8 decomposition fix. Without it, cursor state is corrupted in F2FS
normalization.

## sukisu-ultra/

Root and stealth. `50_add_susfs_in_gki-android15-6.6.patch` is the kernel-side
SUSFS integration for the `builtin` branch, byte for byte what
[simonpunk/susfs4ksu](https://github.com/simonpunk/susfs4ksu) ships for
`gki-android15-6.6`. `sukisu_apex_fixes.patch` carries source fixes and a
version pin.

### sukisu-ultra/main/

For building against SukiSU Ultra's `main` branch, which is what v1.1 uses.
`main` reports KSU UAPI 5 where `builtin` is stuck at 2, and 2 was what people
reported as too old. It does not build SUSFS in, so SUSFS comes from here
instead.

`10_enable_susfs_for_ksu-main.patch` is upstream `susfs4ksu` plus five
corrections, each explained in that directory's README. Both patches apply to a
clean `origin/main` with no failures, and the result was compared file by file
against the tree that produced the tested image: 28 files, no differences.

## isa/

`0001_target_oryon_isa.patch` sets `-march=armv8-a+dotprod+fp16+i8mm` in
`build.config.gki`. The kernel previously carried no `-march` at all.

The baseline stays `armv8-a` on purpose. ARMv9.2 mandates SVE2 and clang treats
that as already enabled, so `-march=armv9.2-a` makes it vectorise ordinary
loops into `ptrue`/`addvl`/`ld1w { z12.s }`, which kernel code must never emit.
`-mno-sve` and `-mno-sve2` are rejected by this clang. Verified with `-dM -E`
that only `DOTPROD`, `FP16_SCALAR_ARITHMETIC`, `FP16_VECTOR_ARITHMETIC` and
`MATMUL_INT8` come on.

## kmi/

`0002_relax_gki_module_kmi.patch` adds `CONFIG_APEX_RELAX_GKI_MODULE_KMI`,
which lets unsigned modules import any exported symbol instead of only those on
the generated unprotected list. It is a Kconfig switch, visible in `.config`,
and reverting is one line in `gki_defconfig`.

Ten stock modules were refused at every boot: `bluetooth`, `hci_uart`,
`btbcm`, `btqca`, `btsdio`, `hidp`, `rfcomm`, `rtl8150`, `9pnet_fd` and
`ptp_kvm`. The first nine now load. `ptp_kvm` cannot work on a phone without a
hypervisor and still fails, which is correct.

Not caused by anything in this repository. The KMI lists arrived with the AOSP
6.6.139 base and no commit since has touched them. The stock modules are built
for 6.6.118 and the 6.6.139 list no longer carries every symbol they import.

## versioning/

`0003_derive_version_from_kconfig.patch` restores the upstream
`scripts/setlocalversion` and moves the version to `CONFIG_LOCALVERSION`.

The last line of `setlocalversion` had been replaced with a hardcoded `echo`,
so the reported version bore no relation to what was built, and it spelled out
`maybe-dirty` unconditionally. On a clean tree that was a false claim.

Worth being precise about where `maybe-dirty` came from: without
`--config=stamp`, kleaf's `_write_localversion` in `stamp.bzl` writes exactly
that into a `localversion` file. Our builds do pass `--config=stamp`. The
hardcoded `echo` asserted it regardless of both.

## combined/

`apex-foundry-all.patch` is the whole change set as one diff against a clean
AOSP `android15-6.6` 6.6.139 tree. 188 files. Verify it with:

```bash
git apply --check apex-patches/combined/apex-foundry-all.patch
```

It predates the patches above. Regenerate it if you need it current.