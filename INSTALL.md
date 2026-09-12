# Install Jitter on Mac

You do not need Xcode, the Adobe SDK, or a Reddit DM to use the release plugin.

## 1. Download the compiled plugin

Open [the latest release](https://github.com/theomirzakhanian/Jitter/releases/latest)
and expand **Assets** if necessary. Download
**Jitter-v1.3.0-macOS-universal.zip**, or the newer version's similarly named zip.

The automatically generated **Source code (zip)** and **Source code (tar.gz)**
files are for developers. They do not contain a ready-to-install Jitter.plugin.

Double-click the downloaded zip in Finder. Open the extracted **Jitter** folder.
It should contain:

```text
Jitter/
  Jitter.plugin
  install.sh
  README.md
  INSTALL.md
  CHANGELOG.md
  BUILD.md
  LICENSE
  assets/jitter.png
```

`Jitter.plugin` is a bundle and may look like a single file in Finder.

## 2. Quit After Effects

Use **After Effects > Quit After Effects** (Command-Q). Closing a project or a
window alone is not enough. Quit any other running AE versions as well.

## 3. Run the installer

1. Open **Terminal** from Applications > Utilities.
2. Type `cd `, including the space after `cd`.
3. Drag the extracted **Jitter folder** from Finder into Terminal. The command
   now contains its actual location; you do not need to type the path.
4. Press Return, then enter:

   ```bash
   bash install.sh
   ```

5. Read the list of After Effects installations. Enter your Mac administrator
   password if prompted. Terminal does not display dots or letters as you type
   it; press Return when done.

The script looks for Jitter.plugin beside itself and installs into the
`Plug-ins/Effects` folders of detected AE installations under `/Applications`.
It removes download quarantine from the bundle and verifies its signature after
copying. The binary is ad-hoc signed, not Apple-notarized; use the official
GitHub release or [build from source](BUILD.md).

**A signature warning means verification failed even if the script also says
“installed.”** Follow the checks below before treating that as success.

## 4. Find and try Jitter

Reopen AE and search **Jitter** in **Effects & Presets**. It also appears at
**Effect > Video Copilot > Jitter**. Apply it to a layer.

For a small shake with strong color separation:

- Master Amount: **10**
- Enable > Slide: **On**
- Slide > Amount: **10**
- Slide > Twitches/sec: **5**
- Slide > RGB Split: **100**
- Enable > Blur: **Off**

Preview a few seconds. In v1.3.0, lowering Slide Amount no longer weakens RGB
separation. Master Amount still controls the overall strength.

## Updating

Download the latest binary zip and repeat the steps with AE closed. The
installer replaces the existing Jitter.plugin in the detected installations.
Keep an older zip if you need to restore a previous render appearance.

v1.3.0 changes existing RGB Split looks. Review saved comps before final export;
see [release notes](CHANGELOG.md). A release filename alone does not tell you
which bundle AE currently has installed: v1.3.0 embeds its version in the bundle.

## Troubleshooting

| What you see | What to do |
| --- | --- |
| No Jitter.plugin in the extracted folder | Download the **macOS-universal** asset, not the source-code zip. |
| `install.sh: No such file or directory` | Repeat the `cd ` + drag-folder step. Run `ls` to confirm `install.sh` is in the current folder. |
| `Cannot get the real path for source 'Jitter.plugin'` | A manual copy command was run from the wrong folder. Use the installer steps above with the compiled download. |
| `permission denied: ./install.sh` | Run `bash install.sh`; it does not require the script's executable bit. |
| Installer says AE is running | Quit every AE version completely and retry. |
| No AE installation found | The installer searches `/Applications`. Use the manual path below for an installation elsewhere. |
| Effect missing from the panel | Confirm the installer named the AE version you reopened; clear the panel search and search Jitter. Then verify the installed bundle below. |
| Damaged/unidentified bundle or signature warning | Re-download the official asset, extract a fresh copy, and reinstall with AE closed. Use the manual checks below if it persists. |
| Effect loads, but nothing moves | Enable Slide and raise Slide Amount or RGB Split. Master Amount must be above 0. Other operators need their own Amount raised. |
| RGB split disappears with Blur | Active Blur overrides RGB Split in the current renderer. Disable Blur on that Jitter instance. |
| Old Twitch project still reports a missing effect | Verify Jitter is loaded after restarting AE. If it is, report the exact missing-effect name and project/preset steps. |

## Verify the installed bundle

Change the first line to match **your installed AE folder**, then run in Terminal:

```bash
JITTER_AE_DIR="/Applications/Adobe After Effects 2025"
JITTER_BUNDLE="$JITTER_AE_DIR/Plug-ins/Effects/Jitter.plugin"
/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$JITTER_BUNDLE/Contents/Info.plist"
codesign --verify --strict --verbose=2 "$JITTER_BUNDLE"
lipo -archs "$JITTER_BUNDLE/Contents/MacOS/Jitter"
```

For this release, the version should be **1.3.0**, codesign should report valid
verification, and the architecture list should contain **arm64** and **x86_64**
(in either order). Missing files mean the bundle is not installed at that path.
These checks verify the files; the final check is that Jitter loads in AE.

## Manual installation

Use this if AE is installed outside `/Applications`. Quit AE. Open Terminal in
the extracted Jitter folder using step 3 above, and replace the destination
with your actual AE folder:

```bash
xattr -dr com.apple.quarantine "Jitter.plugin"
sudo ditto --norsrc --noextattr --noacl "Jitter.plugin" \
  "/Applications/Adobe After Effects 2025/Plug-ins/Effects/Jitter.plugin"
```

The copy flags avoid carrying resource forks and extended attributes that can
interfere with signature verification. Reopen AE, then perform the checks above.

## Uninstall or roll back

Quit AE. In Finder, open the AE folder's **Plug-ins > Effects** folder and move
**Jitter.plugin** to Trash. Repeat for other AE installations if needed.
To roll back, install a previous binary from
[all releases](https://github.com/theomirzakhanian/Jitter/releases).

## Still stuck?

[Open an issue](https://github.com/theomirzakhanian/Jitter/issues) with the Jitter,
AE and macOS versions; Mac chip; downloaded asset filename; exact Terminal
error; and the result of the verification commands. For render problems,
include settings, composition dimensions and bit depth. Redact personal paths
and private project material before posting.
