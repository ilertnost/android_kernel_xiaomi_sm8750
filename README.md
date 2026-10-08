# APEX-Foundry

Custom GKI kernel for the **Xiaomi 15** (codename `dada`, SM8750 / Snapdragon 8 Elite).

Builds against Google's Android Generic Kernel Image **6.6.139** (branch `android15-6.6`)
and runs the device's **stock vendor modules unmodified**.

Flavour: `v1.1` — SukiSU Ultra main, SUSFS, BORE, memfd ashmem shim, ADIOS, NTsync

---

## What this is

A stock Android 6.6 GKI plus a small build-level change, and nothing else.
No vendor source code is vendored in, and no module is rebuilt or replaced.

The result boots with the modules already present on the device, from
`/vendor_dlkm` (397 modules) and `/system_dlkm` (192 modules), and audio,
Bluetooth, WLAN, haptics and mobile data all work.


### Design philosophy: MISR (Make It Simple, Reliable)

Every optimization, flag and commit goes through the same audit. If it does
not produce a measurable gain, or adds complexity the platform does not
justify, it is dropped. Daily-driver stability and clean code outrank
placebo commits and attractive changelogs.

This has cost us things we wanted. `CONFIG_LD_DEAD_CODE_DATA_ELIMINATION` is
present in the defconfig and is flagged `EXPERIMENTAL` upstream, with its own
help text warning it can produce a *silently broken kernel*. It stays only
because it has been running without incident; it is on the list to remove.

It also decided the root implementation, and the reason was the only kind
MISR accepts: the build did not work.

We started on KernelSU Next and never got it to build. Fixing one error
reliably produced two more, and each fix meant another layer of patching on
top of a kernel we were already patching. In the chat people asked us to drop
it for SukiSU Ultra, and the argument on offer was that SukiSU Ultra's
Material You interface looks better. That argument we rejected, because a
nicer interface is not a reason to change a working baseline. We moved to
SukiSU Ultra anyway, and not for that reason: KernelSU Next would not
compile.

Once there we pinned SukiSU Ultra to a fixed commit and stopped tracking its
branch.

## Why a change was needed

Android's GKI has a KMI protection mechanism. Any **unsigned** module that
exports a symbol listed in `android/abi_gki_protected_exports_aarch64` is
refused at load time with `-EACCES`, from `kernel/module/main.c`:

```c
if (!mod->sig_ok && gki_is_module_protected_export(kernel_symbol_name(s)))
        return -EACCES;
```

Xiaomi's vendor modules are unsigned. Thirteen of them were therefore
rejected on every boot, and one of those failures took most of the device
down with it:

```
rfkill.ko          rejected -> btpower, btqca, hci_uart, bluetooth.ko fail
                             -> cfg80211: Unknown symbol rfkill_alloc
                             -> WLAN never comes up
btfm_slim_codec    rejected -> btfmcodec_dev component never registers
                             -> msm_asoc_machine_probe defers forever
                             -> no sound card at all
```

`btpower`, `hci_uart` and `bluetooth` all need `rfkill_alloc`, so the whole
Bluetooth stack went down with it.

### The fix

Disable the check at build level rather than patching it out in C:

- `BUILD.bazel` — drop `protected_exports_list`
- `modules.bzl` — `protected_modules = []`

Vendor modules remain subject to `CONFIG_MODVERSIONS` CRC verification, so
version mismatches are still caught.

## Requirements

Tested on Arch Linux. You will need:

- Android `repo` tool
- ~120 GB free disk (manifest, hermetic prebuilts, bazel cache)
- ~30 GB RAM recommended for the first full build

```bash
sudo pacman -S --needed git bc bison flex python ccache
mkdir -p ~/bin
curl -L https://storage.googleapis.com/git-repo-downloads/repo -o ~/bin/repo
chmod +x ~/bin/repo
export PATH="$HOME/bin:$PATH"
```

## Building

```bash
mkdir -p ~/kernel_workspace && cd ~/kernel_workspace

repo init --depth=1 -u https://android.googlesource.com/kernel/manifest \
          -b common-android15-6.6-2026-07
repo sync -c --no-clone-bundle --no-tags --optimized-fetch --prune -j"$(nproc)"

# point 'common' at this repository
git -C common remote remove origin
git -C common remote add origin https://github.com/ilertnost/android_kernel_xiaomi_sm8750.git
git -C common fetch origin lineage-23.2
git -C common checkout lineage-23.2

mkdir -p dist ~/.cache/bazel

tools/bazel run \
  --lto=none \
  --config=stamp \
  --disk_cache="$HOME/.cache/bazel" \
  //common:kernel_aarch64_dist \
  -- --dist_dir="$PWD/dist"
```

