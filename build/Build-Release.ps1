#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$PWSDKDir = $env:PWSDKDir,
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot) 'release-output'),
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path $PSScriptRoot
$version = ([xml](Get-Content (Join-Path $repo 'Version.props') -Raw)).Project.PropertyGroup.ThemesVersion
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
Push-Location $repo
try {
    $revision = (& git rev-parse HEAD).Trim()
    $dirty = [bool](& git status --porcelain)
    if ($dirty -and !$SkipTests) { throw 'Commit release changes before building a verified release.' }
    if (!(Test-Path 'external/minhook/include/MinHook.h')) { throw 'Run git submodule update --init --recursive first.' }
    $suite = if ($SkipTests) { 'Build' } else { 'Full' }
    & ./ThirdDigital.ProjectWise.Themes.Harness/Verify.ps1 -Suite $suite -Configuration Release -ResultsRoot (Join-Path $output 'verification') -PWSDKDir $PWSDKDir
    if ($LASTEXITCODE -ne 0) { throw 'Module verification failed.' }
    $module = Join-Path $repo 'ThirdDigital.ProjectWise.Themes/out/Win32/Release/bin/ThirdDigital.ProjectWise.Themes.dll'
    if ((Get-Item $module).VersionInfo.ProductVersion -ne $version) { throw 'DLL version does not match Version.props.' }
    & dotnet build ./ThirdDigital.ProjectWise.Themes.Install/ThirdDigital.ProjectWise.Themes.Install.wixproj -c Release -v minimal
    if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
    $name = "ThirdDigital.ProjectWise.Themes-$version-x86.msi"
    Copy-Item -LiteralPath (Join-Path $repo "ThirdDigital.ProjectWise.Themes.Install/out/Release/$name") -Destination (Join-Path $output $name) -Force
    $hash = (Get-FileHash (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'), "$hash  $name`n", [Text.UTF8Encoding]::new($false))
    if (!$SkipTests -and ((& git rev-parse HEAD).Trim() -ne $revision -or (& git status --porcelain))) {
        throw 'Source changed during verification. Commit the changes and rebuild.'
    }
    [ordered]@{ version = $version; commit = $revision; tests = $suite; sha256 = $hash } |
        ConvertTo-Json | Set-Content (Join-Path $output 'build-evidence.json') -Encoding utf8
    Write-Host "Release assets: $output"
} finally { Pop-Location }
