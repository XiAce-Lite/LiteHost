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
if (-not (Test-Path $exe)) {
    throw "ビルド成果物が見つかりません。Defender が隔離／削除した可能性があります。管理者で .\scripts\add-defender-exclusion.ps1 を実行してから再ビルドしてください。"
}

$scanner = Join-Path (Split-Path $exe -Parent) "LiteHostScanner.exe"
if (-not (Test-Path $scanner)) {
    Write-Warning "LiteHostScanner.exe が隣にありません。VST3 スキャンはプロセス内フォールバックになります: $scanner"
}

# Defender は書き込み直後に消すことがある
Start-Sleep -Seconds 2
if (-not (Test-Path $exe)) {
    throw "LiteHost.exe がビルド直後に消えました（Defender の誤検知が多いです）。管理者で .\scripts\add-defender-exclusion.ps1 を実行してから再ビルドしてください。"
}

Write-Host "OK: $exe"
if (Test-Path $scanner) { Write-Host "OK: $scanner" }
