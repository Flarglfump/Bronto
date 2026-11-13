<#
Installs the Bronto editor on Windows, and sets BRONTO_EDITOR for scripts.
"make install" (MinGW) and "build.bat install" (MSVC) run it.

  tools\install_editor_windows.ps1              build the tools if they're out of date, then install
  tools\install_editor_windows.ps1 -NoBuild     install what's already in build\
  tools\install_editor_windows.ps1 -AllUsers    install for everyone (needs an administrator PowerShell)
  tools\install_editor_windows.ps1 -Uninstall   remove it (with -AllUsers for an install for everyone)

Where it goes, as Windows programs installed without an installer usually do:

  %LOCALAPPDATA%\Programs\Bronto Editor\   the editor (editor.exe, and editor.com for command lines),
                                           bronto_player, bscheck, players\, web\, and sdk\: the
                                           engine's headers and sources, for games' C code
                                           (kept together: the editor finds them beside it)
                                           (-AllUsers: %ProgramFiles%\Bronto Editor\)
  Start menu > Bronto Editor               a shortcut, with the editor's icon
  Settings > Apps > Installed apps         an entry, so it can be uninstalled from there too
  BRONTO_EDITOR                            editor.com's path, a user environment variable
                                           (-AllUsers: a system one): it waits for jobs like
                                           --build and shows their output

In PowerShell: & $env:BRONTO_EDITOR --build windows my_game
In cmd:        "%BRONTO_EDITOR%" --build windows my_game
Programs started after the install see BRONTO_EDITOR; ones already open (this terminal) don't.
Nothing is copied if the installed files are already the same as the ones in build\.
#>
[CmdletBinding()]
param(
    [switch]$NoBuild,
    [switch]$AllUsers,
    [switch]$Uninstall,
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'
$AppName = 'Bronto Editor'
$UninstallKeyName = 'BrontoEditor'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $Root 'build' }
$BuildDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)   # (a full path)

if ($AllUsers) {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Error "-AllUsers needs an administrator PowerShell (right-click PowerShell > Run as administrator)."
    }
    $InstallDir = Join-Path $env:ProgramFiles $AppName
    $StartMenu = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'
    $Scope = 'Machine'
    $UninstallRoot = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
} else {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\$AppName"
    $StartMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
    $Scope = 'User'
    $UninstallRoot = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
}
$Shortcut = Join-Path $StartMenu "$AppName.lnk"
$UninstallKey = Join-Path $UninstallRoot $UninstallKeyName
$Exe = Join-Path $InstallDir 'editor.exe'               # the windowed editor (the shortcut)
$Cli = Join-Path $InstallDir 'editor.com'               # ... and the one for command lines (BRONTO_EDITOR)

# ---- uninstall ------------------------------------------------------------------------

if ($Uninstall) {
    # (Settings > Apps runs the copy of this script in the install folder: move out of it first.)
    Set-Location $env:TEMP
    if (Test-Path $Shortcut) { Remove-Item $Shortcut -Force }
    if (Test-Path $UninstallKey) { Remove-Item $UninstallKey -Recurse -Force }
    if (@($Exe, $Cli) -contains [Environment]::GetEnvironmentVariable('BRONTO_EDITOR', $Scope)) {
        [Environment]::SetEnvironmentVariable('BRONTO_EDITOR', $null, $Scope)
    }
    if (Test-Path $InstallDir) {
        try { Remove-Item $InstallDir -Recurse -Force }
        catch { Write-Error "Couldn't remove $InstallDir - is the editor still open? Close it and try again." }
        Write-Host "Removed $InstallDir"
    } else {
        Write-Host "The Bronto editor isn't installed in $InstallDir"
    }
    exit 0
}

# ---- build, if anything changed since the tools were built ---------------------------------

$Tools = 'editor.exe', 'editor.com', 'bronto_player.exe', 'bscheck.exe'

function Test-ToolsOutOfDate {
    $built = foreach ($t in $Tools) {
        $p = Join-Path $BuildDir $t
        if (-not (Test-Path $p)) { return $true }
        (Get-Item $p).LastWriteTimeUtc
    }
    $oldest = ($built | Measure-Object -Minimum).Minimum
    $dirs = 'src', 'include', 'tools\editor', 'tools\player', 'tools\bscheck', 'tools\web', 'tools\launcher' | ForEach-Object { Join-Path $Root $_ }
    $newer = Get-ChildItem $dirs -Recurse -File -Include *.c, *.h, *.s -ErrorAction SilentlyContinue |
             Where-Object { $_.LastWriteTimeUtc -gt $oldest } | Select-Object -First 1
    return [bool]$newer
}

