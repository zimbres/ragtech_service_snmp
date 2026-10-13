
[CmdletBinding()]
param(
    [switch]$Test
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir    = Join-Path $ProjectRoot 'build-windows'
$PackageDir  = Join-Path $ProjectRoot 'dist'
$Testing     = if ($Test) { 'ON' } else { 'OFF' }

# Locate CMake tools
$CMakeBin = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'

$CMake = Join-Path $CMakeBin 'cmake.exe'
$CTest = Join-Path $CMakeBin 'ctest.exe'
$CPack = Join-Path $CMakeBin 'cpack.exe'

foreach ($Tool in @($CMake, $CTest, $CPack)) {
    if (-not (Test-Path $Tool)) {
        throw "Required tool not found: $Tool"
    }
}

# Visual Studio 2026 = version 18
$Generator = 'Visual Studio 18 2026'

Push-Location $ProjectRoot

try {
    # Configure
    & $CMake `
        -S $ProjectRoot `
        -B $BuildDir `
        -G $Generator `
        -A x64 `
        "-DBUILD_TESTING=$Testing"

    if ($LASTEXITCODE -ne 0) {
        throw 'CMake configuration failed.'
    }

    # Build
    & $CMake `
        --build $BuildDir `
        --config Release `
        --parallel

    if ($LASTEXITCODE -ne 0) {
        throw 'Build failed.'
    }

    # Run tests (optional)
    if ($Test) {
        & $CTest `
            --test-dir $BuildDir `
            -C Release `
            --output-on-failure

        if ($LASTEXITCODE -ne 0) {
            throw 'Tests failed.'
        }
    }

    # Ensure package directory exists
    New-Item -ItemType Directory `
        -Path $PackageDir `
        -Force | Out-Null

    # Package
    & $CPack `
        --config (Join-Path $BuildDir 'CPackConfig.cmake') `
        -G ZIP `
        -C Release `
        -B $PackageDir

    if ($LASTEXITCODE -ne 0) {
        throw 'ZIP packaging failed.'
    }

    Write-Host ""
    Write-Host "Build completed successfully!" -ForegroundColor Green
    Write-Host "Executable: $BuildDir\Release\ragtech_service.exe"
    Write-Host "ZIP package: $PackageDir"
}
finally {
    Pop-Location
}
