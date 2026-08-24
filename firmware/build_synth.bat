@echo off
rem Builds the synthesizer test program (per-segment and whole-string) on the PC. Needs Visual Studio 2022 Community.
rem
rem NOTE: keep this file pure ASCII. cmd.exe parses .bat with the system code
rem page (cp950 here), so Chinese in a rem line gets mangled and cmd tries to
rem execute the fragments as commands. Same rule as build_uf2.bat /
rem build_offset.bat in the repo root. The Chinese write-up lives in
rem firmware/README.md.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /O2 /utf-8 /D_USE_MATH_DEFINES /Fe:%~dp0test_synth.exe %~dp0test_synth.c %~dp0synth.c %~dp0speech.c %~dp0lts.c %~dp0dict.c /Fo:%~dp0
