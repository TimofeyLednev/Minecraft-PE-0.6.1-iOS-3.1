#!/usr/bin/env bash
#
# build-ios.sh — cross-compile Minecraft PE 0.6.1 for iOS 3.x / armv6 on Linux.
#
# This reproduces, on plain Linux, what you used to do in Xcode 4.1 against the
# iOS 4.3 SDK. It:
#   1. fetches the iOS 4.3 SDK (last SDK line with real armv6 framework slices),
#   2. builds a Mach-O toolchain (cctools-port ld64 + lipo/strip) and ldid,
#   3. reads minecraftpe.xcodeproj with the bundled `xcparse` tool to get the
#      exact source list + build settings (no hand-maintained file lists),
#   4. compiles every source for armv6 and links against the SDK frameworks,
#   5. packages a fake-signed .ipa.
#
# Usage:
#   ./build-ios.sh [--target minecraftpe] [--config Release] [--jobs N]
#
# Env knobs (all optional):
#   CLANG, LLVM_CONFIG   toolchain binaries     (default: clang, llvm-config)
#   DEPLOY_TARGET        min iOS version        (default: from xcodeproj, else 3.0)
#   SDK_DIR              reuse an existing SDK   (skips download)
#   KEEP_WORK            "1" keeps build/work between runs (default: cached anyway)
#
set -euo pipefail

scriptroot="$(cd "$(dirname "$0")" && pwd)"
cd "$scriptroot"

