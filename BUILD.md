# Building Jitter

## Requirements

- macOS with Xcode 16 or newer
- The Adobe After Effects SDK — free download from Adobe's developer site, no NDA, no fee
- After Effects 2024 or newer to test in

## Place the repo inside the SDK

The Xcode project reaches the SDK through relative paths (`../../../Headers`,
`../../../Util`), so the clone has to sit where the sample plugins do:

```bash
cd <AE SDK>/Effect
git clone https://github.com/theomirzakhanian/Jitter.git Jitter
```

The result should be `<AE SDK>/Effect/Jitter/Mac/Jitter.xcodeproj`. Cloning
anywhere else builds nothing — the headers won't resolve.

## Build

```bash
cd Jitter/Mac
xcodebuild -project Jitter.xcodeproj -configuration Release build
```

That produces `Mac/build/Release/Jitter.plugin` — a universal binary
(arm64 + x86_64), ad-hoc signed, ready to install as-is. No renaming, no
manual `codesign` step.

Use `-configuration Debug` while developing: unoptimized, native arch only,
builds faster.

## Install

```bash
# Quit After Effects first — it won't reload a plugin in place.
AE="/Applications/Adobe After Effects 2025"
sudo rm -rf "$AE/Plug-ins/Effects/Jitter.plugin"
sudo ditto --norsrc --noextattr --noacl \
  build/Release/Jitter.plugin "$AE/Plug-ins/Effects/Jitter.plugin"
```

The effect shows up as **Jitter** under the Video Copilot category.

`ditto --norsrc --noextattr --noacl` matters: extended attributes tag along
otherwise and invalidate the signature, after which AE silently declines to
load the bundle.

## Tests

Both harnesses run without launching After Effects. Run them from the repo root:

```bash
# Engine: determinism, seed independence, output envelopes, easing
clang++ -std=c++17 -O2 engine_test.cpp JitterEngine.cpp -o engine_test
./engine_test

# SmartRender pipeline against a mocked AE host
clang++ -std=c++17 -O2 \
  -I../../Headers -I../../Headers/SP -I../../Util -I../../Resources \
  -DAE_OS_MAC=1 -DPF_DEEP_COLOR_AWARE=1 \
  test_full_render.cpp Jitter.cpp Jitter_Strings.cpp \
  ../../Util/AEGP_SuiteHandler.cpp ../../Util/Smart_Utils.cpp \
  ../../Util/MissingSuiteError.cpp \
  -o test_full_render
./test_full_render
```

```bash
# Twitch-compatible engine: PRNG, event grid, envelope, slide
clang++ -std=c++17 -O2 twitch_engine_test.cpp JitterEngineTwitch.cpp -o twitch_engine_test
./twitch_engine_test
```

The render harness is worth running before any install cycle — it catches
blank-output regressions (a zero `extent_hint` writing no pixels, fast-path
vs slow-path divergence) that are miserable to diagnose inside AE.

`twitch_engine_test` pins the parity engine against real libc `rand()` output
captured from both arm64 and x86_64. If those first-six-draws assertions ever
fail, bit-exact compatibility with the original plugin is gone. It's also worth
running under `-fsanitize=thread` occasionally, since the whole point of that
engine is that it holds no shared state.

## Debug logging

`Jitter.cpp` has `JITTER_DEBUG_LOG` near the top. Set it to `1` and every
render call appends to `/tmp/jitter_debug.log`. Ships as `0`.

## Project layout notes

Three things about the Xcode project that will otherwise look like mistakes:

- **`JitterEngine.cpp` is not a target member.** `Jitter.cpp` `#include`s it
  directly (unity build) so the engine inlines into the render path. Adding it
  to the Sources phase gives you duplicate symbols.
- **`JitterPiPL.r` is not compiled.** The PiPL is registered programmatically
  in `PluginDataEntryFunction2`, which is how the modern SDK prefers it. The
  `.r` file is kept for reference.
- **There's a "Strip extended attributes" script phase.** iCloud-synced folders
  (`~/Documents`, `~/Desktop`) stamp `com.apple.FinderInfo` onto build output,
  and `codesign` rejects any bundle carrying it. The phase clears them right
  before signing. Harmless everywhere else.
