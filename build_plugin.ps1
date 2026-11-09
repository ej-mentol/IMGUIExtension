param(
    [string]$MetaHookDir = "",
    [string]$Configuration = "",
    [string]$Generator = "",
    [switch]$SkipTests,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$projectDir = $PSScriptRoot

# Resolve MetaHook SDK directory (the only external dependency this plugin needs).
if (-not $MetaHookDir) {
    if ($env:METAHOOK_SOURCE_PATH) {
        $MetaHookDir = $env:METAHOOK_SOURCE_PATH
    } elseif (Test-Path "$projectDir\..\..\MetaHook\include\metahook.h") {
        $MetaHookDir = (Resolve-Path "$projectDir\..\..\MetaHook").Path
    } else {
        $resolved = Resolve-Path "$projectDir\..\..\MetaHook" -ErrorAction SilentlyContinue
        if (-not $resolved) {
            throw "MetaHook SDK not found. Pass -MetaHookDir <path> or set METAHOOK_SOURCE_PATH."
        }
        $MetaHookDir = $resolved.Path
    }
} else {
    $MetaHookDir = (Resolve-Path $MetaHookDir).Path
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    throw "cmake not found in PATH. Install CMake 3.21+ (or run it from a Developer PowerShell)."
}

# Reuse the surrounding aggregator's FormatValidation submodule when present so a
# build in this workspace never depends on the network.
$formatValidationDir = ""
if ($env:FORMAT_VALIDATION_SOURCE_PATH) {
    $formatValidationDir = $env:FORMAT_VALIDATION_SOURCE_PATH
} elseif (Test-Path "$projectDir\..\..\thirdparty\FormatValidation\cmake\FormatValidation.cmake") {
    $formatValidationDir = (Resolve-Path "$projectDir\..\..\thirdparty\FormatValidation").Path
}

$configs = if ($Configuration) { @($Configuration) } else { @("Debug", "Release") }

Write-Host "CMake:      $($cmake.Source)" -ForegroundColor Cyan
Write-Host "ProjectDir: $projectDir" -ForegroundColor Cyan
Write-Host "MetaHook:   $MetaHookDir" -ForegroundColor Cyan
if ($formatValidationDir) {
    Write-Host "Formatting: $formatValidationDir" -ForegroundColor Cyan
}

foreach ($config in $configs) {
    if ($config -notin @("Debug", "Release")) {
        throw "Unsupported configuration '$config'. Use Debug or Release."
    }

    $buildDir = Join-Path $projectDir "build\x86\$config"
    Write-Host "`n>>> Configuring IMGUIExtension [$config | Win32] -> $buildDir" -ForegroundColor Yellow
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }

    $configureArgs = @("-S", $projectDir, "-B", $buildDir, "-A", "Win32",
        "-DMETAHOOK_SOURCE_PATH=$MetaHookDir")
    if ($Generator) {
        $configureArgs += @("-G", $Generator)
    }
    if ($formatValidationDir) {
        $configureArgs += "-DFORMAT_VALIDATION_SOURCE_PATH=$formatValidationDir"
    }

    & $cmake.Source @configureArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Configure failed for configuration: $config"
    }

    Write-Host "`n>>> Building IMGUIExtension [$config | Win32]" -ForegroundColor Yellow
    # No --parallel: MSBuild 18 with this VS generator reports a spurious failure
    # ("Errors: 0") on up-to-date trees when CMake adds -m.
    & $cmake.Source --build $buildDir --config $config
    if ($LASTEXITCODE -ne 0) {
        throw "IMGUIExtension build failed for configuration: $config"
    }

    if (-not $SkipTests) {
        Write-Host "`n>>> Running dispatcher tests [$config]" -ForegroundColor Yellow
        $ctest = Get-Command ctest -ErrorAction SilentlyContinue
        if ($ctest) {
            & $ctest.Source --test-dir $buildDir -C $config --output-on-failure
            if ($LASTEXITCODE -ne 0) {
                throw "Tests failed for configuration: $config"
            }
        } else {
            Write-Warning "ctest not found; skipping tests."
        }
    }

    $dll = Join-Path $buildDir "$config\IMGUIExtension.dll"
    if (Test-Path $dll) {
        $item = Get-Item $dll
        Write-Host ">>> Built: $($item.FullName) ($($item.Length) bytes, $($item.LastWriteTime))" -ForegroundColor Green
        Write-Host "    Install with: cmake --install `"$buildDir`" --config $config" -ForegroundColor DarkGray
    }
}

Write-Host "`nAll requested configurations built successfully!" -ForegroundColor Green
