#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$Repository = 'Third-Digital/ThirdDigital.ProjectWise.Themes',
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot) 'release-output')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path $PSScriptRoot
function Invoke-Gh {
    & gh @args
    if ($LASTEXITCODE -ne 0) { throw "GitHub CLI failed: $($args[0])" }
}
Push-Location $repo
try {
    if ((& git branch --show-current) -ne 'main') { throw 'Publish from main.' }
    if (& git status --porcelain) { throw 'Commit all release changes before publishing.' }
    $version = ([xml](Get-Content Version.props -Raw)).Project.PropertyGroup.ThemesVersion
    $commit = (& git rev-parse HEAD).Trim()
    $evidence = Get-Content (Join-Path $OutputDirectory 'build-evidence.json') -Raw | ConvertFrom-Json
    $asset = Join-Path $OutputDirectory "ThirdDigital.ProjectWise.Themes-$version-x86.msi"
    if ($evidence.commit -ne $commit -or $evidence.version -ne $version -or $evidence.tests -ne 'Full') {
        throw 'Build and run the full verification suite at the committed release revision first.'
    }
    if ((Get-FileHash $asset -Algorithm SHA256).Hash -ne $evidence.sha256) { throw 'Installer changed after verification.' }
    $visibility = Invoke-Gh repo view $Repository --json visibility --jq .visibility
    if ($visibility -ne 'PRIVATE') { throw 'This release process currently requires a private repository. Review this guard before a public launch.' }
    $remoteHead = Invoke-Gh api "repos/$Repository/commits/main" --jq .sha
    if ($remoteHead -ne $commit) { throw 'Push the reviewed main branch before publishing.' }
    & git tag -a "v$version" -m "ProjectWise Themes v$version" $commit
    if ($LASTEXITCODE -ne 0) { throw 'Tag already exists or could not be created; inspect it before retrying.' }
    & git push origin "refs/tags/v$version"
    if ($LASTEXITCODE -ne 0) { throw 'Tag push failed.' }
    Invoke-Gh release create "v$version" $asset (Join-Path $OutputDirectory 'SHA256SUMS.txt') --repo $Repository --verify-tag --title "ProjectWise Themes v$version" --notes-file (Join-Path $repo 'RELEASE_NOTES.md')
} finally { Pop-Location }
