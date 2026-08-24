@echo off
rem Builds the UI layout test program on the PC. Needs Visual Studio 2022 Community.
rem
rem NOTE: keep this file pure ASCII. cmd.exe parses .bat with the system code
rem page (cp950 here), so Chinese in a rem line gets mangled and cmd tries to
rem execute the fragments as commands. Same rule as build_uf2.bat /
rem build_offset.bat in the repo root. The Chinese write-up lives in
rem firmware/README.md.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /O2 /utf-8 /Fe:%~dp0test_ui.exe %~dp0test_ui.c %~dp0dict.c %~dp0font.c %~dp0ui.c %~dp0fbuf.c /Fo:%~dp0
