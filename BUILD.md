# Building Jitter

You need the Adobe After Effects SDK. Download it from Adobe's developer site (a free signup, no NDA, no fee) and drop this folder into `SDK/Effect/Jitter/` next to the sample plugins.

## Requirements

- macOS with Xcode (tested with Xcode 16+ on macOS 15+)
- Apple Silicon or Intel Mac
- After Effects 2024 or newer for testing

## Quick build

```bash
cd Mac
xcodebuild -project Jitter.xcodeproj -configuration Release -arch arm64
```

The build produces `Mac/build/Release/Skeleton.plugin` (Xcode target name is still "Skeleton" from the template; rename if you care). The plist inside is set up for Jitter — `CFBundleExecutable=Jitter`, `CFBundleIdentifier=com.adobe.AfterEffects.Jitter`.

## Install

After Effects loads `.plugin` bundles from `/Applications/Adobe After Effects <version>/Plug-ins/Effects/`. The bundle needs to be named `Jitter.plugin` and the executable inside renamed to match. Codesign with an ad-hoc signature so Gatekeeper lets AE load it:

```bash
# Rename, sign, install (adjust AE version as needed)
PLUGIN_BUILD="Mac/build/Release/Skeleton.plugin"
PLUGIN_DEST="/Applications/Adobe After Effects 2025/Plug-ins/Effects/Jitter.plugin"

cp -R "$PLUGIN_BUILD" /tmp/Jitter.plugin
mv /tmp/Jitter.plugin/Contents/MacOS/Skeleton /tmp/Jitter.plugin/Contents/MacOS/Jitter
xattr -cr /tmp/Jitter.plugin
codesign --force --sign - --timestamp=none /tmp/Jitter.plugin

# Quit AE first, then:
sudo rm -rf "$PLUGIN_DEST"
sudo ditto --norsrc --noextattr --noacl /tmp/Jitter.plugin "$PLUGIN_DEST"
```

The `xattr -cr` and `ditto --noextattr` flags strip extended attributes that codesign refuses to touch — without them, signing fails with "resource fork, Finder information, or similar detritus not allowed."

## Standalone tests

Two test programs run without After Effects:

- `engine_test.cpp` — exercises the deterministic engine (seed independence, output ranges, smoothness).
- `test_full_render.cpp` — mocks the AE SmartRender pipeline. Useful for catching regressions in the render path without a full reinstall cycle.

```bash
# Engine tests
clang++ -std=c++17 -O2 engine_test.cpp JitterEngine.cpp -o engine_test
./engine_test

# Full-render harness (run from SDK/Effect/Jitter/)
clang++ -std=c++17 -O2 \
  -I../../Headers -I../../Headers/SP -I../../Util -I../../Resources \
  -DAE_OS_MAC=1 -DPF_DEEP_COLOR_AWARE=1 \
  test_full_render.cpp Jitter.cpp Jitter_Strings.cpp \
  ../../Util/AEGP_SuiteHandler.cpp ../../Util/Smart_Utils.cpp \
  ../../Util/MissingSuiteError.cpp \
  -o test_full_render
./test_full_render
```

## Debug logging

`Jitter.cpp` has `JITTER_DEBUG_LOG` at the top. Set it to `1` and the plugin appends to `/tmp/jitter_debug.log` on every render call. Useful when something renders blank and you have no idea why.
