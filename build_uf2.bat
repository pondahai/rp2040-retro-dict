@echo off
rem Build the board firmware (plain build, links at 0x10000000).
rem
rem   build_uf2.bat [path to arduino-cli]
rem
rem The two -I flags are required: the pure-C sources live in firmware\, and
rem Arduino copies the whole sketch tree into a build directory before
rem compiling, so relative paths cannot escape it. RetroDict\src\rd_*.c are
rem one-line includes; what actually gets compiled is firmware\*.c.
rem
rem See README.md for the offset build (0x10004000, required by
rem rp2040-retro-loader) -- that is what ships.
rem
rem NOTE: keep this file ASCII. Chinese comments here get mangled by the
rem console code page and cmd tries to execute them as commands.
setlocal
set CLI=%~1
if "%CLI%"=="" set CLI=arduino-cli
set HERE=%~dp0
rem Keyboard QH wiring: set KBD_SHARED_DATA=1 in the environment for boards
rem with the 165 QH moved to GP15 (see README). Unset/0 = original GP27.
set "KBD_DEF="
if defined KBD_SHARED_DATA set "KBD_DEF=-DKBD_SHARED_DATA=%KBD_SHARED_DATA%"
rem KBD_CLK_ON_LCD_DC=1: keyboard CLOCK moved from GP26 to the LCD D/C pin (GP20).
if defined KBD_CLK_ON_LCD_DC set "KBD_DEF=%KBD_DEF% -DKBD_CLK_ON_LCD_DC=%KBD_CLK_ON_LCD_DC%"
"%CLI%" compile --fqbn rp2040:rp2040:rpipico ^
  --build-property "compiler.c.extra_flags=-I%HERE%firmware" ^
  --build-property "compiler.cpp.extra_flags=-I%HERE%firmware %KBD_DEF%" ^
  --output-dir "%HERE%build" "%HERE%RetroDict"
