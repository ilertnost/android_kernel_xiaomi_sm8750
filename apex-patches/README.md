# Patch provenance

These are the exact patch sources applied on the `v0.2-exp` branch. They are
included so every source change in the tree can be traced back to an
upstream patch instead of being taken on trust.

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

## KernelSU-Next and SUSFS

Deliberately absent. Both were attempted and reverted; the working tree in
this branch contains neither. Verified against the built `Image`:

```
kernelsu  0   KernelSU  0   susfs  0   SUSFS  0   ksu_  0
```

KernelSU-Next's own `kernel/setup.sh` installs the tree but expects a **tag**
or commit, not a branch name. Passing a branch pulls a moving tip, which is
what made the third-party SUSFS fix patches fail to apply. See the commit
history of `apex-foundry-v0.1-prot` for that investigation.
