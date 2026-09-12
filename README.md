<table>
  <tr>
    <td width="140"><img src="assets/jitter.png" width="120" alt="Jitter logo"></td>
    <td><h1>Jitter</h1><sub>Seeded motion glitches for After Effects, native on Apple Silicon</sub></td>
  </tr>
</table>

**Free and open source. Download directly—no DM or beta invitation needed.**

[**Download the latest Mac release**](https://github.com/theomirzakhanian/Jitter/releases/latest)
· [Installation walkthrough](INSTALL.md)
· [Release notes](CHANGELOG.md)
· [Report a problem](https://github.com/theomirzakhanian/Jitter/issues)

Jitter is an independent C++ After Effects plugin inspired by Video Copilot's
Twitch. It provides Slide, Scale, Time, Color, Light and Blur, with seeded
animation, Smart Render and Multi-Frame Rendering support.

## Download and install

1. Open the [latest release](https://github.com/theomirzakhanian/Jitter/releases/latest).
2. Under **Assets**, download **`Jitter-v1.3.0-macOS-universal.zip`** (or the
   newer version's similarly named zip). **Do not choose “Source code (zip)”:**
   that contains source files, not the compiled plugin.
3. Unzip it and quit After Effects completely.
4. In Terminal, type `cd ` with a trailing space, drag the extracted **Jitter**
   folder into the window, then press Return. Run:

   ```bash
   bash install.sh
   ```

5. Reopen After Effects. Search for **Jitter** in **Effects & Presets**, or use
   **Effect > Video Copilot > Jitter**.

The installer asks for your Mac administrator password to copy into installed
After Effects folders in `/Applications`. Terminal shows no characters while
that password is typed. It also removes download quarantine from this bundle.
The build is ad-hoc signed, not Apple-notarized.

[The full walkthrough](INSTALL.md) covers verifying the download, manual
installation, updates, and missing-effect errors.

## Try the RGB glitch

Apply Jitter to footage or a text layer, then set:

| Control | Value |
| --- | --- |
| Master Amount | 10 |
| Enable > Slide | On |
| Slide > Amount | 10 |
| Slide > Twitches/sec | 5 |
| Slide > RGB Split | 100 |
| Enable > Blur | Off |

Preview several seconds. **Slide Amount controls the shake; RGB Split controls
chromatic separation independently of Slide Amount.** At RGB Split 100, the
shared slide is replaced by independent channel motion. At intermediate
values, some shared motion remains. Set Slide Amount to 0 for separation
without base translation. Master Amount still scales both; Master Amount 0
or disabling Slide turns both off.

To try a shake without color separation, set RGB Split to 0 and raise Slide
Amount. Other operator Amounts start at 0; enabling them alone does not animate
them.

## Compatibility and release status

**v1.3.0 is a public release.** See [CHANGELOG.md](CHANGELOG.md) for changes and
upgrade notes. The latest-release page is the canonical download; an aescripts
listing is not currently offered here.

| Requirement | Support |
| --- | --- |
| Platform | macOS; universal arm64 + x86_64 bundle |
| After Effects | 2024 or newer is the project target |
| macOS version | Bundle targets macOS 11+; your AE version may require a newer macOS |
| Color depth | 8 and 16 bits per channel; 32-bit float rendering is not supported |
| Windows | No Windows binary provided |

The v1.3.0 checks cover the standalone engines, the mocked AE render pipeline
(including RGB separation in 8/16-bit), and the universal release build. These
checks do not establish compatibility with every AE/macOS combination.

## Opening projects and presets that used Twitch

Jitter registers both `ADBE Jitter` and the legacy match name
`Videocopilot Twitch`. Parameter order and disk IDs remain unchanged in v1.3.0
so existing projects can resolve the effect and load their saved values.

**Saved RGB Split looks change in v1.3.0**, including through the legacy Twitch
registration: lowering Slide Amount no longer suppresses separation. Review
comps using RGB Split before final export; reduce RGB Split if the result is
stronger than intended. Keep the previous release if you must reproduce an
old Jitter render exactly.

Jitter is not a frame-for-frame Twitch replacement. The active renderer uses a
different random generator, and some controls remain approximate or inactive.

## Operators and known limitations

| Operator | Current behavior |
| --- | --- |
| Slide | XY shake, direction/spread/tendency and RGB separation |
| Scale | Zoom around the selected origin |
| Time | Frame offsets, forward-only or both directions |
| Color | Hue rotation / colorize approximation |
| Light | Brightness adjustment |
| Blur | Directional tap-filter approximation |

- **An active Blur currently takes precedence over RGB Split.** Disable Blur
  on the same Jitter instance for RGB separation. This release does not change
  that limitation.
- Master Speed is not used by the active engine.
- Spread is not applied when Direction is 0 (or 360).
- Slide/Scale Motion Blur, Scale Origin Randomize, Color Randomize and several
  advanced Blur controls are present for parameter compatibility but not
  implemented. Use Lens Blur does not select a lens-blur renderer.
- Show Twitch Only is a debug view, not the original modulation overlay.
- Color, Light and Blur do not reproduce the original Twitch algorithms.
- The separate Twitch-compatible experimental value engine is not connected
  to the plugin renderer.

## Help and contribution

[Installation and troubleshooting](INSTALL.md) · [Build and tests](BUILD.md)
· [Issue tracker](https://github.com/theomirzakhanian/Jitter/issues)

When reporting a problem, include Jitter version, AE version, macOS version,
Mac chip, the exact error, and steps/settings that reproduce it. For visual
problems, include composition size and bit depth. Remove personal paths or
private project content from anything you share.

## License and naming

MIT; see [LICENSE](LICENSE). Twitch is a Video Copilot trademark. Jitter is
independently developed and is not affiliated with Video Copilot.
