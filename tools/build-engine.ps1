param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$projectRoot = Join-Path $repoRoot "ProjectRaceman"
$solutionPath = Join-Path $projectRoot "Project Raceman.sln"
$binDir = Join-Path $projectRoot "bin\$Configuration"
$outputRoot = [System.IO.Path]::GetFullPath($OutputPath)

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

function Copy-RequiredFile {
    param(
        [string[]]$Candidates,
        [string]$DestinationDirectory,
        [switch]$Optional
    )

    foreach ($candidate in $Candidates) {
        if (Test-Path $candidate) {
            New-Item -ItemType Directory -Force -Path $DestinationDirectory | Out-Null
            Copy-Item -Force -LiteralPath $candidate -Destination $DestinationDirectory
            return $true
        }
    }

    if ($Optional) {
        Write-Warning "Optional file not found: $($Candidates -join ', ')"
        return $false
    }

    throw "Required file not found: $($Candidates -join ', ')"
}

function Copy-DirectoryClean {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path $Source)) {
        throw "Required directory not found: $Source"
    }

    $sourceFull = [System.IO.Path]::GetFullPath($Source)
    $destinationFull = [System.IO.Path]::GetFullPath($Destination)
    if ($sourceFull.TrimEnd('\') -ieq $destinationFull.TrimEnd('\')) {
        throw "Refusing to overwrite source directory: $sourceFull"
    }

    if (Test-Path $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Destination $_.Name) -Recurse -Force
    }
}

if (-not (Test-Path $solutionPath)) {
    throw "Solution not found: $solutionPath"
}

$msbuild = Find-MSBuild
Write-Host "Building Project Raceman editor $Configuration|$Platform..."
& $msbuild $solutionPath /m "/p:Configuration=$Configuration" "/p:Platform=$Platform"
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

if (Test-Path $outputRoot) {
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$null = Copy-RequiredFile -Candidates @(Join-Path $binDir "ProjectRaceman.exe") -DestinationDirectory $outputRoot
$null = Copy-RequiredFile -Candidates @(Join-Path $binDir "ProjectScripts.dll") -DestinationDirectory $outputRoot -Optional
$null = Copy-RequiredFile -Candidates @(Join-Path $binDir "ProjectRaceman.lib") -DestinationDirectory (Join-Path $outputRoot "bin\$Configuration")

$dllCandidates = @{
    "assimp-vc143-mt.dll" = @(
        (Join-Path $binDir "assimp-vc143-mt.dll"),
        (Join-Path $projectRoot "assimp-vc143-mt.dll"),
        (Join-Path $repoRoot "dlls\assimp-vc143-mt.dll")
    )
    "irrKlang.dll" = @(
        (Join-Path $binDir "irrKlang.dll"),
        (Join-Path $projectRoot "irrKlang.dll"),
        (Join-Path $repoRoot "dlls\irrKlang.dll")
    )
    "ikpMP3.dll" = @(
        (Join-Path $binDir "ikpMP3.dll"),
        (Join-Path $projectRoot "ikpMP3.dll"),
        (Join-Path $repoRoot "dlls\ikpMP3.dll")
    )
}

foreach ($name in $dllCandidates.Keys) {
    $null = Copy-RequiredFile -Candidates $dllCandidates[$name] -DestinationDirectory $outputRoot
}

Copy-DirectoryClean -Source (Join-Path $projectRoot "src\shaders") -Destination (Join-Path $outputRoot "src\shaders")
Copy-DirectoryClean -Source (Join-Path $projectRoot "src\scripting") -Destination (Join-Path $outputRoot "src\scripting")
Copy-DirectoryClean -Source (Join-Path $projectRoot "editor-assets") -Destination (Join-Path $outputRoot "editor-assets")
Copy-DirectoryClean -Source (Join-Path $projectRoot "templates") -Destination (Join-Path $outputRoot "templates")

$toolsDest = Join-Path $outputRoot "tools"
New-Item -ItemType Directory -Force -Path $toolsDest | Out-Null
Copy-Item -Force -LiteralPath (Join-Path $PSScriptRoot "build-game.ps1") -Destination $toolsDest
Copy-Item -Force -LiteralPath (Join-Path $PSScriptRoot "build-scripts.ps1") -Destination $toolsDest

Copy-DirectoryClean -Source (Join-Path $repoRoot "includes") -Destination (Join-Path $outputRoot "includes")

$configDest = Join-Path $outputRoot "config"
New-Item -ItemType Directory -Force -Path $configDest | Out-Null
if (Test-Path (Join-Path $projectRoot "config\vehicles")) {
    Copy-DirectoryClean -Source (Join-Path $projectRoot "config\vehicles") -Destination (Join-Path $configDest "vehicles")
}
if (Test-Path (Join-Path $projectRoot "config\scenes")) {
    Copy-DirectoryClean -Source (Join-Path $projectRoot "config\scenes") -Destination (Join-Path $configDest "scenes")
}

$buildInfo = @(
    "Project Raceman engine build",
    "Configuration=$Configuration",
    "Platform=$Platform",
    "Source=$projectRoot",
    "BuiltAt=$((Get-Date).ToString('o'))"
)
Set-Content -LiteralPath (Join-Path $outputRoot "engine-build-info.txt") -Value $buildInfo

Write-Host "Engine build created at $outputRoot"
