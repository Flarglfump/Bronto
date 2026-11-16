#!/bin/bash
# Installs "Open in Bronto Editor", a Finder Quick Action (right-click > Quick Actions),
# which opens the selected project in the Bronto Editor app with
#
#   open -a 'Bronto Editor' <path>
#
#   tools/install_quick_action_macos.sh              install (or update) it
#   tools/install_quick_action_macos.sh --uninstall  remove it
#
# What it opens, for the first selected item:
#   a folder                  that folder, as a project
#   a .scene file             that scene, in its project
#   project.bronto            its project folder
#   any other file            the nearest folder above it with a project.bronto
#                             (or, if there is none, the file's own folder)
#
# The app is the one "make install" (tools/install_editor_macos.sh) puts in
# /Applications or ~/Applications. The Quick Action is a workflow in ~/Library/Services;
# its shell script is open_in_bronto.sh inside it, to read or change.

set -euo pipefail

NAME="Open in Bronto Editor"
SERVICES="$HOME/Library/Services"
WORKFLOW="$SERVICES/$NAME.workflow"

uninstall=0
for arg in "$@"; do
    case "$arg" in
        --uninstall) uninstall=1 ;;
        -h|--help)   sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install_quick_action_macos: unknown option $arg (try --help)" >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" != "Darwin" ]; then
    echo "install_quick_action_macos: this script is for macOS" >&2
    exit 1
fi

refresh_services() {        # so Finder's menus pick up the change now, not after a log-out
    [ -x /System/Library/CoreServices/pbs ] && /System/Library/CoreServices/pbs -flush >/dev/null 2>&1 || true
}

if [ "$uninstall" = 1 ]; then
    if [ -d "$WORKFLOW" ]; then
        rm -rf "$WORKFLOW"
        refresh_services
        echo "Removed $WORKFLOW"
    else
        echo "\"$NAME\" isn't installed."
    fi
    exit 0
fi

if ! open -Ra "Bronto Editor" 2>/dev/null; then
    echo "note: the Bronto Editor app isn't installed yet - run \"make install\" first, or the Quick Action will say so"
fi

# ---- the workflow bundle -------------------------------------------------------------
# Made in a temporary folder and moved into place, so a failed install leaves the old one.

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
NEW="$STAGE/$NAME.workflow"
mkdir -p "$NEW/Contents"

# The script Finder runs, with the selected items as arguments.
cat > "$NEW/Contents/open_in_bronto.sh" <<'SCRIPT'
#!/bin/bash
# "Open in Bronto Editor" (a Finder Quick Action, from Bronto's
# tools/install_quick_action_macos.sh): opens the first selected item's project in the
# Bronto Editor app.
app="Bronto Editor"

# The project to open for one selected path.
target_for() {
    local f="$1" d
    if [ -d "$f" ]; then echo "$f"; return; fi
    case "$f" in
        *.scene|*/project.bronto) echo "$f"; return ;;
    esac
    d="$(dirname "$f")"                            # some other file: its project
    while [ -n "$d" ] && [ "$d" != "/" ]; do
        if [ -f "$d/project.bronto" ]; then echo "$d"; return; fi
        d="$(dirname "$d")"
    done
    dirname "$f"
}

alert() {                                          # (the path goes in as an argument, not as code)
    /usr/bin/osascript - "$1" "$2" <<'AS' >/dev/null 2>&1
on run argv
    display alert (item 1 of argv) message (item 2 of argv) as warning
end run
AS
}

