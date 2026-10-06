# APEX-Foundry

Custom GKI kernel for the **Xiaomi 15** (codename `dada`, SM8750 / Snapdragon 8 Elite).

Builds against Google's Android Generic Kernel Image **6.6.139** (branch `android15-6.6`)
and runs the device's **stock vendor modules unmodified**.

Flavour: `v1.0 (Tempered)` — SukiSU Ultra and SUSFS built in

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
- ~30 GB free disk
- ~16 GB RAM

```bash
sudo pacman -S --needed git bc bison flex python ccache
mkdir -p ~/bin
curl -L https://storage.googleapis.com/git-repo-downloads/repo -o ~/bin/repo
chmod +x ~/bin/repo
export PATH="$HOME/bin:$PATH"
```

### Disk, measured

These are the actual figures from the reference build, not estimates:

| | |
|---|---|
| `common/` source tree | 1.6 GB |
| `prebuilts/` hermetic toolchain | 6.6 GB |
| `out/` bazel output tree | 6.7 GB |
| `dist/` packaged images | 0.7 GB |
| **workspace total** | **~18 GB** |
| `~/.cache/bazel` disk cache | up to ~10 GB |
| **peak on disk** | **~28 GB** |

The source tree alone is 1.6 GB. If disk is tight, the bazel disk cache is
the part to drop or cap; the build still works, it just recompiles more.

### RAM

The reference build completed on a 16-core, 32 GB machine with
`CONFIG_LTO_CLANG_THIN`. The LTO link is the peak consumer; everything else
fits comfortably.

16 GB is a practical floor for the ThinLTO configuration. Building with
`--lto=none` needs considerably less, at the cost of the cross-module
inlining that ThinLTO provides.

Capping parallelism lowers the peak: `--jobs=N` on the bazel command line
reduces how many compilation and link jobs run at once.