if (-not $NoBuild -and (Test-ToolsOutOfDate)) {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        Write-Host 'Building the editor, bscheck and bronto_player (build.bat tools)...'
        Push-Location $Root                                 # (build.bat works from the repository's folder)
        try { & cmd.exe /c "build.bat tools" } finally { Pop-Location }
    } elseif ((Get-Command gcc.exe -ErrorAction SilentlyContinue) -and
              ($make = Get-Command make.exe, mingw32-make.exe -ErrorAction SilentlyContinue | Select-Object -First 1)) {
        Write-Host "Building the editor, bscheck and bronto_player ($($make.Name) tools)..."
        & $make.Source -C $Root tools
    } else {
        Write-Error ("No C compiler found to build the editor with. Run this from a Developer PowerShell for " +
                     "Visual Studio (for MSVC's cl), or with MinGW's gcc and make on PATH; or build it " +
                     "first and use -NoBuild.")
    }
    if ($LASTEXITCODE -ne 0) { Write-Error 'The build failed.' }
}
foreach ($t in $Tools) {
    if (-not (Test-Path (Join-Path $BuildDir $t))) {
        Write-Error "No $t in $BuildDir - build it with ""build.bat tools"" (MSVC) or ""make tools"" (MinGW)."
    }
}

# ---- what goes in --------------------------------------------------------------------------
# Relative path in the install folder -> file in build\.