The kernel lands at `dist/Image`. First build takes roughly 20–40 minutes;
later builds are incremental through the bazel disk cache.

### Verify

```bash
strings dist/Image | grep -m1 '^Linux version'
# 6.6.139-4k-APEX-Foundry-v1.1
```

The version string is produced the normal way, from `CONFIG_LOCALVERSION` in
`gki_defconfig`. Nothing overrides it and `scripts/setlocalversion` is stock.
Verified output:

```bash
strings dist/Image | grep -m1 '^Linux version'
# 6.6.139-android15-8-4k-APEX-Foundry-v1.1
```

Two kleaf-generated pieces go into that:

- `android15-8` is `-${android_release}-${KMI_GENERATION}`, written by kleaf into
  a `localversion` file in the source tree. `android_release` comes from the
  workspace `BRANCH`, `KMI_GENERATION` is AOSP's GKI KMI generation. See
  `build/kernel/kleaf/impl/stamp.bzl`, `_write_localversion`.
- `-4k` is `CONFIG_LOCALVERSION` from `gki_defconfig`, where `-APEX-Foundry-v1.1`
  was added.

`--config=stamp` also matters. Without it, kleaf writes `-maybe-dirty` into that
same `localversion` file instead of consulting `STABLE_SCMVERSIONS`. With it,
`LOCALVERSION=""` is exported, which is what makes `setlocalversion` skip git, so
the result carries no commit hash and is reproducible.

Earlier builds produced a different string by replacing the final `echo` in
`scripts/setlocalversion` with a hardcoded one. That made the reported version
unrelated to what was built, and it spelled out `maybe-dirty` unconditionally,
whether or not the tree was dirty. On a clean tree that was a false claim. Both
problems are gone.

`UTS_RELEASE` is capped at 64 characters, so keep `CONFIG_LOCALVERSION` well
under that. `v1.1` fits easily; the aborted `v0.2-Experimental` did not, which
is why that build shipped abbreviated as `v0.2-Exp`.

SukiSU Ultra is not vendored in this repository. Fetch it at its pinned commit:

```bash
curl -LSs "https://raw.githubusercontent.com/SukiSU-Ultra/SukiSU-Ultra/builtin/kernel/setup.sh" \
  | bash -s 70fa0e092a2c81060823f8ae526eac14fdda2930
```

Then apply `apex-patches/sukisu-ultra/sukisu_apex_fixes.patch` and restore
`apex-patches/sukisu-ultra/arch.h`. Full steps are in
`apex-patches/sukisu-ultra/README.md`.

## Flashing

The released zip is an AnyKernel3 package. Flash it from recovery, or with
KernelFlasher / any AK3-compatible flasher. It writes the `boot` partition
only, via `magiskboot`.

Verify after boot:

```bash
adb shell uname -r
adb shell 'dmesg | grep -c "protected symbol"'   # expect 0
adb shell cat /proc/asound/cards                 # expect sunmtpsndcard
adb shell ls /sys/class/bluetooth/               # expect hci0
adb shell 'ip -br link | grep wlan'               # expect wlan0
```

## Root and stealth

**SukiSU Ultra**, built into the kernel, plus **SUSFS v2.3.0**. No module to
install and no boot image patch — `uname -r` reports `APEX-Foundry-v1.1`
and the manager talks to it directly.

From v1.1 the root implementation comes from SukiSU Ultra's `main` branch, not
`builtin`. `main` carries KSU UAPI 5 where `builtin` is stuck at 2, and 2 was
what people reported as too old. SUSFS is not built in on `main`, so it comes
from the patch set in `apex-patches/sukisu-ultra/main/`, which is upstream
`susfs4ksu` plus five corrections documented in that directory's README.

SukiSU's `builtin` branch carries its own ten `CONFIG_KSU_SUSFS_*` symbols, so
no external patch was required there. That is why `builtin` was used through
v1.0. KernelSU-Next keeps SUSFS out of tree and needs third-party patches tied
to one exact upstream commit, and was never a candidate.

