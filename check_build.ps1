$cl = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\HostX86\x64\CL.exe"
$inc1 = "d:\Project-Raceman\ProjectRaceman\..\includes"
$inc2 = "d:\Project-Raceman\ProjectRaceman\editor-assets\third_party"
$outDir = "d:\Project-Raceman\ProjectRaceman\bin-int\Debug"

$files = @(
    "d:\Project-Raceman\ProjectRaceman\src\ui\SceneEditorPersistence.cpp",
    "d:\Project-Raceman\ProjectRaceman\src\ui\SceneEditorProject.cpp"
)

foreach ($src in $files) {
    Write-Host "Compiling $src..."
    $result = & $cl /c /nologo /EHsc /std:c++17 /MDd /D WIN32 /D _DEBUG /D _MBCS /Zc:wchar_t /Zc:forScope /Zc:inline /permissive- /W3 /FS "/I$inc1" "/I$inc2" "/Fo$outDir\" "/FdNUL" $src 2>&1
    $result | Where-Object { $_ -match "error|warning" } | Write-Host
}
Write-Host "Done."
