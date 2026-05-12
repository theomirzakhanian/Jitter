<table>
  <tr>
    <td width="140"><img src="assets/jitter.png" width="120" alt="Jitter logo"></td>
    <td>
      <h1>Jitter</h1>
      <sub>After Effects Plugin</sub>
    </td>
  </tr>
</table>

An After Effects plugin that does what Video Copilot's Twitch did, except it runs natively on Apple Silicon.

The original Twitch is x86_64 only, so on M-series Macs it runs through Rosetta if it runs at all, and current AE builds don't load it cleanly. I rewrote it from scratch against the modern AE SDK with Smart Render and Multi-Frame Rendering so it plays nice with current AE.

## What it does

Randomized, seeded motion glitches on a layer. Six operators:

- **Slide**: XY jitter, with direction angle, spread, and an RGB-split sub-param for chromatic split
- **Scale**: zoom around a configurable origin
- **Time**: frame jumps, one-direction or both
- **Color**: HSL hue rotation (v1 is simple; full HSL range selection is planned)
- **Light**: brightness shift (brighter, darker, or both)
- **Blur**: directional blur along the slide axis

Each operator has its own seed, amount, and twitches-per-second. On top of that there's a master Amount that scales everything at once, plus Ease In, Ease Out, and Randomize Minimum behaviour controls.

## Status

Works. Renders fine on Apple Silicon, doesn't crash, handles adjustment layers, survives scrubbing. The UI mirrors Twitch's layout so you don't have to relearn anything, and the legacy match name `Videocopilot Twitch` is registered alongside `ADBE Jitter` so projects and animation presets that reference Twitch resolve to this plugin.

What's not done:

- Frame-for-frame parity with Twitch. The two plugins use different RNGs, so individual frame values won't match. Visual envelopes (peak amplitudes, event rates) are close.
- Color, Light, and Blur are functional but simpler than Twitch's originals. Twitch's Color uses HSL range selection; mine does a flat hue rotation. Twitch's Blur is FFT-based; mine is a directional tap filter. The output is believable, just not identical.
- Animation presets that reference params outside the operators above may not load cleanly. The disk IDs are aligned with Twitch's, but I haven't tested every preset out there.

## How it's put together

The engine is stateless. Given `(seed, time)` it returns each operator's value in O(1) without walking the timeline from t=0. That property is what makes MFR safe: every frame can be rendered in any order, on any thread, with nothing shared between them.

Per-operator event grid:

```
i        = floor(t * twitches_per_sec)
v_curr   = randomTarget(hash(master_seed, op_seed, op_id, channel, i))
v_next   = randomTarget(hash(master_seed, op_seed, op_id, channel, i+1))
local_t  = t * twitches_per_sec - i
output   = lerp(v_curr, v_next, ease(local_t, behavior))
```

`hash` is splitmix64 over five 32-bit lanes (master_seed, op_seed, op_id, channel_id, event_idx). Salting per-operator means changing Scale's seed doesn't move Slide.

`SmartPreRender` reads all the params, runs the engine at `current_time`, and stashes the result in `extra->output->pre_render_data`. `SmartRender` recovers it, builds a transform matrix, and either fast-paths straight to output (no operators active) or runs through a temp world for the Color, Light, and Blur passes.

There's also a mock-AE harness in `test_full_render.cpp` that drives the SmartRender pipeline without launching AE. It caught at least one rendering bug (output world's `extent_hint` arriving as zero, which silently writes no pixels) that I'd have spent days chasing inside AE.

## Build and install

See [BUILD.md](BUILD.md). The short version: drop the folder into the AE SDK's `SDK/Effect/Jitter/`, build via Xcode, sign with `codesign -s -`, copy the bundle into AE's Plug-ins folder.

## Naming

Twitch is a Video Copilot trademark. This plugin is named Jitter; it happens to register under Twitch's match name so existing projects keep working.

## License

MIT. See [LICENSE](LICENSE).
