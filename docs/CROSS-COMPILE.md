# Cross-compiling Minecraft PE 0.6.1 for iOS 3.x on Linux

This documents how to build the `minecraftpe` target for **armv6 / iOS 3.x**
on a plain Linux box (and in GitHub Actions), without a Mac and without Xcode —
reproducing what was previously done in **Xcode 4.1 against the iOS 4.3 SDK**.

## TL;DR

```bash
./build-ios.sh                 # Release, target "minecraftpe"
./build-ios.sh --config Debug  # other config
# → build/minecraftpe.ipa
```

In CI it runs automatically on every push via
[`.github/workflows/build-ios.yml`](../.github/workflows/build-ios.yml) and
uploads the `.ipa` as a build artifact.

## Why iOS 4.3 SDK?

The original Xcode 4.1 setup linked against the **iOS 4.3 SDK**, which is the
sweet spot for an armv6 / iOS 3.x target:

| SDK     | armv6 framework slices            | Notes                              |
|---------|-----------------------------------|------------------------------------|
| 4.3     | **Real armv6 slices** in UIKit, OpenGLES, QuartzCore, Foundation, CoreGraphics, OpenAL, AudioToolbox, AVFoundation | What we use. Symbols resolve fully at link time. |
| 5.1     | UIKit etc. are armv7-only **stubs** | Links, but UIKit symbols stay undefined until runtime. |

Because 4.3 ships genuine armv6 binaries, the linker validates **every** symbol,
and the resulting executable is a clean `NOUNDEFS` Mach-O armv6 binary.

## Pipeline

`build-ios.sh` does the whole thing end-to-end:

1. **Build `xcparse`** — a tiny zero-dependency C tool
   ([`tools/xcparse/`](../tools/xcparse)) that parses
   `minecraftpe.xcodeproj/project.pbxproj` and prints the exact source list,
   linked frameworks, and build settings. No hand-maintained file lists — the
   project file is the single source of truth.
2. **Fetch the iOS 4.3 SDK** via sparse checkout of
   [`EachAndOther/Legacy-iOS-SDKs`](https://github.com/EachAndOther/Legacy-iOS-SDKs).
3. **Build a Mach-O toolchain** (cached): `ld64` + `lipo` + `strip` from
   [`cctools-port`](https://github.com/Un1q32/cctools-port), and
   [`ldid`](https://github.com/ProcursusTeam/ldid) for fake code signing.
4. **Compile** all ~366 sources for `armv6-apple-ios3.0` (parallel, incremental).
5. **Link** against the SDK frameworks → `armv6` Mach-O executable.
6. **Package** a fake-signed `.ipa`.

## Key compiler flags (matching Xcode 4.1)

| Flag | Why |
|------|-----|
| `-target armv6-apple-ios3.0` | armv6, deployment target from the project (`IPHONEOS_DEPLOYMENT_TARGET`). |
| `-std=gnu++98` | The code predates C++11. Brace-init **narrowing** (e.g. `RakPeer.cpp`, `NetherReactorPattern.cpp`) must remain a warning, not a hard error. |
| `-fno-objc-arc` | iOS 3-era Objective-C is manual reference counting. |
| `-fobjc-abi-version=2` | Matches the device runtime. |
| `-DGFX_SMALLER_CHUNKS` | From `OTHER_CPLUSPLUSFLAGS`. |
| `-DNS_BLOCK_ASSERTIONS=1 -fomit-frame-pointer -ffast-math` | From `OTHER_CFLAGS`. |
| `-include .../minecraftpe-Prefix.pch` | The project's prefix header (`GCC_PREFIX_HEADER`). |

### Header search paths

The project uses mostly **relative** includes (`"../../Minecraft.h"`), which
clang resolves against each file's own directory automatically. Only a few extra
`-I` paths are needed:

- `handheld/src/raknet` — RakNet uses flat includes (`"BitStream.h"`).
- `handheld/lib/include` — bundled third-party headers.
- `InAppSettingsKit/{Controllers,Models,Views}` — the settings UI sub-project.

> **Important:** do *not* add every source directory to `-I`. `handheld/src/platform`
> contains a `time.h` that would shadow the SDK's `<time.h>` and break libstdc++.

## A real bug this surfaces

`xcparse --resolve` does **case-insensitive** path resolution on purpose. The
project references `world/entity/PathFinderMob.cpp` (capital F) but the file on
disk is `PathfinderMob.cpp` (lowercase f). On macOS's case-insensitive
filesystem this builds fine; on Linux it would fail without the fallback.

## xcparse usage

```bash
cc -O2 -o xcparse tools/xcparse/xcparse.c

PBX=handheld/project/iosproj/minecraftpe.xcodeproj/project.pbxproj
./xcparse $PBX --what targets                              # list targets
./xcparse $PBX --target minecraftpe --what configs         # list configs
./xcparse $PBX --target minecraftpe --what sources --resolve   # on-disk source paths
./xcparse $PBX --target minecraftpe --what frameworks      # linked frameworks
./xcparse $PBX --target minecraftpe --config Release --what setting:IPHONEOS_DEPLOYMENT_TARGET
```

`--resolve` resolves each source to its real on-disk path (case-insensitively)
and reports any genuinely missing files on stderr.

## Local build dependencies (Debian/Ubuntu)

```bash
sudo apt-get install -y clang llvm llvm-dev libclang-dev \
    build-essential git wget zip libplist-dev libssl-dev pkg-config
```

`libssl-dev` is required by `ldid`; `libplist-dev` by both `ldid` and cctools.

## Output

```
build/minecraftpe          # raw armv6 Mach-O executable (NOUNDEFS)
build/Payload/...          # assembled .app bundle
build/minecraftpe.ipa      # fake-signed, installable on a jailbroken iOS 3.x device
```

The `.ipa` is fake-signed (`ldid`), so it installs on a **jailbroken** device
(or any device that accepts ad-hoc/self-signed binaries). For a normal App Store
/ developer-signed build you'd re-sign with a real certificate.
