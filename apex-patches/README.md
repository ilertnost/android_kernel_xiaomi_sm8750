# Patch provenance

These are the exact patch sources applied on the `v0.2-exp` branch. They are
included so every source change in the tree can be traced back to an
upstream patch instead of being taken on trust.

## optimizations/

23 patches from `wildkernels_patches/common/`. They are not maintained here
and carry their original authors in the `Signed-off-by:` trailers.

`clear_page_16bytes_align.patch` is deliberately **not** included. It edits
`arch/arm64/lib/clear_page.S`, whose layout changed between 6.6 and 6.7, so
its only hunk does not apply to this 6.6.139 tree. The optimization it
provides is minor and was dropped rather than forced.

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
