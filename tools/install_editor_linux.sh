#!/bin/sh
# Installs the Bronto editor on Linux and the BSDs, the usual way for programs built from
# source, and sets BRONTO_EDITOR for shell scripts. "make install" runs it.
#
#   tools/install_editor_linux.sh                  install (after "make tools")
#   tools/install_editor_linux.sh --uninstall      remove what it installed
#   PREFIX=/opt/bronto tools/install_editor_linux.sh   install somewhere else
#   DESTDIR=pkg tools/install_editor_linux.sh      stage into pkg/ (for packaging)
#
# Where it goes (PREFIX): /usr/local when run as root (sudo make install) or when
# /usr/local can be written to, otherwise ~/.local, the per-user prefix. Under PREFIX:
#
#   lib/bronto/                          the editor, bronto_player, bscheck, players/, web/
#                                        (kept together: the editor finds the players beside it)
#   bin/bronto-editor, bin/bscheck, bin/bronto_player   links to them
#   share/applications/bronto-editor.desktop          the app menu entry
#   share/icons/hicolor/<size>/apps/bronto-editor.png  its icon
#
# BRONTO_EDITOR (bin/bronto-editor) goes in /etc/profile.d/bronto-editor.sh for a system-wide
# install, or in ~/.profile for a per-user one. Nothing is copied if the installed files
# are already the same as the ones in build/.

set -eu

NAME="bronto-editor"
MARK_BEGIN="# >>> Bronto editor >>>"
MARK_END="# <<< Bronto editor <<<"

uninstall=0
for arg in "$@"; do
    case "$arg" in
        --uninstall) uninstall=1 ;;
        -h|--help)   sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install_editor_linux: unknown option $arg (try --help)" >&2; exit 2 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
DESTDIR="${DESTDIR:-}"
is_root=0
[ "$(id -u)" = 0 ] && is_root=1

if [ -n "${PREFIX:-}" ]; then
    prefix_given=1
elif [ "$is_root" = 1 ] || [ -n "$DESTDIR" ] || { [ -w /usr/local/bin ] && [ -w /usr/local/share ]; }; then
    prefix_given=0; PREFIX=/usr/local
else
    prefix_given=0; PREFIX="$HOME/.local"
