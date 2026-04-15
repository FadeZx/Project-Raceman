@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
cl /c /nologo /EHsc /std:c++17 /MDd /D WIN32 /D _DEBUG /D _MBCS /Zc:wchar_t /Zc:forScope /Zc:inline /permissive- /W3 /FS /Id:\Project-Raceman\ProjectRaceman\..\includes /Id:\Project-Raceman\ProjectRaceman\editor-assets\third_party "/Fod:\Project-Raceman\ProjectRaceman\bin-int\Debug\\" /FdNUL d:\Project-Raceman\ProjectRaceman\src\ui\SceneEditorHierarchy.cpp
echo Exit code: %ERRORLEVEL%