[ $# -gt 0 ] || exit 0
target="$(target_for "$1")"                         # (one project at a time: the first item)
if ! /usr/bin/open -a "$app" "$target" 2>/dev/null; then
    alert "Couldn't open $(basename "$target") in $app" \
          "Is the $app app installed? Run \"make install\" in your Bronto folder to install it."
    exit 1
fi
SCRIPT
chmod +x "$NEW/Contents/open_in_bronto.sh"

# The Quick Action itself: Finder, any file or folder, no output.
cat > "$NEW/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>NSServices</key>
  <array>
    <dict>
      <key>NSBackgroundColorName</key><string>background</string>
      <key>NSIconName</key><string>NSActionTemplate</string>
      <key>NSMenuItem</key>
      <dict><key>default</key><string>$NAME</string></dict>
      <key>NSMessage</key><string>runWorkflowAsService</string>
      <key>NSRequiredContext</key>
      <dict><key>NSApplicationIdentifier</key><string>com.apple.finder</string></dict>
      <key>NSSendFileTypes</key>
      <array><string>public.item</string></array>
    </dict>
  </array>
</dict>
</plist>
EOF

# Automator's document: one "Run Shell Script" action, input as arguments, which runs
# the script above from wherever the workflow is.
cat > "$NEW/Contents/document.wflow" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>AMApplicationBuild</key><string>521</string>
  <key>AMApplicationVersion</key><string>2.10</string>
  <key>AMDocumentVersion</key><string>2</string>
  <key>actions</key>
  <array>
    <dict>
      <key>action</key>
      <dict>
        <key>AMAccepts</key>
        <dict>
          <key>Container</key><string>List</string>
          <key>Optional</key><true/>
          <key>Types</key><array><string>com.apple.cocoa.string</string></array>
        </dict>
        <key>AMActionVersion</key><string>2.0.3</string>
        <key>AMApplication</key><array><string>Automator</string></array>
        <key>AMParameterProperties</key>
        <dict>
          <key>COMMAND_STRING</key><dict/>
          <key>CheckedForUserDefaultShell</key><dict/>
          <key>inputMethod</key><dict/>
          <key>shell</key><dict/>
          <key>source</key><dict/>
        </dict>
        <key>AMProvides</key>
        <dict>
          <key>Container</key><string>List</string>
          <key>Types</key><array><string>com.apple.cocoa.string</string></array>
        </dict>
        <key>ActionBundlePath</key><string>/System/Library/Automator/Run Shell Script.action</string>
        <key>ActionName</key><string>Run Shell Script</string>
        <key>ActionParameters</key>
        <dict>
          <key>COMMAND_STRING</key>
          <string>"$HOME/Library/Services/Open in Bronto Editor.workflow/Contents/open_in_bronto.sh" "$@"</string>
          <key>CheckedForUserDefaultShell</key><true/>
          <key>inputMethod</key><integer>1</integer>
          <key>shell</key><string>/bin/bash</string>
          <key>source</key><string></string>
        </dict>
        <key>BundleIdentifier</key><string>com.apple.RunShellScript</string>
        <key>CFBundleVersion</key><string>2.0.3</string>
        <key>CanShowSelectedItemsWhenRun</key><false/>
        <key>CanShowWhenRun</key><true/>
        <key>Category</key><array><string>AMCategoryUtilities</string></array>
        <key>Class Name</key><string>RunShellScriptAction</string>
        <key>InputUUID</key><string>6F2A9C1E-3B4D-4E5F-8A7B-1C2D3E4F5A60</string>
        <key>Keywords</key><array><string>Shell</string><string>Script</string><string>Command</string><string>Run</string><string>Unix</string></array>
        <key>OutputUUID</key><string>7A3B0D2F-4C5E-4F60-9B8C-2D3E4F5A6B71</string>
        <key>UUID</key><string>8B4C1E30-5D6F-4071-AC9D-3E4F5A6B7C82</string>
        <key>UnlocalizedApplications</key><array><string>Automator</string></array>
        <key>arguments</key>
        <dict>
          <key>0</key>
          <dict>
            <key>default value</key><integer>0</integer>
            <key>name</key><string>inputMethod</string>
            <key>required</key><string>0</string>
            <key>type</key><string>0</string>
            <key>uuid</key><string>0</string>
          </dict>
          <key>1</key>
          <dict>
            <key>default value</key><false/>
            <key>name</key><string>CheckedForUserDefaultShell</string>
            <key>required</key><string>0</string>
            <key>type</key><string>0</string>
            <key>uuid</key><string>1</string>
          </dict>
          <key>2</key>
          <dict>
            <key>default value</key><string></string>
            <key>name</key><string>source</string>
            <key>required</key><string>0</string>
            <key>type</key><string>0</string>
            <key>uuid</key><string>2</string>
          </dict>
          <key>3</key>
          <dict>
            <key>default value</key><string></string>
            <key>name</key><string>COMMAND_STRING</string>
            <key>required</key><string>0</string>
            <key>type</key><string>0</string>
            <key>uuid</key><string>3</string>
          </dict>
          <key>4</key>
          <dict>
            <key>default value</key><string>/bin/sh</string>
            <key>name</key><string>shell</string>
            <key>required</key><string>0</string>
            <key>type</key><string>0</string>
            <key>uuid</key><string>4</string>
          </dict>
        </dict>
        <key>isViewVisible</key><integer>1</integer>
        <key>location</key><string>309.000000:253.000000</string>
        <key>nibPath</key><string>/System/Library/Automator/Run Shell Script.action/Contents/Resources/Base.lproj/main.nib</string>
      </dict>
      <key>isViewVisible</key><integer>1</integer>
    </dict>
  </array>
  <key>connectors</key><dict/>
  <key>workflowMetaData</key>
  <dict>
    <key>applicationBundleID</key><string>com.apple.finder</string>
    <key>applicationBundleIDsByPath</key>
    <dict>
      <key>/System/Library/CoreServices/Finder.app</key><string>com.apple.finder</string>
    </dict>
    <key>applicationPath</key><string>/System/Library/CoreServices/Finder.app</string>
    <key>applicationPaths</key>
    <array><string>/System/Library/CoreServices/Finder.app</string></array>
    <key>inputTypeIdentifier</key><string>com.apple.Automator.fileSystemObject</string>
    <key>outputTypeIdentifier</key><string>com.apple.Automator.nothing</string>
    <key>presentationMode</key><integer>15</integer>
    <key>processesInput</key><false/>
    <key>serviceApplicationBundleID</key><string>com.apple.finder</string>
    <key>serviceApplicationPath</key><string>/System/Library/CoreServices/Finder.app</string>
    <key>serviceInputTypeIdentifier</key><string>com.apple.Automator.fileSystemObject</string>
    <key>serviceOutputTypeIdentifier</key><string>com.apple.Automator.nothing</string>
    <key>serviceProcessesInput</key><false/>
    <key>systemImageName</key><string>NSActionTemplate</string>
    <key>useAutomaticInputType</key><false/>
    <key>workflowTypeIdentifier</key><string>com.apple.Automator.servicesMenu</string>
  </dict>
</dict>
</plist>
EOF

if command -v plutil >/dev/null; then                 # (both files have to be valid property lists)
    plutil -lint -s "$NEW/Contents/Info.plist" "$NEW/Contents/document.wflow"
fi

mkdir -p "$SERVICES"
rm -rf "$WORKFLOW"
mv "$NEW" "$WORKFLOW"
refresh_services
echo "Installed \"$NAME\": right-click a project folder (or a scene, or any file in a project)"
echo "in Finder and choose Quick Actions > $NAME."
echo "(If it isn't listed there yet, look under Quick Actions > Customize..., or log out and back in.)"
