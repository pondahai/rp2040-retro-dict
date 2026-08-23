@echo off
rem 在 PC 上編譯前景（鍵盤 + 狀態機 + UI）測試程式。需要 Visual Studio 2022 Community。
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /O2 /utf-8 /D_USE_MATH_DEFINES /Fe:%~dp0test_app.exe %~dp0test_app.c %~dp0app.c %~dp0keys.c %~dp0dict.c %~dp0font.c %~dp0ui.c %~dp0fbuf.c %~dp0speech.c %~dp0synth.c %~dp0lts.c %~dp0ime.c /Fo:%~dp0
