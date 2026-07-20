param(
    [string]$Configuration = "Release",
    [string]$Output = "dist\foo_musiccom.fb2k-component"
)
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $projectRoot "dist\foo_musiccom.dll"
if (-not (Test-Path -LiteralPath $dll)) { throw "Build output not found: $dll" }
$stage = Join-Path $projectRoot "build-msvc\package"
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $stage "foo_musiccom.dll") -Force
$zip = [IO.Path]::ChangeExtension((Join-Path $projectRoot $Output), ".zip")
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath (Join-Path $stage "foo_musiccom.dll") -DestinationPath $zip
$package = Join-Path $projectRoot $Output
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Force }
Move-Item -LiteralPath $zip -Destination $package
Write-Output $package