# ---------------------------------------------------------------------------
# Arguments
# ---------------------------------------------------------------------------
TARGET="minecraftpe"
CONFIG="Release"
JOBS="$(nproc 2>/dev/null || echo 2)"
while [ $# -gt 0 ]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --jobs|-j) JOBS="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

ARCH="armv6"
CLANG="${CLANG:-clang}"
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"

PROJ="handheld/project/iosproj/minecraftpe.xcodeproj/project.pbxproj"
PROJDIR="handheld/project/iosproj"
IASK="handheld/project/lib_projects/InAppSettingsKit"

workdir="$scriptroot/build/work"
objdir="$workdir/obj/$ARCH"
tcbin="$workdir/toolchain/bin"
mkdir -p "$objdir" "$tcbin"
export PATH="$tcbin:$PATH"

# ---------------------------------------------------------------------------
# 0. Build the xcparse helper (reads the .xcodeproj for us)
# ---------------------------------------------------------------------------
xcparse="$workdir/xcparse"
if [ ! -x "$xcparse" ] || [ tools/xcparse/xcparse.c -nt "$xcparse" ]; then
    printf '\n==> Building xcparse...\n'
    cc -O2 -o "$xcparse" tools/xcparse/xcparse.c
fi

DEPLOY_TARGET="${DEPLOY_TARGET:-$("$xcparse" "$PROJ" --target "$TARGET" --config "$CONFIG" \
    --what setting:IPHONEOS_DEPLOYMENT_TARGET 2>/dev/null || true)}"
DEPLOY_TARGET="${DEPLOY_TARGET:-3.0}"
TRIPLE="${ARCH}-apple-ios${DEPLOY_TARGET}"

# ---------------------------------------------------------------------------
# 1. Fetch the iOS 4.3 SDK (real armv6 slices in UIKit/OpenGLES/QuartzCore/...)
# ---------------------------------------------------------------------------
sdk="${SDK_DIR:-$workdir/sdks/iPhoneOS4.3.sdk}"
if [ ! -d "$sdk" ]; then
    printf '\n==> Fetching iOS 4.3 SDK...\n'
    rm -rf "$workdir/sdks/_dl"
    mkdir -p "$workdir/sdks/_dl" "$workdir/sdks"
    ( cd "$workdir/sdks/_dl"
      git init -q
      git remote add origin https://github.com/EachAndOther/Legacy-iOS-SDKs.git
      git config core.sparseCheckout true
      echo "iPhoneOS4.3.sdk/*" > .git/info/sparse-checkout
      git pull -q --depth 1 origin master )
    mv "$workdir/sdks/_dl/iPhoneOS4.3.sdk" "$sdk"
    rm -rf "$workdir/sdks/_dl"
fi
# Sanity: the SDK must carry an armv6 slice for UIKit.
if ! file "$sdk/System/Library/Frameworks/UIKit.framework/UIKit" | grep -q armv6; then
    echo "ERROR: iOS 4.3 SDK has no armv6 UIKit slice — wrong SDK." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 2. Build the Mach-O linker (cctools-port ld64) + lipo/strip, and ldid (once)
# ---------------------------------------------------------------------------
if [ ! -x "$tcbin/ld64.ld64" ] || [ ! -x "$tcbin/lipo" ]; then
    printf '\n==> Building cctools-port (ld64, lipo, strip)...\n'
    cctools_commit=fee8115127bb849d7481ea0015f181d3ebbd33cf
    ( cd "$workdir"
      rm -rf "cctools-port-$cctools_commit"
      wget -qO- "https://github.com/Un1q32/cctools-port/archive/$cctools_commit.tar.gz" | tar -xz
      cd "cctools-port-$cctools_commit/cctools"
      ./configure --enable-silent-rules --with-llvm-config="$LLVM_CONFIG" CC="$CLANG" CXX="${CLANG}++"
      make -C libstuff -j"$JOBS"
      make -C libmacho -j"$JOBS"
      make -C ld64 -j"$JOBS"
      make -C misc strip lipo -j"$JOBS"
      cp ld64/src/ld/ld "$tcbin/ld64.ld64"
      cp misc/lipo "$tcbin/lipo"
      cp misc/strip "$tcbin/cctools-strip" )
fi

if ! command -v ldid >/dev/null && [ ! -x "$tcbin/ldid" ]; then
    printf '\n==> Building ldid (fake code signing)...\n'
    ldid_commit=ef330422ef001ef2aa5792f4c6970d69f3c1f478
    ( cd "$workdir"
      rm -rf "ldid-$ldid_commit"
      wget -qO- "https://github.com/ProcursusTeam/ldid/archive/$ldid_commit.tar.gz" | tar -xz
      cd "ldid-$ldid_commit"
      make CXX="${CLANG}++" LDFLAGS="-lplist-2.0"
      cp ldid "$tcbin/ldid" )
fi
LDID="$(command -v ldid || echo "$tcbin/ldid")"

# ---------------------------------------------------------------------------
# 3. Gather sources + settings from the .xcodeproj
# ---------------------------------------------------------------------------
printf '\n==> Reading %s (target=%s config=%s)...\n' "$PROJ" "$TARGET" "$CONFIG"
mapfile -t SOURCES < <("$xcparse" "$PROJ" --target "$TARGET" --what sources --resolve)
mapfile -t FRAMEWORKS < <("$xcparse" "$PROJ" --target "$TARGET" --what frameworks)
PREFIX_PCH="$PROJDIR/$("$xcparse" "$PROJ" --target "$TARGET" --config "$CONFIG" \
    --what setting:GCC_PREFIX_HEADER)"
PRODUCT="$("$xcparse" "$PROJ" --target "$TARGET" --config "$CONFIG" --what setting:PRODUCT_NAME)"
[ "$PRODUCT" = "\$(TARGET_NAME)" ] && PRODUCT="$TARGET"

printf '    %d source files, %d frameworks, deploy target iOS %s\n' \
    "${#SOURCES[@]}" "${#FRAMEWORKS[@]}" "$DEPLOY_TARGET"

# Header search paths: the project relies mostly on relative includes, plus a
# flat raknet dir, the bundled lib headers, and InAppSettingsKit subdirs.
INCS=(
    -I "handheld/src/raknet"
    -I "handheld/lib/include"
    -I "$IASK/Controllers" -I "$IASK/Models" -I "$IASK/Views"
)

# Flags chosen to match Xcode 4.1 / GCC-LLVM behaviour:
#   -std=gnu++98             the project predates C++11; brace-init narrowing
#                            must stay a warning, not an error.
#   -fno-objc-arc            iOS 3-era code is manual reference counting.
#   -DGFX_SMALLER_CHUNKS     from OTHER_CPLUSPLUSFLAGS.
#   -fomit-frame-pointer -ffast-math -DNS_BLOCK_ASSERTIONS=1  from OTHER_CFLAGS.
COMMON_FLAGS=(
    -target "$TRIPLE" -isysroot "$sdk"
    -fno-objc-arc -fobjc-abi-version=2
    -DGFX_SMALLER_CHUNKS -DNS_BLOCK_ASSERTIONS=1
    -fomit-frame-pointer -ffast-math -Os
    -Wno-everything
    -include "$PREFIX_PCH"
    "${INCS[@]}"
)

# ---------------------------------------------------------------------------
# 4. Compile every source for armv6 (incremental + parallel)
# ---------------------------------------------------------------------------
printf '\n==> Compiling %d files for %s...\n' "${#SOURCES[@]}" "$TRIPLE"

# COMMON_FLAGS isn't exportable as an array; pass it to subshells via a file.
printf '%s\n' "${COMMON_FLAGS[@]}" > "$workdir/common.flags"

OBJECTS=()
jobs_running=0
i=0
for src in "${SOURCES[@]}"; do
    i=$((i+1))
    obj="$objdir/$(printf '%05d_%s.o' "$i" "$(basename "$src")")"
    OBJECTS+=("$obj")
    # Incremental: skip if object is newer than source.
    if [ -f "$obj" ] && [ "$obj" -nt "$src" ]; then continue; fi
    (
        std=()
        case "$src" in *.cpp|*.cc|*.cxx|*.mm) std=(-std=gnu++98) ;; esac
        mapfile -t CF < "$workdir/common.flags"
        "$CLANG" "${CF[@]}" "${std[@]}" -c "$src" -o "$obj"
    ) &
    jobs_running=$((jobs_running+1))
    if [ "$jobs_running" -ge "$JOBS" ]; then wait -n; jobs_running=$((jobs_running-1)); fi