> The RAM figure is a recommendation derived from the reference machine, not
> an instrumented peak measurement. The disk figures above are measured.

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
# 6.6.139-android15-8-maybe-dirty-4k-APEX-Foundry-v1.0-Tempered
```

`UTS_RELEASE` is capped at 64 characters by the kernel build. `v1.0-Tempered`
fits at 61; the longer `v0.2-Experimental` did not, which is why that build
shipped abbreviated as `v0.2-Exp`.

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
install and no boot image patch — `uname -r` reports `APEX-Foundry-v1.0-Tempered`
and the manager talks to it directly.

SukiSU's `builtin` branch carries its own ten `CONFIG_KSU_SUSFS_*` symbols, so
no external patch is required. That is why it is used instead of KernelSU-Next,
which keeps SUSFS out of tree and needs third-party patches tied to one exact
upstream commit.

Pinned to `70fa0e092a2c81060823f8ae526eac14fdda2930`. The branch carries no
release tag — every tag lives on `main` and none is an ancestor of `builtin`, so
a tag would drop the GKI integration. The commit is pinned instead.

Three upstream defects had to be fixed to make that commit compile; they are
documented with error messages in `apex-patches/sukisu-ultra/FIXES.md`.

`UTS_RELEASE` is capped at 64 characters, so the reported tag is the full
`v1.0-Tempered` while the management app shows `40959`, derived by SukiSU from
its own commit count.

## Performance and Droidspaces

**Performance — 24 patches.** Memory and scheduler hot paths, `memcmp` and
`int_sqrt`, cache pressure, F2FS congestion, ext4 commit age, wakeup
behaviour, and two patches that quiet kernel log spam.

All 24 candidate patches are now applied. An earlier revision of this README
recorded `clear_page_16bytes_align` as inapplicable to 6.6.139; that was wrong.
The patch applies cleanly to `arch/arm64/lib/clear_page.S` and aligns
`__pi_clear_page` to 16 bytes, which measurably reduces time spent zeroing
pages under `CONFIG_MEMORY_INIT` style allocation.

**ThinLTO.** Enabled via `CONFIG_LTO_CLANG_THIN`. The stock kernel ships with
`+lto`; this tree previously built with `CONFIG_LTO_NONE`. Matching stock is
worth roughly 3% of image size for improved cross-module inlining.

**NTsync.** `CONFIG_NTSYNC`, the CodeWeavers driver that emulates Windows NT
synchronization primitives. Required by Wine and Proton for correct
semantics; a no-op for native Android applications.

**ADIOS I/O scheduler.** Built in, not a module, so it is active from boot
with no deployment step. `CONFIG_MQ_IOSCHED_ADIOS` and
`CONFIG_MQ_IOSCHED_DEFAULT_ADIOS`. It predicts device class per I/O and
adapts rather than applying one static policy, which suits flash storage.
Driver by Masahito Suzuki, GPL-2.0, taken from palazik's source tree.

**Unicode bypass fix.** A restructure of the `DECOMPOSE` branch in
`utf8byte()` that checks for empty decomposition before moving the cursor
pointers, so zero-width and similar characters no longer corrupt cursor
state in UTF-8 normalisation. Affects the F2FS casefolding path.

**Not applied: `-mcpu=oryon-1`.** It is a Qualcomm clang extension and is
rejected by the hermetic AOSP toolchain:

```
clang: error: unsupported argument 'oryon-1' to option '-mcpu='
```

Adopting it would mean switching to a Qualcomm-flavoured clang such as
ZyCromerZ Clang 19 and revalidating the entire build. Left out deliberately.

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

## Known cosmetic issue

The management app reports a version mismatch against the kernel. The app ships
from SukiSU's `module_repository` and checks against its own version, while this
kernel pins a specific commit rather than tracking the branch tip. The interface
is compatible — root, SUSFS and the manager all function — so only the version
number disagrees. It resolves when the app is updated upstream.

## Branch naming

The branch is named after the Android platform the kernel targets, using the
LineageOS convention, not after the GKI branch it is developed on. The two are
different things:

- GKI source branch: `common-android15-6.6`. This records that Google developed
  the 6.6 kernel train during the Android 15 timeframe. It says nothing about
  which Android the kernel runs on, and `uname -r` will keep reporting
  `android15-8` regardless.
- Target platform: Android 16, hence `lineage-23.2`. The patches taken from
  the community build workflows are written against 23.0-23.2, and Android 16
  is what has been verified on device.

Kernel version string stays `APEX-Foundry-v1.0-Tempered`; the two numbering
schemes are independent and never mixed.

## Layout

| Branch | Contents |
|---|---|
| `lineage-23.2` | Current. KMI fix, optimizations, Droidspaces, SukiSU Ultra, SUSFS. |
| `apex-foundry-v0.1-prot` | The working base, before optional features. |
| `android15-6.6-dada` | Full upstream GKI history, kept for reference. |

## Status

Verified on device: audio, Bluetooth, WLAN, modem, haptics, charging and
Droidspaces all work, and SukiSU Ultra with SUSFS is stable.

Gaming results reported on HyperOS 3.0.307.0.WOCCNXM, same settings in each
case, before and after this kernel:

| Game | Before | After |
|---|---|---|
| Zenless Zone Zero | 40-50 fps | stable 60 fps |
| Arknights Endfield | 60-90 fps, dropping to 40 | 90-120 fps |

Frame generation is the game's own official option, used here because the
games cap at 60 fps without it. Comparison is by feel, not by an external
capture tool, so treat these as indicative rather than lab figures.

ThinLTO, ADIOS, NTsync, the 24th optimization patch and the Unicode fix were
added in the revision that introduced this table and are built and packaged
but not yet soak-tested on device.

## Acknowledgements

- Google, for Android GKI (`kernel/common`, branch `android15-6.6`)
- AnyKernel3 by osm0sis
- **[palazik](https://github.com/palazik)**, for the optimization patch set
  carried in `apex-patches/optimizations/` and `apex-patches/droidspaces/`.
  These 24 patches originate from the WildKernels project and are maintained
  and curated by palazik; this tree applies them verbatim, with no local
  modification. NTsync and the `clear_page()` alignment come from
  [palazik/kernel_patches](https://github.com/palazik/kernel_patches) as well
  (`ntsync/`, `optimizations/clear_page_16bytes_align.patch`).
- The optimization and Droidspaces patch authors, whose patches are recorded
  verbatim in `apex-patches/`
- SukiSU Ultra and SUSFS by their respective authors
- NTsync by Elizabeth Figura (CodeWeavers), GPL-2.0
- Sultan Alsawaf for the `clear_page()` alignment

`clear_page_16bytes_align.patch` is a standalone upstream-quality patch and
should be proposed to mainline independently of this kernel.

## Licence

Kernel sources remain GPL-2.0 as per the upstream tree. The AnyKernel3
installer in the flashable zip is GPL-3.0, see `LICENSE` in that package.