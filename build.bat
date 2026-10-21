@echo off
rem Bronto Engine - Windows build script for MSVC (run from a "Developer Command Prompt").
rem   build.bat          build the tests + examples and run the tests
rem   build.bat web      build for the web: build\web\ (the web player the editor's
rem                      Build > Web uses, and a page for each example). Needs LLVM's
rem                      clang (the LLVM installer for Windows) on PATH, besides MSVC.
rem Uses the unity build (src\bronto.c), so no make/CMake is needed.
rem user32/gdi32 are linked automatically (#pragma comment in the Win32 backend).
setlocal
if /I "%1"=="web" goto :web
set CFLAGS=/nologo /W4 /O2 /Zi /fp:precise /Iinclude /D_CRT_SECURE_NO_WARNINGS /wd4100
if not exist build mkdir build
if not exist build\obj mkdir build\obj
if not exist build\test_out mkdir build\test_out

cl %CFLAGS% /Iexamples\snake /Iexamples\towerdefense /Itools\editor /Fobuild\obj\ /Fdbuild\ /Fe:build\bronto_tests.exe src\bronto.c tests\*.c examples\snake\snake_*.c examples\towerdefense\td_*.c tools\editor\doc.c tools\editor\console.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\bounce.exe src\bronto.c examples\bounce\main.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\snake.exe src\bronto.c examples\snake\*.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\towerdefense.exe src\bronto.c examples\towerdefense\*.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\editor.exe src\bronto.c tools\editor\*.c tools\web\webpage.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\bscheck.exe src\bronto.c tools\bscheck\main.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\bronto_player.exe src\bronto.c tools\player\main.c || goto :error

build\bronto_tests.exe --out build\test_out || goto :error
echo.
echo Build and tests succeeded. Play: build\snake.exe or build\towerdefense.exe; edit scenes: build\editor.exe
exit /b 0

:error
echo Build or tests FAILED.
exit /b 1

:web
rem WebAssembly with plain clang: src\platform\web is the C library, tools\web\bronto.js the browser side.
where clang >nul 2>nul || (echo build.bat web: clang isn't on PATH - install LLVM from https://llvm.org and try again. & exit /b 1)
set WCFLAGS=--target=wasm32 -std=c99 -O2 -ffp-contract=off -Iinclude -nostdlibinc -isystem src\platform\web\include -mbulk-memory -mnontrapping-fptoint -msign-ext -mexception-handling -mllvm -wasm-enable-sjlj
set WLDFLAGS=-nostdlib -Wl,--no-entry -Wl,--stack-first -Wl,-z,stack-size=4194304 -Wl,--strip-debug
if not exist build mkdir build
if not exist build\obj mkdir build\obj
if not exist build\wasm mkdir build\wasm
if not exist build\web mkdir build\web
for %%f in (src\platform\web\*.c) do clang %WCFLAGS% -fno-builtin -c %%f -o build\wasm\%%~nf.o || goto :error
rem The longjmp exception tag: see src\platform\web\tag.s.
clang --target=wasm32 -mexception-handling -c src\platform\web\tag.s -o build\wasm\tag.o || goto :error
clang %WCFLAGS% -c src\bronto.c -o build\wasm\bronto.o || goto :error
set WRT=build\wasm\libc.o build\wasm\malloc.o build\wasm\math.o build\wasm\memfs.o build\wasm\sjlj.o build\wasm\start.o build\wasm\tag.o build\wasm\stdio.o build\wasm\bronto.o
clang %WCFLAGS% %WLDFLAGS% tools\player\main.c %WRT% -o build\web\bronto_player.wasm || goto :error
copy /Y tools\web\bronto.js build\web\ >nul
copy /Y tools\web\shell.html build\web\ >nul
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\bronto_web.exe src\bronto.c tools\web\main.c tools\web\webpage.c || goto :error
call :webgame bounce "Bounce" || goto :error
call :webgame snake "Bronto Snake" || goto :error
call :webgame towerdefense "Pop Defense" || goto :error
build\bronto_web.exe examples\platformer --runtime build\web -o build\web\platformer\index.html || goto :error
echo.
echo Web build done: open build\web\snake\index.html (or any example's index.html) in a browser.
echo The editor's Build ^> Web uses build\web\ (bronto_player.wasm, bronto.js, shell.html).
exit /b 0

rem A C example as a page: :webgame <folder> <title>
:webgame
setlocal enabledelayedexpansion
set SRCS=
for %%f in (examples\%1\*.c) do set SRCS=!SRCS! %%f
clang %WCFLAGS% %WLDFLAGS% !SRCS! %WRT% -o build\web\%1.wasm || exit /b 1
build\bronto_web.exe --wasm build\web\%1.wasm --title %2 --runtime build\web -o build\web\%1\index.html || exit /b 1
endlocal
exit /b 0