fi
PREFIX="${PREFIX%/}"
system_wide=0
case "$PREFIX" in "$HOME"|"$HOME"/*) ;; *) system_wide=1 ;; esac

# ---- BRONTO_EDITOR -------------------------------------------------------------------

PROFILE_D="/etc/profile.d/$NAME.sh"
USER_PROFILE="$HOME/.profile"

remove_block() {                                    # the lines between the markers
    [ -f "$1" ] && grep -qF "$MARK_BEGIN" "$1" || return 0
    tmp="$(mktemp)"
    awk -v b="$MARK_BEGIN" -v e="$MARK_END" '$0 == b { s = 1; next } $0 == e { s = 0; next } !s' "$1" > "$tmp"
    cat "$tmp" > "$1"
    rm -f "$tmp"
}

set_variable() {
    line="export BRONTO_EDITOR=\"$1\""
    if [ "$system_wide" = 1 ]; then
        if [ "$is_root" = 1 ] && [ -d /etc/profile.d ]; then
            if [ "$(cat "$PROFILE_D" 2>/dev/null)" != "$line" ]; then
                echo "$line" > "$PROFILE_D"
                echo "Set BRONTO_EDITOR in $PROFILE_D"
            fi
            return
        fi
        echo "note: run as root (sudo) to set BRONTO_EDITOR for everyone; setting it for you instead"
    fi
    if ! grep -qxF "$line" "$USER_PROFILE" 2>/dev/null; then
        remove_block "$USER_PROFILE"
        printf '%s\n%s\n%s\n%s\n' "$MARK_BEGIN" \
            "# Set by Bronto's tools/install_editor_linux.sh (make uninstall removes it)." \
            "$line" "$MARK_END" >> "$USER_PROFILE"
        echo "Set BRONTO_EDITOR in $USER_PROFILE"
    fi
}

refresh_caches() {                                  # the menu and icon caches under prefix $1, if any
    [ -z "$DESTDIR" ] || return 0
    command -v update-desktop-database >/dev/null 2>&1 &&
        update-desktop-database -q "$1/share/applications" 2>/dev/null || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -f "$1/share/icons/hicolor/index.theme" ] &&
        gtk-update-icon-cache -q -t -f "$1/share/icons/hicolor" 2>/dev/null || true
}

ICON_SIZES="48 128 256 512"

# ---- uninstall ---------------------------------------------------------------------------

uninstall_from() {
    p="$1"; d="$DESTDIR$p"
    [ -d "$d/lib/bronto" ] || [ -f "$d/share/applications/$NAME.desktop" ] || return 1
    for link in "$NAME" bscheck bronto_player; do    # only our links, not someone else's bscheck
        case "$(readlink "$d/bin/$link" 2>/dev/null)" in */lib/bronto/*) rm -f "$d/bin/$link" ;; esac
    done
    rm -rf "$d/lib/bronto"
    rm -f "$d/share/applications/$NAME.desktop"
    for s in $ICON_SIZES; do rm -f "$d/share/icons/hicolor/${s}x${s}/apps/$NAME.png"; done
    refresh_caches "$p"
    echo "Removed the Bronto editor from $d"
}

if [ "$uninstall" = 1 ]; then
    found=0
    if [ "$prefix_given" = 1 ]; then
        uninstall_from "$PREFIX" && found=1
    else
        for p in /usr/local "$HOME/.local"; do uninstall_from "$p" && found=1; done
    fi
    [ "$found" = 1 ] || echo "The Bronto editor isn't installed in ${PREFIX}"
    if [ -z "$DESTDIR" ]; then
        if [ "$is_root" = 1 ] && [ -f "$PROFILE_D" ]; then rm -f "$PROFILE_D"; echo "Removed $PROFILE_D"; fi
        if [ -f "$USER_PROFILE" ] && grep -qF "$MARK_BEGIN" "$USER_PROFILE"; then
            remove_block "$USER_PROFILE"; echo "Removed BRONTO_EDITOR from $USER_PROFILE"
        fi
    fi
    exit 0
fi

# ---- install -----------------------------------------------------------------------------

for tool in editor bronto_player bscheck; do
    if [ ! -x "$BUILD/$tool" ]; then
        echo "install_editor_linux: no $tool in $BUILD - run \"make tools\" first" >&2
        exit 1
    fi
done

D="$DESTDIR$PREFIX"
LIB="$D/lib/bronto"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT INT TERM

# Everything that goes in lib/bronto, gathered first so it can be compared with what's
# installed.
mkdir -p "$STAGE/lib"
cp "$BUILD/editor" "$BUILD/bronto_player" "$BUILD/bscheck" "$STAGE/lib/"
[ -d "$BUILD/players" ] && cp -R "$BUILD/players" "$STAGE/lib/players"
for webdir in "$BUILD/web" "$BUILD/players/web"; do  # the web player (make web)
    if [ -f "$webdir/bronto_player.wasm" ] && [ -f "$webdir/bronto.js" ] && [ -f "$webdir/shell.html" ]; then
        mkdir -p "$STAGE/lib/web"
        cp "$webdir/bronto_player.wasm" "$webdir/bronto.js" "$webdir/shell.html" "$STAGE/lib/web/"
        break
    fi
done
[ -d "$STAGE/lib/web" ] || echo "note: no web player in build/ (make web), so the editor's Build > Web won't be available"
(cd "$STAGE/lib" && find . -type f ! -name manifest | LC_ALL=C sort | xargs cksum) > "$STAGE/lib/manifest"

if [ -f "$LIB/manifest" ] && cmp -s "$STAGE/lib/manifest" "$LIB/manifest" &&
   [ -f "$D/share/applications/$NAME.desktop" ] && [ -L "$D/bin/$NAME" ]; then
    echo "The Bronto editor in $D is already up to date."
else
    mkdir -p "$D/lib" "$D/bin" "$D/share/applications"
    rm -rf "$LIB"
    cp -R "$STAGE/lib" "$LIB"
    ln -sf ../lib/bronto/editor "$D/bin/$NAME"
    ln -sf ../lib/bronto/bronto_player "$D/bin/bronto_player"
    ln -sf ../lib/bronto/bscheck "$D/bin/bscheck"

    for s in $ICON_SIZES; do                         # the editor draws its own icon
        mkdir -p "$D/share/icons/hicolor/${s}x${s}/apps"
        "$BUILD/editor" --write-icon "$D/share/icons/hicolor/${s}x${s}/apps/$NAME.png" "$s" >/dev/null 2>&1 ||
            echo "note: couldn't save the ${s}px icon"
    done

    cat > "$D/share/applications/$NAME.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Bronto Editor
GenericName=Game Editor
Comment=Make 2D and 3D games with the Bronto engine
Exec="$PREFIX/bin/$NAME" %f
Icon=$NAME
Terminal=false
Categories=Development;IDE;Game;
EOF
    refresh_caches "$PREFIX"
    echo "Installed the Bronto editor in $D"
fi

if [ -z "$DESTDIR" ]; then
    set_variable "$PREFIX/bin/$NAME"
    case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "note: $PREFIX/bin isn't on your PATH, so run it as \$BRONTO_EDITOR" ;; esac
    if [ "${BRONTO_EDITOR:-}" != "$PREFIX/bin/$NAME" ]; then
        if [ "$system_wide" = 1 ] && [ "$is_root" = 1 ]; then from="$PROFILE_D"; else from="$USER_PROFILE"; fi
        echo "BRONTO_EDITOR=$PREFIX/bin/$NAME (log out and back in, or run: . $from, to use it now)"
    fi
fi
