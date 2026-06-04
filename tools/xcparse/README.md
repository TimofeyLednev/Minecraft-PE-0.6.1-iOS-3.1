# xcparse

A tiny, zero-dependency C tool that reads an Xcode project
(`*.xcodeproj/project.pbxproj`) and prints a flat build plan for cross-compiling
on Linux. Built for the `minecraftpe.xcodeproj` in this repo, but it's a general
OpenStep-plist (pbxproj) reader.

## Build

```bash
cc -O2 -o xcparse xcparse.c
```

No dependencies beyond libc.

## Usage

```
xcparse <project.pbxproj> [--target NAME] [--config NAME] [--what WHAT]
```

`WHAT` is one of:

| `--what`         | Output |
|------------------|--------|
| `targets`        | All native target names. |
| `configs`        | Build configuration names for the target. |
| `sources`        | Source file paths (relative to the `.xcodeproj` dir) in the Sources build phase. |
| `sources --resolve` | Same, but resolved to real on-disk paths (case-insensitively); missing files reported on stderr. |
| `frameworks`     | Linked framework names (no `.framework` suffix). |
| `setting:KEY`    | A single build setting, merged from project- and target-level configs. |

## Examples

```bash
PBX=../../handheld/project/iosproj/minecraftpe.xcodeproj/project.pbxproj

./xcparse $PBX --what targets
# minecraftpe.demo
# minecraftpe

./xcparse $PBX --target minecraftpe --what sources --resolve | head
./xcparse $PBX --target minecraftpe --what frameworks
./xcparse $PBX --target minecraftpe --config Release --what setting:IPHONEOS_DEPLOYMENT_TARGET
```

## What it does

- Parses the OpenStep-plist object graph (handles comments, quoted/bareword
  tokens, nested dicts/arrays).
- Walks `PBXGroup` parent chains to resolve each `PBXFileReference` to a real
  relative path, honouring `sourceTree` (`<group>`, `SOURCE_ROOT`, `<absolute>`).
- Merges build settings from the **project-level** and **target-level**
  `XCBuildConfiguration` (target overrides project).
- `--resolve` does case-insensitive on-disk lookup, which catches
  case-mismatch bugs that only work on macOS's case-insensitive filesystem
  (e.g. `PathFinderMob.cpp` vs `PathfinderMob.cpp`).

See [`../../docs/CROSS-COMPILE.md`](../../docs/CROSS-COMPILE.md) for the full
build pipeline that uses this tool.