$files = [ordered]@{}
foreach ($t in $Tools) { $files[$t] = Join-Path $BuildDir $t }
$players = Join-Path $BuildDir 'players'
if (Test-Path $players) {                                   # other systems' players
    Get-ChildItem $players -Recurse -File | ForEach-Object {
        $files['players\' + $_.FullName.Substring($players.Length + 1)] = $_.FullName
    }
}
$webFound = $false
foreach ($webDir in (Join-Path $BuildDir 'web'), (Join-Path $BuildDir 'players\web')) {   # the web player (build.bat web)
    $web = 'bronto_player.wasm', 'bronto.js', 'shell.html' | ForEach-Object { Join-Path $webDir $_ }
    if (@($web | Where-Object { Test-Path $_ }).Count -eq 3) {
        foreach ($w in $web) { $files['web\' + (Split-Path -Leaf $w)] = $w }
        $webFound = $true
        break
    }
}
if (-not $webFound) { Write-Host "note: no web player in build\ (build.bat web), so the editor's Build > Web won't be available" }
# The engine's functions in the editor, for the games' C code it compiles and loads: an import
# library (MSVC's editor.lib, MinGW's libeditor.a).
$importLib = $false
foreach ($lib in 'editor.lib', 'libeditor.a') {
    $p = Join-Path $BuildDir $lib
    if (Test-Path $p) { $files[$lib] = $p; $importLib = $true }
}
if (-not $importLib) { Write-Host "note: no editor.lib in build\, so the editor can't load games' C code (only build it into games)" }
# The engine's headers and sources (sdk\), which the editor compiles with games' C code.
foreach ($part in 'include', 'src') {
    $from = Join-Path $Root $part
    Get-ChildItem $from -Recurse -File | ForEach-Object {
        $files["sdk\$part\" + $_.FullName.Substring($from.Length + 1)] = $_.FullName
    }
}
$files['sdk\tools\player\main.c'] = Join-Path $Root 'tools\player\main.c'
$files['sdk\tools\web\bronto.js'] = Join-Path $Root 'tools\web\bronto.js'
$files['sdk\tools\web\shell.html'] = Join-Path $Root 'tools\web\shell.html'
$files['install_editor_windows.ps1'] = $PSCommandPath      # (Settings > Apps runs it to uninstall)

$manifest = ($files.Keys | Sort-Object | ForEach-Object {
    "$((Get-FileHash $files[$_] -Algorithm SHA256).Hash) $_"
}) -join "`r`n"
$manifestPath = Join-Path $InstallDir 'manifest.txt'

# ---- install -------------------------------------------------------------------------------

$upToDate = (Test-Path $manifestPath) -and ((Get-Content $manifestPath -Raw).Trim() -eq $manifest) -and
            (Test-Path $Shortcut) -and (Test-Path $UninstallKey)
if ($upToDate) {
    Write-Host "The Bronto editor in $InstallDir is already up to date."
} else {
    if (Test-Path $InstallDir) {
        try { Remove-Item $InstallDir -Recurse -Force }
        catch { Write-Error "Couldn't replace $InstallDir - is the editor still open? Close it and try again." }
    }
    foreach ($rel in $files.Keys) {
        $dest = Join-Path $InstallDir $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
        Copy-Item $files[$rel] $dest
    }
    Set-Content -Path $manifestPath -Value $manifest -Encoding ASCII

    # The icon: the editor draws its own (--write-icon) as a PNG, and an .ico can hold a
    # PNG as it is: a 6-byte header and one 16-byte entry in front of it.
    $icon = Join-Path $InstallDir 'editor.ico'
    $png = Join-Path $env:TEMP "bronto-editor-icon-$PID.png"
    & $Cli --write-icon $png 256 | Out-Null                 # (editor.com: it waits for it)
    if ($LASTEXITCODE -eq 0 -and (Test-Path $png)) {
        $bytes = [IO.File]::ReadAllBytes($png)
        $stream = [IO.File]::Create($icon)
        $w = New-Object IO.BinaryWriter($stream)
        $w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]1)      # an icon, one image
        $w.Write([byte]0); $w.Write([byte]0); $w.Write([byte]0); $w.Write([byte]0)   # 256 x 256 (0 means 256)
        $w.Write([UInt16]1); $w.Write([UInt16]32)                          # planes, bits per pixel
        $w.Write([UInt32]$bytes.Length); $w.Write([UInt32]22)              # its size, where it starts
        $w.Write($bytes)
        $w.Close()
        Remove-Item $png -Force
    } else {
        $icon = $null
        Write-Host "note: couldn't save the editor's icon, so the shortcut has the default one"
    }

    New-Item -ItemType Directory -Force -Path $StartMenu | Out-Null
    $shell = New-Object -ComObject WScript.Shell
    $lnk = $shell.CreateShortcut($Shortcut)
    $lnk.TargetPath = $Exe
    $lnk.WorkingDirectory = $InstallDir
    $lnk.Description = 'Make games with the Bronto engine'
    if ($icon) { $lnk.IconLocation = "$icon,0" }
    $lnk.Save()

    $size = [int](($files.Values | ForEach-Object { (Get-Item $_).Length } | Measure-Object -Sum).Sum / 1KB)
    New-Item -Path $UninstallKey -Force | Out-Null
    $flag = if ($AllUsers) { ' -AllUsers' } else { '' }
    $entry = @{
        DisplayName     = $AppName
        DisplayVersion  = '0.1'
        Publisher       = 'Bronto'
        InstallLocation = $InstallDir
        UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$InstallDir\install_editor_windows.ps1`" -Uninstall$flag"
        EstimatedSize   = $size
        NoModify        = 1
        NoRepair        = 1
    }
    if ($icon) { $entry.DisplayIcon = $icon }
    foreach ($k in $entry.Keys) {
        $type = if ($entry[$k] -is [int]) { 'DWord' } else { 'String' }
        New-ItemProperty -Path $UninstallKey -Name $k -Value $entry[$k] -PropertyType $type -Force | Out-Null
    }
    Write-Host "Installed the Bronto editor in $InstallDir"
}

# ---- BRONTO_EDITOR ---------------------------------------------------------------------------
# (SetEnvironmentVariable tells Windows, so programs started from now on see it.)

if ([Environment]::GetEnvironmentVariable('BRONTO_EDITOR', $Scope) -ne $Cli) {
    [Environment]::SetEnvironmentVariable('BRONTO_EDITOR', $Cli, $Scope)
    Write-Host "Set BRONTO_EDITOR ($($Scope.ToLower()) environment variable) to $Cli"
}
if ($env:BRONTO_EDITOR -ne $Cli) {
    $env:BRONTO_EDITOR = $Cli          # (this PowerShell has it too, if the script was run from it directly)
    Write-Host 'Terminals that were already open when it was set need reopening to see BRONTO_EDITOR.'
}
