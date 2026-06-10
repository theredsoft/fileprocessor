# Сборка релиза FileProcessor под Windows: бинарь + Qt-зависимости (+ инсталлятор).
#
#   .\build.ps1 -QtDir "C:\Qt\6.7.2\msvc2022_64"            # dist\ с exe и DLL
#   .\build.ps1 -QtDir "C:\Qt\6.7.2\msvc2022_64" -Installer # + setup.exe (нужен Inno Setup 6)
#
# Требования: Visual Studio 2022 (среда vcvars активна), CMake, Ninja, Qt 6.

param(
    [Parameter(Mandatory = $true)][string]$QtDir,
    [switch]$Installer
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$buildDir = "$root\build-release"
$distDir = "$PSScriptRoot\dist"

cmake -S $root -B $buildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH=$QtDir
cmake --build $buildDir
ctest --test-dir $buildDir --output-on-failure

if (Test-Path $distDir) { Remove-Item -Recurse -Force $distDir }
New-Item -ItemType Directory -Path $distDir | Out-Null
Copy-Item "$buildDir\fileprocessor.exe" $distDir

& "$QtDir\bin\windeployqt.exe" --release --no-translations `
    --no-system-d3d-compiler --no-opengl-sw "$distDir\fileprocessor.exe"

# VC-runtime в dist: иначе на машине без VS/redist приложение не стартует.
$vcCrt = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT"
) | ForEach-Object { Get-ChildItem $_ -ErrorAction SilentlyContinue } | Select-Object -First 1
if ($vcCrt) {
    Copy-Item "$($vcCrt.FullName)\*.dll" $distDir
    Write-Host "VC runtime добавлен из: $($vcCrt.FullName)"
} else {
    Write-Warning "VC143.CRT не найден — на целевых машинах потребуется vc_redist.x64.exe"
}

Write-Host "Готово: $distDir"

if ($Installer) {
    & iscc "$PSScriptRoot\fileprocessor.iss"
    Write-Host "Инсталлятор: $PSScriptRoot\output"
}
