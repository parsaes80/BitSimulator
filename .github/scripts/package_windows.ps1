# Packages a Release build into <OutDir>\<Name>.zip: the app, Qt (windeployqt), the MSVC runtime, the tools and the licenses
# Usage: package_windows.ps1 -BuildDir build -QtDir C:\Qt\6.12.0\msvc2022_64 -OutDir dist [-Name BitSimulator_Windows_x64]
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $QtDir,
    [Parameter(Mandatory)] [string] $OutDir,
    [string] $Name = "BitSimulator_Windows_x64"
)
$ErrorActionPreference = "Stop"

# Runs a program and fails on its exit code only: a warning on stderr is not an error (Windows PowerShell 5.1 would stop on it)
function Invoke-Tool([string] $Program, [string[]] $Arguments) {
    $ErrorActionPreference = "Continue"
    & $Program @Arguments 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$appDir = Join-Path $BuildDir "BitSimulator"
# Visual Studio builds put the exe in Release\, Ninja builds next to the tools
$exe = @("$appDir\Release\BitSimulator.exe", "$appDir\BitSimulator.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw "No BitSimulator.exe in $appDir or $appDir\Release" }

$stage = Join-Path $OutDir $Name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $exe $stage
foreach ($item in "oss-cad-suite", "ghdl", "licenses", "sv2v.exe", "dot.exe") {
    if (-not (Test-Path "$appDir\$item")) { throw "The build has no $item in $appDir" }
    Copy-Item -Recurse "$appDir\$item" $stage
}
# The bundles' git files are not part of the app
Get-ChildItem $stage -Recurse -Force -Include ".gitignore", ".gitattributes" | Remove-Item -Force

Invoke-Tool "$QtDir\bin\windeployqt.exe" @("--release", "--no-translations", "--no-compiler-runtime", "$stage\BitSimulator.exe")

# The MSVC runtime next to the exe, so the app also starts where the Visual C++ Redistributable is not installed
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory | Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "No MSVC runtime folder under $vs\VC\Redist\MSVC" }
Copy-Item "$crt\*.dll" $stage
Write-Host "MSVC runtime from $($crt.FullName)"

# Smoke test: the app must start with only its own folder to find DLLs in, and keep running
$cleanPath = "$env:SystemRoot\System32;$env:SystemRoot"
$savedPath = $env:PATH
$env:PATH = $cleanPath
try {
    $process = Start-Process -FilePath "$stage\BitSimulator.exe" -WorkingDirectory $stage -PassThru
    Start-Sleep -Seconds 10
    if ($process.HasExited) { throw "The packaged app exited at once (exit code $($process.ExitCode)); a DLL is probably missing" }
    Stop-Process -Id $process.Id -Force
    $process.WaitForExit()
} finally {
    $env:PATH = $savedPath
}
# A run can leave a saves folder behind
if (Test-Path "$stage\saves") { Remove-Item -Recurse -Force "$stage\saves" }
Write-Host "Smoke test passed: the packaged app starts on its own"

$zip = Join-Path $OutDir "$Name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Invoke-Tool "7z" @("a", "-tzip", "-mx=9", $zip, $stage) | Out-Null
Write-Host "Packaged $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
