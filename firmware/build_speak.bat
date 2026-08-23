@echo off
rem 在 PC 上編譯發音測試程式。需要 Visual Studio 2022 Community。
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /O2 /utf-8 /D_USE_MATH_DEFINES /Fe:%~dp0test_speak.exe %~dp0test_speak.c %~dp0speech.c %~dp0synth.c %~dp0lts.c %~dp0dict.c /Fo:%~dp0
