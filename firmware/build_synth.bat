@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W3 /O2 /D_USE_MATH_DEFINES /Fe:%~dp0test_synth.exe %~dp0test_synth.c %~dp0synth.c /Fo:%~dp0
