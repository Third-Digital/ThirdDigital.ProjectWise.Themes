#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path $PSScriptRoot
Push-Location $repo
try {
    $files = @(& git ls-files)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate tracked files.' }
    $privatePaths = $files | Where-Object {
        $_ -match '(^|/)(\.claude|\.codex|\.agents|\.idea|artifacts|release-output|scratchpad)(/|$)' -or
        $_ -match '\.(jsonl|log|pdb|wixpdb|mp4|msi|dmp)$'
    }
    if ($privatePaths) { throw "Private or generated artifacts are tracked: $($privatePaths -join ', ')" }
    foreach ($file in $files) {
        if ($file -match '\.(props|vcxproj|csproj|wixproj|wxs|manifest)$') {
            $null = [xml](Get-Content -LiteralPath $file -Raw)
        }
        if ($file -match '\.ps1$') {
            $tokens = $null
            $parseErrors = $null
            $null = [Management.Automation.Language.Parser]::ParseFile((Join-Path $repo $file), [ref]$tokens, [ref]$parseErrors)
            if ($parseErrors.Count) { throw "PowerShell syntax errors in ${file}: $($parseErrors.Message -join '; ')" }
        }
    }
    $version = ([xml](Get-Content Version.props -Raw)).Project.PropertyGroup
    if ($version.ThemesVersion -notmatch '^\d+\.\d+\.\d+$' -or $version.ThemesFileVersion -ne ($version.ThemesVersion.Replace('.', ',') + ',0')) {
        throw 'Version.props product and file versions do not agree.'
    }
    Write-Host 'PASS tracked artifact policy, XML, PowerShell syntax and version consistency'
} finally { Pop-Location }
