#!/usr/bin/env pwsh
# Builds MkPCApp end-to-end: native app (CMake/MSVC) + sensor bridge (.NET publish).
# Run from the repo root: ./scripts/build.ps1

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

Write-Host "==> Initializing git submodules (imgui, implot)..."
git submodule update --init --recursive

$BuildDir = Join-Path $RepoRoot "build"
Write-Host "==> Configuring CMake ($BuildDir)..."
cmake -S $RepoRoot -B $BuildDir -DCMAKE_BUILD_TYPE=Release

Write-Host "==> Building (native app + sensor bridge publish)..."
cmake --build $BuildDir --config Release

Write-Host "==> Done. Run: $BuildDir\bin\MkPCApp.exe"
