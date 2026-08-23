@echo off
rem 在 PC 上編譯注音引擎測試程式。需要 Visual Studio 2022 Community。
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /O2 /utf-8 /Fe:%~dp0test_ime.exe %~dp0test_ime.c %~dp0ime.c /Fo:%~dp0
