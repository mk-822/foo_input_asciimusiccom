[CmdletBinding()]
param(
    [string]$Version,
    [string]$Notes,
    [switch]$Draft,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeFile = Join-Path $projectRoot "CMakeLists.txt"
$componentSource = Join-Path $projectRoot "src\foobar_input.cpp"
$artifact = Join-Path $projectRoot "dist\foo_input_asciimusiccom.fb2k-component"

function Invoke-Checked {
    param([Parameter(Mandatory)][string]$Program, [Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

Push-Location $projectRoot
try {
    $cmakeText = Get-Content -Raw -LiteralPath $cmakeFile
    $componentText = Get-Content -Raw -LiteralPath $componentSource
    $cmakeMatch = [regex]::Match($cmakeText, 'project\(foo_input_asciimusiccom VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
    $componentMatch = [regex]::Match($componentText, 'DECLARE_COMPONENT_VERSION\(\s*"[^"]+",\s*"([0-9]+\.[0-9]+\.[0-9]+)"')
    if (-not $cmakeMatch.Success -or -not $componentMatch.Success) { throw "Could not determine the project/component version" }

    $projectVersion = $cmakeMatch.Groups[1].Value
    $componentVersion = $componentMatch.Groups[1].Value
    if ($projectVersion -ne $componentVersion) { throw "Version mismatch: CMake=$projectVersion, component=$componentVersion" }
    if (-not $Version) { $Version = $projectVersion }
    $Version = $Version.TrimStart('v')
    if ($Version -ne $projectVersion) { throw "Requested version $Version does not match source version $projectVersion" }
    $tag = "v$Version"

    $gh = Get-Command gh -ErrorAction SilentlyContinue
    $ghPath = if ($gh) { $gh.Source } else { Join-Path $env:ProgramFiles "GitHub CLI\gh.exe" }
    if (-not (Test-Path -LiteralPath $ghPath)) { throw "GitHub CLI (gh) is required: https://cli.github.com/" }
    Invoke-Checked $ghPath auth status

    $status = & git status --porcelain
    if ($LASTEXITCODE -ne 0) { throw "git status failed" }
    if ($status) { throw "Working tree is not clean. Commit or stash changes before releasing." }
    $branch = (& git branch --show-current).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $branch) { throw "Could not determine the current branch" }
    Invoke-Checked git fetch origin $branch --tags
    $localCommit = (& git rev-parse HEAD).Trim()
    $remoteCommit = (& git rev-parse "origin/$branch").Trim()
    if ($localCommit -ne $remoteCommit) { throw "Local $branch is not synchronized with origin/$branch" }
    $tagCommit = (& git rev-list -n 1 $tag 2>$null)
    if ($LASTEXITCODE -eq 0 -and $tagCommit.Trim() -ne $localCommit) {
        throw "Tag $tag already exists at a different commit"
    }

    if (-not $SkipBuild) {
        Invoke-Checked powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -Configuration Release
    }
    if (-not (Test-Path -LiteralPath $artifact)) { throw "Release artifact was not created: $artifact" }

    if (-not $tagCommit) { Invoke-Checked git tag -a $tag -m "Release $tag" }
    try {
        Invoke-Checked git push origin $tag
        $releaseArgs = @('release', 'create', $tag, $artifact, '--title', $tag, '--verify-tag')
        if ($Draft) { $releaseArgs += '--draft' }
        if ($Notes) { $releaseArgs += @('--notes', $Notes) } else { $releaseArgs += '--generate-notes' }
        Invoke-Checked $ghPath @releaseArgs
    }
    catch {
        Write-Warning "The tag may already have been pushed. Fix the error, then create the release for that tag."
        throw
    }

    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
    Write-Output "Published $tag"
    Write-Output "Artifact: $artifact"
    Write-Output "SHA256: $hash"
}
finally { Pop-Location }
