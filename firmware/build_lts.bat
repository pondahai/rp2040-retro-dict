@echo off
rem 在 PC 上編譯 letter-to-sound 測試程式。需要 Visual Studio 2022 Community。
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /O2 /utf-8 /Fe:%~dp0test_lts.exe %~dp0test_lts.c %~dp0lts.c /Fo:%~dp0
