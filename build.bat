@echo off
rem Bronto Engine - Windows build script for MSVC (run from a "Developer Command Prompt").
rem   build.bat          build the tests + examples and run the tests
rem Uses the unity build (src\bronto.c), so no make/CMake is needed.
rem user32/gdi32 are linked automatically (#pragma comment in the Win32 backend).
setlocal
set CFLAGS=/nologo /W4 /O2 /Zi /fp:precise /Iinclude /D_CRT_SECURE_NO_WARNINGS /wd4100
if not exist build mkdir build
if not exist build\obj mkdir build\obj
if not exist build\test_out mkdir build\test_out

cl %CFLAGS% /Iexamples\snake /Iexamples\towerdefense /Itools\editor /Fobuild\obj\ /Fdbuild\ /Fe:build\bronto_tests.exe src\bronto.c tests\*.c examples\snake\snake_*.c examples\towerdefense\td_*.c tools\editor\doc.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\bounce.exe src\bronto.c examples\bounce\main.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\snake.exe src\bronto.c examples\snake\*.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\towerdefense.exe src\bronto.c examples\towerdefense\*.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\editor.exe src\bronto.c tools\editor\*.c || goto :error
cl %CFLAGS% /Fobuild\obj\ /Fdbuild\ /Fe:build\bscheck.exe src\bronto.c tools\bscheck\main.c || goto :error

build\bronto_tests.exe --out build\test_out || goto :error
echo.
echo Build and tests succeeded. Play: build\snake.exe or build\towerdefense.exe; edit scenes: build\editor.exe
exit /b 0

:error
echo Build or tests FAILED.
exit /b 1
