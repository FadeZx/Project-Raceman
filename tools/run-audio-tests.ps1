# Builds and runs the standalone behaviour tests: audio, engine and the
# vehicle setup preset tables.
#
# These deliberately do not go through the .vcxproj: the units under test are
# pure (no OpenGL, no audio device, no ImGui), so compiling them directly keeps
# the loop to a couple of seconds instead of a multi-minute editor build.
#
#   powershell -ExecutionPolicy Bypass -File tools\run-audio-tests.ps1

$ErrorActionPreference = "Stop"

$repoRoot  = Split-Path -Parent $PSScriptRoot
$srcRoot   = Join-Path $repoRoot "ProjectRaceman\src"
$testRoot  = Join-Path $repoRoot "ProjectRaceman\tests"
$outDir    = Join-Path $repoRoot "ProjectRaceman\bin-int\tests"

New-Item -ItemType Directory -Force $outDir | Out-Null

# Pull the MSVC environment into this session so cl.exe can find its headers.
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at $vcvars - adjust the path for your Visual Studio install."
}
cmd /c "`"$vcvars`" > nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue
    }
}

# name -> (test source, dependency sources, include dirs)
$suites = @(
    @{
        Name     = "engine_state"
        Source   = Join-Path $testRoot "test_engine_state.cpp"
        Deps     = @(Join-Path $srcRoot "physics\VehicleEngineState.cpp")
        Includes = @(Join-Path $srcRoot "physics")
        Args     = @()
    },
    @{
        Name     = "engine_synth"
        Source   = Join-Path $testRoot "test_engine_synth.cpp"
        Deps     = @(
            (Join-Path $srcRoot "audio\EngineSynth.cpp"),
            (Join-Path $srcRoot "audio\EngineSoundProfile.cpp"),
            (Join-Path $srcRoot "audio\EngineSynthGenerator.cpp"),
            (Join-Path $srcRoot "physics\VehicleConfig.cpp")
        )
        Includes = @((Join-Path $srcRoot "audio"), (Join-Path $srcRoot "physics"))
        # Renders listenable WAVs next to the test binaries.
        Args     = @($outDir)
    },
    @{
        Name     = "vehicle_setup_presets"
        Source   = Join-Path $testRoot "test_vehicle_setup_presets.cpp"
        Deps     = @(Join-Path $srcRoot "physics\VehicleConfig.cpp")
        Includes = @(Join-Path $srcRoot "physics")
        Args     = @()
    },
    @{
        Name     = "tyre_synth"
        Source   = Join-Path $testRoot "test_tyre_synth.cpp"
        Deps     = @(
            (Join-Path $srcRoot "audio\TyreSynth.cpp"),
            (Join-Path $srcRoot "audio\TyreSoundProfile.cpp"),
            (Join-Path $srcRoot "audio\EngineSynthGenerator.cpp"),
            (Join-Path $srcRoot "physics\VehicleConfig.cpp")
        )
        Includes = @((Join-Path $srcRoot "audio"), (Join-Path $srcRoot "physics"))
        Args     = @($outDir)
    }
)

$failed = 0
foreach ($suite in $suites) {
    $exe = Join-Path $outDir "test_$($suite.Name).exe"
    # Delete first. Otherwise a compile failure leaves the previous binary in
    # place, the runner happily executes it, and stale code reports ALL PASS.
    if (Test-Path $exe) { Remove-Item -Force $exe }
    Write-Host "=== building $($suite.Name) ===" -ForegroundColor Cyan

    $clArgs = @("/nologo", "/EHsc", "/std:c++17", "/MD", "/W3")
    foreach ($inc in $suite.Includes) { $clArgs += "/I$inc" }
    $clArgs += "/Fe:$exe"
    $clArgs += "/Fo:$outDir\"
    $clArgs += $suite.Source
    $clArgs += $suite.Deps

    Push-Location $outDir
    $output = & cl.exe @clArgs 2>&1
    Pop-Location

    if (-not (Test-Path $exe)) {
        Write-Host ($output -join "`n") -ForegroundColor Red
        Write-Host "BUILD FAILED: $($suite.Name)" -ForegroundColor Red
        $failed++
        continue
    }

    Write-Host "=== running $($suite.Name) ===" -ForegroundColor Cyan
    if ($suite.ContainsKey('Args') -and $suite.Args.Count -gt 0) { & $exe @($suite.Args) } else { & $exe }
    if ($LASTEXITCODE -ne 0) { $failed++ }
}

Write-Host ""
if ($failed -eq 0) {
    Write-Host "All suites passed." -ForegroundColor Green
    exit 0
}
Write-Host "$failed suite(s) failed." -ForegroundColor Red
exit 1
