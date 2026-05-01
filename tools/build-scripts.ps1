param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$projectRoot = Join-Path $repoRoot "ProjectRaceman"
$scriptProjectPath = Join-Path $projectRoot "ProjectScripts.vcxproj"

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "MSBuild was not found. Install Visual Studio Build Tools or open a Developer PowerShell where msbuild.exe is on PATH."
}

if (-not (Test-Path $scriptProjectPath)) {
    throw "Script project not found: $scriptProjectPath"
}

$msbuild = Find-MSBuild
Write-Host "Building ProjectScripts $Configuration|$Platform..."
& $msbuild $scriptProjectPath /m "/p:Configuration=$Configuration" "/p:Platform=$Platform"
if ($LASTEXITCODE -ne 0) {
    throw "ProjectScripts build failed with exit code $LASTEXITCODE."
}