done
wait

# ---------------------------------------------------------------------------
# 5. Link against the SDK frameworks
# ---------------------------------------------------------------------------
printf '\n==> Linking %s...\n' "$PRODUCT"
out="$scriptroot/build"
mkdir -p "$out"

FRAMEWORK_FLAGS=()
for fw in "${FRAMEWORKS[@]}"; do FRAMEWORK_FLAGS+=(-framework "$fw"); done

"$CLANG" \
    -target "$TRIPLE" -isysroot "$sdk" \
    -fuse-ld=ld64 -mlinker-version=762 \
    -fno-objc-arc -fobjc-abi-version=2 \
    -lz -lstdc++ \
    "${FRAMEWORK_FLAGS[@]}" \
    "${OBJECTS[@]}" \
    -o "$out/$PRODUCT"

file "$out/$PRODUCT"

# ---------------------------------------------------------------------------
# 6. Assemble the .app bundle, fake-sign, zip into an .ipa
# ---------------------------------------------------------------------------
printf '\n==> Packaging .ipa...\n'
INFOPLIST="$PROJDIR/$("$xcparse" "$PROJ" --target "$TARGET" --config "$CONFIG" --what setting:INFOPLIST_FILE)"
app="$out/Payload/$PRODUCT.app"
rm -rf "$out/Payload"
mkdir -p "$app"
cp "$out/$PRODUCT" "$app/$PRODUCT"

if command -v plistutil >/dev/null; then
    plistutil -i "$INFOPLIST" -o "$app/Info.plist" -f bin 2>/dev/null || cp "$INFOPLIST" "$app/Info.plist"
else
    cp "$INFOPLIST" "$app/Info.plist"
fi

# Bundle game resources (textures, sounds, lang, ...) if present.
if [ -d "handheld/data" ]; then
    cp -R handheld/data/. "$app/" 2>/dev/null || true
fi

if [ -f "$scriptroot/build-ios.entitlements.plist" ]; then
    "$LDID" -S"$scriptroot/build-ios.entitlements.plist" "$app/$PRODUCT"
else
    "$LDID" -S "$app/$PRODUCT"
fi

( cd "$out" && rm -f "$PRODUCT.ipa" && zip -qr "$PRODUCT.ipa" Payload )

printf '\nDone. IPA: %s\n' "$out/$PRODUCT.ipa"
