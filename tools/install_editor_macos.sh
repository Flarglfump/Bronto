#!/bin/bash
# Installs the Bronto editor as an app on macOS, and sets BRONTO_EDITOR for shell scripts.
#
#   tools/install_editor_macos.sh              build (make tools) and install
#   tools/install_editor_macos.sh --no-build   install what's already in build/
#   tools/install_editor_macos.sh --user       install into ~/Applications instead
#   tools/install_editor_macos.sh --uninstall  remove the app and the BRONTO_EDITOR lines
#
# The app is "Bronto Editor.app" in /Applications (or ~/Applications if /Applications
# can't be written to, or with --user). Inside it, Contents/MacOS/ holds the editor and
# everything the editor looks for beside itself: bronto_player and bscheck, players/
# (other systems' players) and web/ (the web player from "make web"), so the Build menu
# works from the app just as it does from build/.
#
# The files are copied, so run this again after rebuilding the editor.
#
# BRONTO_EDITOR is the editor program inside the app, set in ~/.zshrc (and ~/.bash_profile
# if you have one), so shell scripts can do:
#
#   "$BRONTO_EDITOR" --build macos path/to/project
#   "$BRONTO_EDITOR" path/to/project

set -euo pipefail

APP_NAME="Bronto Editor"
BUNDLE_ID="com.bronto.editor"
MARK_BEGIN="# >>> Bronto editor >>>"
MARK_END="# <<< Bronto editor <<<"

build=1
user_only=0
uninstall=0
for arg in "$@"; do
    case "$arg" in
        --no-build)  build=0 ;;
        --user)      user_only=1 ;;
        --uninstall) uninstall=1 ;;
        -h|--help)   sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install_editor_macos: unknown option $arg (try --help)" >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" != "Darwin" ]; then
    echo "install_editor_macos: this script is for macOS" >&2
    exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"

# The shell startup files BRONTO_EDITOR goes in: zsh's (macOS's default shell) always,
# and bash's if it's there or bash is the login shell.
rc_files() {
    echo "$HOME/.zshrc"
    if [ -f "$HOME/.bash_profile" ] || [ "$(basename "${SHELL:-}")" = "bash" ]; then
        echo "$HOME/.bash_profile"
    fi
}

# Removes the block this script wrote (between the markers) from a file.
remove_block() {
    local file="$1"
    [ -f "$file" ] || return 0
    if grep -qF "$MARK_BEGIN" "$file"; then
        local tmp
        tmp="$(mktemp)"
        awk -v b="$MARK_BEGIN" -v e="$MARK_END" '
            $0 == b { skip = 1; next }
            $0 == e { skip = 0; next }
            !skip' "$file" > "$tmp"
        cat "$tmp" > "$file"          # (keeps the file's permissions, and a symlinked rc file)
        rm -f "$tmp"
    fi
}

if [ "$uninstall" = 1 ]; then
    for dir in "/Applications" "$HOME/Applications"; do
        if [ -d "$dir/$APP_NAME.app" ]; then
            rm -rf "$dir/$APP_NAME.app"
            echo "Removed $dir/$APP_NAME.app"
        fi
    done
    while read -r rc; do remove_block "$rc"; done < <(rc_files)
    echo "Removed BRONTO_EDITOR from your shell startup files (open a new terminal for it to go)."
    exit 0
fi

# ---- build ---------------------------------------------------------------------------

if [ "$build" = 1 ]; then
    echo "Building the editor, bscheck and bronto_player (make tools)..."
    make -C "$ROOT" tools
fi
if [ ! -x "$BUILD/editor" ]; then
    echo "install_editor_macos: no editor in $BUILD - run \"make tools\" first" >&2
    exit 1
fi

# ---- where it goes ---------------------------------------------------------------------

if [ "$user_only" = 0 ] && [ -w "/Applications" ]; then
    APPS="/Applications"
else
    APPS="$HOME/Applications"
    [ "$user_only" = 1 ] || echo "Can't write to /Applications, so installing into ~/Applications."
    mkdir -p "$APPS"
