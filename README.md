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
git -C common remote add origin https://github.com/ilertnost/android_kernel_google_sm8750.git
git -C common fetch origin v0.2-exp
git -C common checkout v0.2-exp

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

## Known cosmetic issue

The management app reports a version mismatch against the kernel. The app ships
from SukiSU's `module_repository` and checks against its own version, while this
kernel pins a specific commit rather than tracking the branch tip. The interface
is compatible — root, SUSFS and the manager all function — so only the version
number disagrees. It resolves when the app is updated upstream.

## Layout

| Branch | Contents |
|---|---|
| `v0.2-exp` | Current. KMI fix, optimizations, Droidspaces, SukiSU Ultra, SUSFS. |
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