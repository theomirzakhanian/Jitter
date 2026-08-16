<table>
  <tr>
    <td width="140"><img src="assets/jitter.png" width="120" alt="Jitter logo"></td>
    <td>
      <h1>Jitter</h1>
      <sub>Seeded motion glitches for After Effects, native on Apple Silicon</sub>
    </td>
  </tr>
</table>

![macOS](https://img.shields.io/badge/macOS-11%2B-lightgrey)
![Universal](https://img.shields.io/badge/binary-arm64%20%2B%20x86__64-blue)
![After Effects](https://img.shields.io/badge/After%20Effects-2024%2B-9999ff)
![License](https://img.shields.io/badge/license-MIT-green)

Video Copilot's Twitch is x86_64 only. On an M-series Mac it runs through
Rosetta if it runs at all, and recent AE builds don't load it cleanly. So I
rewrote it from scratch against the modern AE SDK, with Smart Render and
Multi-Frame Rendering support. Same six operators and the same UI layout, so
there's nothing to relearn.

<!-- demo GIF goes here: assets/demo.gif -->

## Install

Download the zip from the
[latest release](https://github.com/theomirzakhanian/Jitter/releases) and
unzip it (double-clicking in Finder is fine). It contains `Jitter.plugin` and
an `install.sh` next to it.

**Quit After Effects first.** It won't pick up a plugin that changes underneath
a running instance.

In Terminal, type `cd ` with a trailing space, drag the unzipped folder onto the
Terminal window, and press Return. Then run:

```bash
./install.sh
```

The script hunts down the bundle, finds every After Effects you have installed,
clears the download quarantine and copies the plugin in without breaking its
signature. It asks for your password, since AE keeps its plugins inside
`/Applications`.

Reopen AE and look under Effect > Video Copilot > Jitter, or type "Jitter" into
the Effects & Presets panel.

One thing catches almost everybody: nothing animates until you raise an
operator's Amount. Enable Slide, then pull `Slide > Amount` up. The master
Amount on its own does nothing.

<details>
<summary>Installing by hand</summary>

Run this from the folder holding `Jitter.plugin`, with the AE version changed to
match yours:

```bash
xattr -dr com.apple.quarantine Jitter.plugin

sudo ditto --norsrc --noextattr --noacl Jitter.plugin \
  "/Applications/Adobe After Effects 2026/Plug-ins/Effects/Jitter.plugin"
```

Both commands earn their keep. `xattr` clears the flag macOS stamps on
downloads, and Gatekeeper blocks the bundle while it is there. `ditto` with
those three flags copies without resource forks, extended attributes or ACLs,
where a plain `cp` or a drag in Finder brings along attributes that invalidate
the code signature. Either failure ends the same way: AE skips the plugin and
mentions it to nobody, so the install looks fine and the effect never shows up.

The build is ad-hoc signed rather than notarized, so macOS treats it as coming
from an unidentified developer. If you would rather compile it yourself, see
[BUILD.md](BUILD.md).

If Terminal answers `Cannot get the real path for source 'Jitter.plugin'`, you
are standing in the wrong folder. `cd` to the one holding the bundle and run it
again.
</details>

<details>
<summary>Checking that it worked</summary>

```bash
AE="/Applications/Adobe After Effects 2026"     # your version here
codesign -v "$AE/Plug-ins/Effects/Jitter.plugin" && echo "signature ok"
lipo -archs "$AE/Plug-ins/Effects/Jitter.plugin/Contents/MacOS/Jitter"
```

The second command should print `x86_64 arm64`.
</details>

<details>
<summary>Uninstalling</summary>

```bash
sudo rm -rf "/Applications/Adobe After Effects 2026/Plug-ins/Effects/Jitter.plugin"
```
</details>

## Operators

Six of them, each with its own seed, amount, and twitches-per-second:

| Operator | What it does |
| --- | --- |
| Slide | XY jitter, with direction angle, spread, and an RGB split sub-param for chromatic separation |
| Scale | Zoom around an origin you pick |
| Time | Frame jumps, forward only or both directions |
| Color | HSL hue rotation |
| Light | Brightness shift: brighter, darker, or both |
| Blur | Directional blur along the slide axis |

There's also a master Amount that scales all of them at once, plus Ease In,
Ease Out, and Randomize Minimum for the behaviour of each event.

## Opening projects that used Twitch

Jitter registers the legacy match name `Videocopilot Twitch` next to
`ADBE Jitter`, and its parameter disk IDs line up with the original's. Projects
and animation presets that reference Twitch will resolve to this plugin and
load their values into the right slots.

Frame-for-frame output won't match, though. The two plugins use different RNGs,
so individual frame values differ even where the visual envelopes (peak
amplitudes, event rates) line up. Presets that touch parameters outside the six
operators above are also untested.

## Status

It works. Renders correctly on Apple Silicon, handles adjustment layers,
survives scrubbing, doesn't crash.

Three operators are simpler than the originals. Color does a flat hue rotation
where Twitch does HSL range selection. Blur is a directional tap filter where
Twitch's is FFT-based. Light is a straight brightness shift. The output is
believable rather than identical.

## How it works

The engine is stateless. Given a seed and a time it returns every operator's
value in O(1), without walking the timeline from t=0. That's what makes
Multi-Frame Rendering safe: any frame can render in any order on any thread,
with no shared state between them.

Each operator runs on its own event grid:

```
i        = floor(t * twitches_per_sec)
v_curr   = randomTarget(hash(master_seed, op_seed, op_id, channel, i))
v_next   = randomTarget(hash(master_seed, op_seed, op_id, channel, i+1))
local_t  = t * twitches_per_sec - i
output   = lerp(v_curr, v_next, ease(local_t, behavior))
```

`hash` is splitmix64 over five 32-bit lanes: master seed, operator seed,
operator id, channel id, event index. Salting per operator is why changing
Scale's seed doesn't move Slide.

`SmartPreRender` reads the params, runs the engine at `current_time`, and
stashes the result in `extra->output->pre_render_data`. `SmartRender` picks it
back up, builds a transform matrix, then either goes straight to output (when
no operator is active) or pushes a temp world through the Color, Light, and
Blur passes.

`test_full_render.cpp` drives that pipeline against a mocked AE host, so render
bugs show up in a two-second test run instead of an install-and-relaunch cycle.
It caught an output world arriving with an `extent_hint` of zero, which
silently writes no pixels at all. That one would have cost me days inside AE.

## Building it yourself

[BUILD.md](BUILD.md) has the details. The short version: clone into the AE
SDK's `Effect/` folder, `xcodebuild -configuration Release`, and you get a
signed universal bundle you can install with the same `ditto` command above.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| `Cannot get the real path for source 'Jitter.plugin'` | Terminal is looking in a different folder from the bundle, so nothing got installed. Type `cd `, drag the folder holding `Jitter.plugin` onto the window, press Return, then run `./install.sh`. |
| `permission denied: ./install.sh` | The script lost its executable bit somewhere along the way. Run `bash install.sh` instead. |
| Effect appears nowhere and the install reported no errors | Check which release you have. Everything before v1.1.1 was stamped with a minimum macOS of 26.2, and AE passes over it without comment on anything older. Download the current release. |
| Effect doesn't appear in AE | AE was running during install. Quit it fully and reopen. |
| Effect is there but nothing animates | Operator Amounts start at 0. Enable Slide and raise `Slide > Amount`. The master Amount on its own will not move anything. |
| Still doesn't appear | Quarantine flag or a broken signature. Run the two checks above; if `codesign -v` complains, reinstall with `ditto`, not `cp` or Finder drag-and-drop. |
| "Jitter.plugin is damaged and can't be opened" | Gatekeeper. `xattr -dr com.apple.quarantine Jitter.plugin`, then install again. |
| Installed, but an old project still says the effect is missing | The project references Twitch by match name. That resolves only once Jitter is loaded, so restart AE after installing. |
| Renders blank | Set `JITTER_DEBUG_LOG` to 1 in `Jitter.cpp`, rebuild, and read `/tmp/jitter_debug.log`. One line per render call. |

## Naming

Twitch is a Video Copilot trademark. This plugin is called Jitter. It registers
under Twitch's match name only so existing projects keep working.

## License

MIT, see [LICENSE](LICENSE).