fi
APP="$APPS/$APP_NAME.app"
EXE="$APP/Contents/MacOS/editor"

# ---- the bundle ------------------------------------------------------------------------
# Made in a temporary folder and then moved into place, so a failed install leaves the
# old app as it was.

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
NEW="$STAGE/$APP_NAME.app"
MACOS="$NEW/Contents/MacOS"
RES="$NEW/Contents/Resources"
mkdir -p "$MACOS" "$RES"

cp "$BUILD/editor" "$MACOS/editor"
for tool in bronto_player bscheck; do
    if [ -x "$BUILD/$tool" ]; then
        cp "$BUILD/$tool" "$MACOS/$tool"
    else
        echo "note: no $tool in build/ (make tools), so the Build menu can't build for this Mac"
    fi
done
if [ -d "$BUILD/players" ]; then                        # other systems' players
    cp -R "$BUILD/players" "$MACOS/players"
fi
web_found=0
for webdir in "$BUILD/web" "$BUILD/players/web"; do    # the web player (make web)
    if [ -f "$webdir/bronto_player.wasm" ] && [ -f "$webdir/bronto.js" ] && [ -f "$webdir/shell.html" ]; then
        mkdir -p "$MACOS/web"
        cp "$webdir/bronto_player.wasm" "$webdir/bronto.js" "$webdir/shell.html" "$MACOS/web/"
        web_found=1
        break
    fi
done
[ "$web_found" = 1 ] || echo "note: no web player in build/ (make web), so Build > Web won't be available"

# The icon: the editor draws its own (--write-icon), and sips and iconutil (part of
# macOS) turn it into an .icns. If any of that fails, the app just has no Finder icon.
icon_key=""
if command -v sips >/dev/null && command -v iconutil >/dev/null &&
   "$BUILD/editor" --write-icon "$STAGE/icon.png" 1024 >/dev/null 2>&1; then
    ICONSET="$STAGE/AppIcon.iconset"
    mkdir -p "$ICONSET"
    for size in 16 32 128 256 512; do
        sips -z $size $size "$STAGE/icon.png" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
        double=$((size * 2))
        sips -z $double $double "$STAGE/icon.png" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
    done
    if iconutil -c icns "$ICONSET" -o "$RES/AppIcon.icns" 2>/dev/null; then
        icon_key="  <key>CFBundleIconFile</key><string>AppIcon</string>"
    fi
fi

cat > "$NEW/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>editor</string>
  <key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
  <key>CFBundleName</key><string>$APP_NAME</string>
  <key>CFBundleDisplayName</key><string>$APP_NAME</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>CFBundleVersion</key><string>1</string>
$icon_key
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

# An ad-hoc signature for the whole bundle, so Apple Silicon Macs run it without
# complaint (it isn't a developer signature; it just seals the bundle as built here).
if command -v codesign >/dev/null; then
    codesign --force --deep --sign - "$NEW" >/dev/null 2>&1 || echo "note: couldn't sign the app (it should still run)"
fi

rm -rf "$APP"
mv "$NEW" "$APP"
touch "$APP"                                            # (so Finder and the Dock pick up the new icon)
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
[ -x "$LSREGISTER" ] && "$LSREGISTER" -f "$APP" >/dev/null 2>&1 || true
echo "Installed $APP"

# ---- BRONTO_EDITOR -------------------------------------------------------------------

while read -r rc; do
    remove_block "$rc"
    {
        echo "$MARK_BEGIN"
        echo "# Set by Bronto's tools/install_editor_macos.sh (run it with --uninstall to remove)."
        echo "export BRONTO_EDITOR=\"$EXE\""
        echo "$MARK_END"
    } >> "$rc"
    echo "Set BRONTO_EDITOR in $rc"
done < <(rc_files)

echo
echo "BRONTO_EDITOR=$EXE"
echo "Open a new terminal (or run: source ~/.zshrc) to use it in this one."
