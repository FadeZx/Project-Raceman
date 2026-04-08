param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [switch]$AttachDebugger
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$projectRoot = Join-Path $repoRoot "ProjectRaceman"
$solutionPath = Join-Path $projectRoot "Project Raceman.sln"
$scriptsPath = Join-Path $projectRoot "Project\assets\scripts"
$exePath = Join-Path $projectRoot "bin\$Configuration\ProjectRaceman.exe"

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

function Find-VisualStudioJitDebugger {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath "Common7\IDE\vsjitdebugger.exe"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $command = Get-Command vsjitdebugger.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return $null
}

function Stop-App {
    param([System.Diagnostics.Process]$Process)

    if ($null -eq $Process -or $Process.HasExited) {
        return
    }

    Write-Host "Stopping running ProjectRaceman..."
    $Process.CloseMainWindow() | Out-Null
    if (-not $Process.WaitForExit(3000)) {
        $Process.Kill()
        $Process.WaitForExit()
    }
}

function Build-App {
    param([string]$MSBuildPath)

    Write-Host "Building $Configuration|$Platform..."
    & $MSBuildPath $solutionPath /m "/p:Configuration=$Configuration" "/p:Platform=$Platform"
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }
}

function Start-App {
    if (-not (Test-Path $exePath)) {
        throw "Built executable not found: $exePath"
    }

    Write-Host "Starting ProjectRaceman..."
    return Start-Process -FilePath $exePath -WorkingDirectory $projectRoot -PassThru
}

function Attach-DebuggerToExistingVisualStudio {
    param([System.Diagnostics.Process]$Process)

    $dteProgIds = @("VisualStudio.DTE.17.0", "VisualStudio.DTE.16.0")
    foreach ($progId in $dteProgIds) {
        try {
            $dte = [System.Runtime.InteropServices.Marshal]::GetActiveObject($progId)
            foreach ($localProcess in $dte.Debugger.LocalProcesses) {
                if ($localProcess.ProcessID -eq $Process.Id) {
                    Write-Host "Attaching existing Visual Studio debugger to ProjectRaceman pid $($Process.Id)..."
                    $localProcess.Attach()
                    return $true
                }
            }
        } catch {
            continue
        }
    }

    return $false
}

function Attach-Debugger {
    param(
        [System.Diagnostics.Process]$Process,
        [string]$DebuggerPath
    )

    if ($null -eq $Process -or $Process.HasExited) {
        Write-Warning "Cannot attach debugger because ProjectRaceman is not running."
        return
    }

    if (Attach-DebuggerToExistingVisualStudio -Process $Process) {
        return
    }

    if ([string]::IsNullOrWhiteSpace($DebuggerPath) -or -not (Test-Path $DebuggerPath)) {
        Write-Warning "Visual Studio debugger was not found. Attach manually to process id $($Process.Id)."
        return
    }

    try {
        Write-Host "Existing Visual Studio debugger was not available; invoking JIT debugger for pid $($Process.Id)..."
        Start-Process -FilePath $DebuggerPath -ArgumentList @("-p", $Process.Id) | Out-Null
    } catch {
        Write-Warning "Failed to attach debugger to process id $($Process.Id): $($_.Exception.Message)"
    }
}

function Get-ScriptSnapshot {
    if (-not (Test-Path $scriptsPath)) {
        return ""
    }

    $files = Get-ChildItem -Path $scriptsPath -File | Where-Object { $_.Extension -eq ".cpp" -or $_.Extension -eq ".h" }
    return ($files | Sort-Object FullName | ForEach-Object {
        "$($_.FullName)|$($_.LastWriteTimeUtc.Ticks)|$($_.Length)"
    }) -join "`n"
}

if (-not (Test-Path $solutionPath)) {
    throw "Solution not found: $solutionPath"
}

if (-not (Test-Path $scriptsPath)) {
    New-Item -ItemType Directory -Path $scriptsPath | Out-Null
}

$msbuild = Find-MSBuild
$debugger = $null
if ($AttachDebugger) {
    $debugger = Find-VisualStudioJitDebugger
    if ([string]::IsNullOrWhiteSpace($debugger)) {
        Write-Warning "Visual Studio debugger was not found. The app will still rebuild and restart without auto attach."
    }
}
$app = $null
$pending = $true
$debounceMs = 1500
$lastChangeUtc = [DateTime]::UtcNow
$lastSnapshot = Get-ScriptSnapshot

Write-Host "Watching $scriptsPath for C++ script changes. Press Ctrl+C to stop."
if ($AttachDebugger) {
    Write-Host "Debugger auto-attach is enabled."
}

try {
    while ($true) {
        $currentSnapshot = Get-ScriptSnapshot
        if ($currentSnapshot -ne $lastSnapshot) {
            $lastSnapshot = $currentSnapshot
            $lastChangeUtc = [DateTime]::UtcNow
            $pending = $true
            Write-Host "Detected script change."
        }

        if ($pending -and ([DateTime]::UtcNow - $lastChangeUtc).TotalMilliseconds -ge $debounceMs) {
            $pending = $false
            try {
                Stop-App $app
                Build-App $msbuild
                $app = Start-App
                if ($AttachDebugger) {
                    Attach-Debugger -Process $app -DebuggerPath $debugger
                }
            } catch {
                Write-Warning $_.Exception.Message
                Write-Host "Fix the compile error and save the script again to retry."
            }
        }

        Start-Sleep -Milliseconds 200
    }
} finally {
    Stop-App $app
}
