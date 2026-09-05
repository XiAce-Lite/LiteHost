$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Visual Studio が見つかりません" }

$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "C++ ツールチェイン付きの Visual Studio が見つかりません" }

$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

if (-not (Test-Path $cmake)) { throw "CMake が見つかりません: $cmake" }
if (-not (Test-Path $ninja)) { throw "Ninja が見つかりません: $ninja" }
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat が見つかりません: $vcvars" }

$buildDir = Join-Path $Root "build"
$config = if ($args.Count -gt 0) { $args[0] } else { "Release" }

$cmd = "`"$vcvars`" && `"$cmake`" -B `"$buildDir`" -S `"$Root`" -G Ninja -DCMAKE_BUILD_TYPE=$config -DCMAKE_MAKE_PROGRAM=`"$ninja`" && `"$cmake`" --build `"$buildDir`""
Write-Host "Building LiteHost ($config)..."
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

$exe = Join-Path $buildDir "LiteHost_artefacts\$config\LiteHost.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $buildDir "LiteHost_artefacts\LiteHost.exe"
}
Write-Host "OK: $exe"
