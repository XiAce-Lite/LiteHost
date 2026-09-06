$ErrorActionPreference = "Stop"

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
{
    Write-Host "管理者権限で再実行します（UAC）..."
    $self = $MyInvocation.MyCommand.Path
    Start-Process -FilePath "powershell.exe" -Verb RunAs -Wait `
        -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $self)
    exit $LASTEXITCODE
}

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$paths = @(
    (Join-Path $root "build"),
    (Join-Path $root "dist")
)

foreach ($path in $paths)
{
    if (-not (Test-Path $path))
    {
        New-Item -ItemType Directory -Path $path | Out-Null
    }

    Add-MpPreference -ExclusionPath $path
    Write-Host "Excluded: $path"
}

Write-Host ""
Write-Host "現在の除外パス:"
(Get-MpPreference).ExclusionPath
