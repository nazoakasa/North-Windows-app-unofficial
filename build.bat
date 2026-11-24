@echo off
rem MSVC (x64 Native Tools Command Prompt) 用  ※未検証
cd /d "%~dp0src"
rc /nologo north.rc
cl /nologo /utf-8 /O2 /W3 /DUNICODE /D_UNICODE /Fe:..\bin\north.exe main.c ui.c gfx.c net.c util.c login.c stats.c cJSON.c north.res ^
   user32.lib gdi32.lib ole32.lib oleaut32.lib winhttp.lib crypt32.lib shell32.lib shlwapi.lib msimg32.lib gdiplus.lib comdlg32.lib advapi32.lib ^
   /link /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1
echo OK -^> bin\north.exe
