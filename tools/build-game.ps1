param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [string]$ProjectPath = "",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$buildRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$sourceProjectRoot = Join-Path $buildRoot "ProjectRaceman"
$solutionPath = Join-Path $sourceProjectRoot "Project Raceman.sln"
$sourceMode = Test-Path $solutionPath
$engineRoot = if ($sourceMode) { $sourceProjectRoot } else { $buildRoot }
$binDir = if ($sourceMode) { Join-Path $sourceProjectRoot "bin\$Configuration" } else { $engineRoot }
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
        [string]$Destination,
        [string[]]$SkipNames = @()
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
        if ($SkipNames -contains $_.Name) {
            return
        }
        $target = Join-Path $Destination $_.Name
        Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse -Force
    }
}

if ($sourceMode) {
    $msbuild = Find-MSBuild
    Write-Host "Building Project Raceman $Configuration|$Platform..."
    & $msbuild $solutionPath /m "/p:Configuration=$Configuration" "/p:Platform=$Platform"
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Host "Using packaged engine from $engineRoot"
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$null = Copy-RequiredFile -Candidates @(Join-Path $binDir "ProjectRaceman.exe") -DestinationDirectory $outputRoot
$null = Copy-RequiredFile -Candidates @(Join-Path $binDir "ProjectScripts.dll") -DestinationDirectory $outputRoot -Optional

$dllCandidates = @{
    "assimp-vc143-mt.dll" = @(
        (Join-Path $binDir "assimp-vc143-mt.dll"),
        (Join-Path $engineRoot "assimp-vc143-mt.dll"),
        (Join-Path $buildRoot "dlls\assimp-vc143-mt.dll")
    )
    "irrKlang.dll" = @(
        (Join-Path $binDir "irrKlang.dll"),
        (Join-Path $engineRoot "irrKlang.dll"),
        (Join-Path $buildRoot "dlls\irrKlang.dll")
    )
    "ikpMP3.dll" = @(
        (Join-Path $binDir "ikpMP3.dll"),
        (Join-Path $engineRoot "ikpMP3.dll"),
        (Join-Path $buildRoot "dlls\ikpMP3.dll")
    )
}

foreach ($name in $dllCandidates.Keys) {
    $null = Copy-RequiredFile -Candidates $dllCandidates[$name] -DestinationDirectory $outputRoot
}

$gameProjectDir = $null

if (-not [string]::IsNullOrWhiteSpace($ProjectPath)) {
    $candidate = [System.IO.Path]::GetFullPath($ProjectPath)
    if (-not (Test-Path $candidate)) {
        throw "Project path does not exist: $candidate"
    }
    if (-not (Test-Path (Join-Path $candidate "project.raceman.json"))) {
        throw "Project path is not a Raceman project: $candidate"
    }
    $gameProjectDir = $candidate
}

# Fallback discovery is kept for command-line use only. Editor builds pass -ProjectPath.
if (-not $gameProjectDir) {
    Get-ChildItem -LiteralPath $engineRoot -Directory | ForEach-Object {
        if ((-not $gameProjectDir) -and (Test-Path (Join-Path $_.FullName "project.raceman.json"))) {
            $gameProjectDir = $_.FullName
        }
    }
}
if (-not $gameProjectDir) {
    Get-ChildItem -LiteralPath $buildRoot -Directory | Where-Object { $_.Name -ne "ProjectRaceman" } | ForEach-Object {
        if ((-not $gameProjectDir) -and (Test-Path (Join-Path $_.FullName "project.raceman.json"))) {
            $gameProjectDir = $_.FullName
        }
    }
}
if (-not $gameProjectDir) {
    throw "No game project found. Pass -ProjectPath or place project.raceman.json in a sub-folder of $buildRoot"
}
$projectFolderName = Split-Path -Leaf $gameProjectDir
$exeSrc  = Join-Path $outputRoot "ProjectRaceman.exe"
$exeDest = Join-Path $outputRoot "$projectFolderName.exe"
if (Test-Path $exeSrc) {
    if (Test-Path $exeDest) { Remove-Item -LiteralPath $exeDest -Force }
    Rename-Item -LiteralPath $exeSrc -NewName "$projectFolderName.exe"
    Write-Host "Renamed executable to $projectFolderName.exe"
}

Write-Host "Bundling project: $gameProjectDir"
# Use robocopy to copy the project directory so that file modification times are
# preserved. The physics collision cache is keyed on mesh file mtimes; if we used
# Copy-Item the timestamps would change and every first run would re-cook colliders.
$projDest = Join-Path $outputRoot "Project"
if (Test-Path $projDest) {
    Remove-Item -LiteralPath $projDest -Recurse -Force
}
robocopy $gameProjectDir $projDest /E /COPY:DAT /NFL /NDL /NJH /NJS
if ($LASTEXITCODE -ge 8) {
    throw "robocopy failed copying project directory (exit code $LASTEXITCODE)."
}
Copy-DirectoryClean -Source (Join-Path $engineRoot "src\shaders") -Destination (Join-Path $outputRoot "src\shaders")

Set-Content -LiteralPath (Join-Path $outputRoot "player.raceman") -Value "player=1"
# Drop a debug.raceman file so the in-game debug console is active by default.
# Delete this file from the output folder to ship a clean release build.
Set-Content -LiteralPath (Join-Path $outputRoot "debug.raceman") -Value "debug=1"

$buildInfo = @(
    "Project Raceman standalone build",
    "ProjectName=$projectFolderName",
    "Configuration=$Configuration",
    "Platform=$Platform",
    "Source=$engineRoot",
    "BuiltAt=$((Get-Date).ToString('o'))"
)
Set-Content -LiteralPath (Join-Path $outputRoot "build-info.txt") -Value $buildInfo

Write-Host "Standalone build created at $outputRoot"