Pinned to `70fa0e092a2c81060823f8ae526eac14fdda2930`. The branch carries no
release tag — every tag lives on `main` and none is an ancestor of `builtin`, so
a tag would drop the GKI integration. The commit is pinned instead.

Three upstream defects had to be fixed to make that commit compile; they are
documented with error messages in `apex-patches/sukisu-ultra/FIXES.md`.

The reported tag is `v4.2.0-SukiSU-Ultra-42d7fda3@apexfoundry` while the
management app shows `40959-5`, derived by SukiSU from its own commit count.
The `5` is the KSU UAPI version and is the reason for the move to `main`; on
`builtin` it was `2`.

## Performance and Droidspaces

**Performance — 23 patches.** Memory and scheduler hot paths, `memcmp` and
`int_sqrt`, cache pressure, F2FS congestion, ext4 commit age, wakeup
behaviour, and two patches that quiet kernel log spam.

One of the 24 candidate patches, `clear_page_16bytes_align`, is **not**
included: it targets the 6.7-era `arch/arm64/lib/clear_page.S` and its single
hunk does not apply to 6.6.139. See `apex-patches/README.md`.

**Droidspaces.** The `sysvipc` KABI fix and the ghost-task NULL check, plus the
seven config options the container runtime needs (`CONFIG_SYSVIPC`,
`CONFIG_DEVTMPFS`, `CONFIG_PID_NS`, `CONFIG_POSIX_MQUEUE`, and three
`CONFIG_NETFILTER_XT_*`).

## Versioning

Three maturity stages, named for what changes between them:

| Flavour | Meaning |
|---|---|
| `v0.1 (Prototype)` | Working model, rough edges expected. |
| `v0.2 (Experimental)` | Features on trial, behaviour may change. |
| `v1.0 (Tempered)` | Hardened by testing, for daily use. |
| `v1.1` | Adds BORE, the memfd ashmem shim, SukiSU Ultra main. See `CHANGELOG.md`. |

## Known cosmetic issue

The management app still reports a version mismatch against the kernel. The app
ships from SukiSU's `module_repository` and compares against its own version,
while this kernel pins a specific commit rather than tracking the branch tip.
The interface is compatible — root, SUSFS and the manager all function — so only
the number disagrees. It resolves when the app is updated upstream.

v1.1 moved the reported UAPI from 2 to 5, which addresses the complaint that 2
was too old. The mismatch itself is a manager-side comparison and could not be
removed from the kernel.

## Branch naming

The branch is named after the Android platform the kernel targets, using the
LineageOS convention, not after the GKI branch it is developed on. The two are
different things:

- GKI source branch: `common-android15-6.6`. This records that Google developed
  the 6.6 kernel train during the Android 15 timeframe. It says nothing about
  which Android the kernel runs on. Its two parts do reach `uname -r` as
  `android15-8`, but as AOSP identifiers, not as a claim about the running
  system: `android15` is the workspace `BRANCH` and `8` is the GKI KMI
  generation.
- Target platform: Android 16, hence `lineage-23.2`. The patches taken from
  the community build workflows are written against 23.0-23.2, and Android 16
  is what has been verified on device.

Kernel version string stays `APEX-Foundry-v1.1`; the two numbering schemes are
independent and never mixed.

## Layout

| Branch | Contents |
|---|---|
| `lineage-23.2` | Current. KMI fix, optimizations, Droidspaces, SukiSU Ultra, SUSFS. |
| `apex-foundry-v0.1-prot` | The working base, before optional features. |
| `android15-6.6-dada` | Full upstream GKI history, kept for reference. |

## Status

Verified on device: audio, Bluetooth, WLAN, charging and Droidspaces all work,
and SukiSU Ultra with SUSFS is stable.

## Acknowledgements

- Google, for Android GKI (`kernel/common`, branch `android15-6.6`)
- AnyKernel3 by osm0sis
- The community GKI build workflows this approach follows
- The optimization and Droidspaces patch authors, whose patches are recorded
  verbatim in `apex-patches/`

## Licence

Kernel sources remain GPL-2.0 as per the upstream tree. The AnyKernel3
installer in the flashable zip is GPL-3.0, see `LICENSE` in that package.