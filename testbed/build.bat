@REM Windows Build Script for Testbed
@ECHO OFF
SetLocal EnableDelayedExpansion

REM Get .c files
Set cFilenames=
FOR /R %%f in (*.c) do (
    SET cFilenames=!cFilenames! "%%f"
)

REM echo "Files:" %cFilenames%

SET assembly=testbed
SET compilerFlags=-g
REM -Wall -Werror
SET includeFlags=-Isrc -I../engine/src/
SET linkerFlags=-L../bin/ -lengine.lib
SET defines=-D_DEBUG -DB_IMPORT

ECHO "Building %assembly%%..."
clang %cFilenames% %compilerFlags% -o ../bin/%assembly%.exe %defines% %includeFlags% %linkerFlags%