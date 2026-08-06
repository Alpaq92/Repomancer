#!/usr/bin/env bash
# Repomancer — Linux Tier-0 shell integration installer (per-user, no root).
#
#   ./install.sh [--bin /path/to/repomancer] [--uninstall]
#
# Installs an application entry, an icon, and context-menu items for whichever
# of Nemo / Nautilus / Dolphin / Thunar are present. All paths are per-user
# under $XDG_DATA_HOME (~/.local/share) and $XDG_CONFIG_HOME (~/.config).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
config="${XDG_CONFIG_HOME:-$HOME/.config}"

BIN=""
UNINSTALL=0
while [ $# -gt 0 ]; do
	case "$1" in
		--bin) BIN="$2"; shift 2 ;;
		--uninstall) UNINSTALL=1; shift ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done

app_desktop="$data/applications/repomancer.desktop"
icon_dst="$data/icons/hicolor/scalable/apps/repomancer.svg"
nemo_dir="$data/nemo/actions"
nautilus_dir="$data/nautilus/scripts"
dolphin_dir="$data/kio/servicemenus"          # Plasma 5.26+ (Type=Application format)
thunar_uca="$config/Thunar/uca.xml"

icon_root="$data/icons/hicolor/scalable/apps"
installed_names=(
	"$app_desktop"
	"$icon_root/repomancer.svg"
	"$icon_root/repomancer-log.svg"
	"$icon_root/repomancer-commit.svg"
	"$icon_root/repomancer-sync.svg"
	"$icon_root/repomancer-settings.svg"
	"$nemo_dir/repomancer-log.nemo_action"
	"$nemo_dir/repomancer-commit.nemo_action"
	"$nemo_dir/repomancer-sync.nemo_action"
	"$nemo_dir/repomancer-settings.nemo_action"
	"$nautilus_dir/Repomancer Log"
	"$nautilus_dir/Repomancer Commit"
	"$dolphin_dir/repomancer.desktop"
)

if [ "$UNINSTALL" = 1 ]; then
	for f in "${installed_names[@]}"; do rm -f "$f"; done
	echo "Removed Repomancer shell items. (Thunar custom actions, if added, must be"
	echo "removed via Thunar ▸ Configure custom actions.)"
	command -v update-desktop-database >/dev/null 2>&1 && \
		update-desktop-database "$data/applications" 2>/dev/null || true
	exit 0
fi

# Resolve the binary.
if [ -z "$BIN" ]; then
	BIN="$(command -v repomancer 2>/dev/null || true)"
fi
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
	echo "Could not find the repomancer binary. Pass --bin /path/to/repomancer" >&2
	exit 1
fi
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
echo "Using binary: $BIN"

# subst FILE -> stdout, replacing the binary placeholder.
subst() { sed "s|@REPOMANCER_BIN@|$BIN|g" "$1"; }

install_file() { # src dst [mode]
	mkdir -p "$(dirname "$2")"
	subst "$1" > "$2"
	[ -n "${3:-}" ] && chmod "$3" "$2" || true
	echo "  installed $2"
}

# App entry + icons. The per-verb Lucide glyphs (repomancer-log/commit/sync/
# settings) match the in-app menu icons; the menus reference them by name.
install_file "$here/repomancer.desktop" "$app_desktop"
icon_root="$data/icons/hicolor/scalable/apps"
mkdir -p "$icon_root"
for svg in "$here"/../../icons/repomancer*.svg; do
	cp "$svg" "$icon_root/$(basename "$svg")"
	echo "  installed $icon_root/$(basename "$svg")"
done

# Nemo (Cinnamon).
if command -v nemo >/dev/null 2>&1 || [ -d "$data/nemo" ]; then
	for f in "$here"/nemo/*.nemo_action; do
		install_file "$f" "$nemo_dir/$(basename "$f")"
	done
	echo "Nemo: installed (Repomancer entries appear on folders)."
fi

# Nautilus (GNOME) — scripts submenu.
if command -v nautilus >/dev/null 2>&1 || [ -d "$data/nautilus" ]; then
	install_file "$here/nautilus/Repomancer Log" "$nautilus_dir/Repomancer Log" 0755
	install_file "$here/nautilus/Repomancer Commit" "$nautilus_dir/Repomancer Commit" 0755
	echo "Nautilus: installed under right-click ▸ Scripts."
fi

# Dolphin (KDE).
if command -v dolphin >/dev/null 2>&1 || [ -d "$data/kio" ] || [ -d "$data/kservices5" ]; then
	install_file "$here/dolphin/repomancer.desktop" "$dolphin_dir/repomancer.desktop"
	echo "Dolphin: installed (a 'Repomancer' submenu on folders; Plasma 5.26+)."
fi

# Thunar (XFCE) — custom actions live in one XML the installer can't safely
# rewrite; point the user at the ready-to-paste snippet.
if command -v thunar >/dev/null 2>&1; then
	subst "$here/thunar/uca.xml.snippet" > "$config/repomancer-thunar-uca.snippet.xml"
	echo "Thunar: a ready snippet was written to"
	echo "  $config/repomancer-thunar-uca.snippet.xml"
	echo "  Merge its <action> blocks into $thunar_uca (or add via Configure custom actions)."
fi

command -v update-desktop-database >/dev/null 2>&1 && \
	update-desktop-database "$data/applications" 2>/dev/null || true
gtk-update-icon-cache -f -t "$data/icons/hicolor" 2>/dev/null || true

echo "Done. You may need to restart the file manager (e.g. 'nemo -q') to see the menu."
