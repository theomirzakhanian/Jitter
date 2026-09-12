# Release notes

## v1.3.0 — 2026-09-12

### RGB separation independent of Slide Amount

Lowering Slide Amount no longer suppresses RGB separation. The reported setup
(Master Amount 10, Slide Amount 10, RGB Split 100) now keeps strong chromatic
separation with a small base shake. Slide Amount 0 also allows RGB-only motion.
Master Amount, Slide's enable switch, seed, event rate and easing still apply.

The blend behavior is retained: RGB Split 0 follows the base slide, while RGB
Split 100 uses independent channel motion. Parameter order and disk IDs have
not changed. The same correction applies to the legacy Twitch registration.

**Upgrade note:** existing comps with RGB Split can render more strongly,
particularly at low Slide Amount. Review them before export and lower RGB Split
as needed. This is a Jitter behavior correction, not verified frame-for-frame
parity with Video Copilot Twitch. Keep an older release for exact old renders.

### Easier download and installation

- Direct latest-release link and clear binary-versus-source download guidance.
- A step-by-step installation guide included in the downloadable zip.
- First-use RGB settings, upgrade instructions and troubleshooting.
- Version 1.3.0 embedded in the plugin bundle for installed-version checks.
- About/help links point to Jitter, and Release builds explicitly include
  Apple Silicon and Intel architectures.
- Explicit supported formats, public-release status and known limitations.

### Validation

Engine regression tests exercise the 10/10/100 report, Slide Amount independence,
partial/full split, Master Amount scaling and disabling controls. The mocked AE
pipeline verifies visible RGB separation in both 8-bit and 16-bit output.
The experimental Twitch-engine tests remain separate from the active renderer.

### Known limitations

An active Blur still overrides RGB Split. Master Speed is inactive, and Spread
is inactive at Direction 0/360. Several advanced controls remain placeholders;
Color, Light and Blur approximate Twitch. See the [README](README.md) for the
full list. Automated/mocked checks do not replace testing in each AE version.

## Earlier releases

See [GitHub releases](https://github.com/theomirzakhanian/Jitter/releases) for
v1.2.0 and earlier release notes and binaries.
