#!/bin/bash
#
# Jitter installer.
#
#   ./install.sh                  installs Jitter.plugin found next to this script
#   ./install.sh /path/to/Jitter.plugin
#
# Finds every installed After Effects, clears the quarantine flag, and copies
# the bundle in with the attributes stripped. Both of those steps matter: a
# quarantined bundle and a signature broken by a Finder copy each make AE skip
# the plugin without printing anything.

set -u

BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; OFF=$'\033[0m'

say()  { printf '%s\n' "$*"; }
ok()   { printf '%s✓%s %s\n' "$GREEN" "$OFF" "$*"; }
warn() { printf '%s!%s %s\n' "$YELLOW" "$OFF" "$*"; }
die()  { printf '%s✗ %s%s\n' "$RED" "$*" "$OFF" >&2; exit 1; }

# ---- locate the plugin -------------------------------------------------
# Checked in order: the argument, next to this script, the working directory,
# then ~/Downloads. That covers unzipping anywhere and running from anywhere.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PLUGIN=""

for candidate in \
	"${1:-}" \
	"$SCRIPT_DIR/Jitter.plugin" \
	"$PWD/Jitter.plugin" \
	"$HOME/Downloads/Jitter.plugin"
do
	[ -n "$candidate" ] || continue
	if [ -d "$candidate" ]; then PLUGIN="$candidate"; break; fi
done

if [ -z "$PLUGIN" ]; then
	die "Couldn't find Jitter.plugin.

Looked next to this script, in $PWD, and in ~/Downloads.
Pass the path directly:

    $BOLD./install.sh /path/to/Jitter.plugin$OFF

Tip: type './install.sh ' then drag Jitter.plugin onto the Terminal window."
fi

# A .plugin is a directory, so confirm it is actually our bundle and not an
# empty folder or a half-extracted archive.
[ -f "$PLUGIN/Contents/MacOS/Jitter" ] || die "\"$PLUGIN\" is not a complete Jitter bundle
(missing Contents/MacOS/Jitter). Re-download and unzip it again."

PLUGIN="$(cd -- "$PLUGIN" >/dev/null 2>&1 && pwd)"
ok "Found $PLUGIN"

# ---- refuse to install underneath a running AE -------------------------
if pgrep -f "Adobe After Effects" >/dev/null 2>&1; then
	die "After Effects is running. Quit it completely, then run this again.
AE does not reload a plugin that changes underneath it."
fi

# ---- find the hosts ----------------------------------------------------
shopt -s nullglob
AE_DIRS=(/Applications/Adobe\ After\ Effects\ */)
shopt -u nullglob

if [ ${#AE_DIRS[@]} -eq 0 ]; then
	die "No After Effects installation found in /Applications.

If yours lives elsewhere, copy the bundle in by hand:
    sudo ditto --norsrc --noextattr --noacl \\
      \"$PLUGIN\" \"/your/AE/Plug-ins/Effects/Jitter.plugin\""
fi

say ""
say "${BOLD}Installing to ${#AE_DIRS[@]} After Effects installation(s):${OFF}"
for d in "${AE_DIRS[@]}"; do say "    $(basename "$d")"; done
say ""

# ---- clear quarantine --------------------------------------------------
# Safe to run on a copy that was never quarantined; it just does nothing.
xattr -dr com.apple.quarantine "$PLUGIN" 2>/dev/null
xattr -cr "$PLUGIN" 2>/dev/null
ok "Cleared download quarantine"

# ---- install -----------------------------------------------------------
say "Administrator access is needed to write into /Applications."
installed=0
failed=0

for AE in "${AE_DIRS[@]}"; do
	EFFECTS="${AE}Plug-ins/Effects"
	NAME="$(basename "$AE")"

	if [ ! -d "$EFFECTS" ]; then
		warn "$NAME: no Plug-ins/Effects folder, skipping"
		continue
	fi

	DEST="$EFFECTS/Jitter.plugin"
	if sudo rm -rf "$DEST" && sudo ditto --norsrc --noextattr --noacl "$PLUGIN" "$DEST"; then
		if codesign -v "$DEST" 2>/dev/null; then
			ok "$NAME"
			installed=$((installed + 1))
		else
			warn "$NAME: installed, but the signature does not verify. AE may refuse to load it."
			installed=$((installed + 1))
		fi
	else
		warn "$NAME: copy failed"
		failed=$((failed + 1))
	fi
done

say ""
if [ "$installed" -eq 0 ]; then
	die "Nothing was installed."
fi

if [ "$failed" -gt 0 ]; then
	ok "Installed into $installed installation(s), $failed failed"
else
	ok "Installed into $installed installation(s)"
fi
say ""
say "Open After Effects and look under ${BOLD}Effect > Video Copilot > Jitter${OFF},"
say "or type \"Jitter\" in the Effects & Presets panel."
say ""
say "Nothing moves until you raise an operator's Amount: enable Slide and pull"
say "${BOLD}Slide > Amount${OFF} up. The master Amount alone will not do it."
