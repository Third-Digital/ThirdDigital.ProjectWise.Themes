#Requires -Version 7.2
<#
.SYNOPSIS
    Builds and verifies ThirdDigital.ProjectWise.Themes (x86 only).
.NOTES
    The ProjectWise SDK location comes from -PWSDKDir or the caller's PWSDKDir environment variable and is
    passed only to the child build process. Evidence (commands, exit codes, logs, TRX, harness output) is
    written beneath -ResultsRoot. Harness children use a disposable HKCU key named by TDPWTHEMES_SETTINGS_KEY.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('BuildContracts', 'Build', 'Unit', 'Native', 'Picker', 'WinForms', 'ContextMenus', 'Regression', 'Full')]
    [string]$Suite = 'Full',

    [string]$Filter = '',

    [ValidateSet('all', 'pre-init', 'post-init', 'none', 'stock')]
    [string]$Mode = 'all',

    [Parameter(Mandatory)]
    [string]$ResultsRoot,

    [switch]$ExpectFailure,

    [string]$PWSDKDir = $env:PWSDKDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Repo = Split-Path -Parent $PSScriptRoot
$Solution = Join-Path $Repo 'ThirdDigital.ProjectWise.Themes.sln'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$Out = Join-Path $ResultsRoot "$Stamp-$Suite-$Configuration"
New-Item -ItemType Directory -Path $Out -Force | Out-Null
$CommandLog = Join-Path $ResultsRoot 'commands.log'
$Summary = Join-Path $Out 'summary.txt'

function Write-Summary([string]$Line) {
    Add-Content -LiteralPath $Summary -Value $Line -Encoding utf8
    Write-Host $Line
}

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $VsWhere)) { throw "vswhere.exe not found at $VsWhere; Visual Studio 2022 is required." }
$VsRoot = & $VsWhere -version '[17.0,18.0)' -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1
$MSBuild = & $VsWhere -version '[17.0,18.0)' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $VsRoot -or -not $MSBuild) { throw 'No Visual Studio 2022 (17.x) installation with MSBuild was found.' }
$VsTest = Join-Path $VsRoot 'Common7\IDE\Extensions\TestPlatform\vstest.console.exe'
if (-not (Test-Path -LiteralPath $VsTest)) { throw "vstest.console.exe not found at $VsTest." }
$DumpBin = Get-ChildItem -Path (Join-Path $VsRoot 'VC\Tools\MSVC\*\bin\Hostx86\x86\dumpbin.exe') -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $DumpBin) { throw 'dumpbin.exe not found in the Visual Studio 2022 installation.' }

$NativeHarness = Join-Path $Repo "ThirdDigital.ProjectWise.Themes.Harness\out\Win32\$Configuration\bin\ThirdDigital.ProjectWise.Themes.Harness.exe"
$WinFormsHarness = Join-Path $Repo "ThirdDigital.ProjectWise.Themes.Harness.WinForms\out\x86\$Configuration\bin\ThirdDigital.ProjectWise.Themes.Harness.WinForms.exe"
$ModuleDll = Join-Path $Repo "ThirdDigital.ProjectWise.Themes\out\Win32\$Configuration\bin\ThirdDigital.ProjectWise.Themes.dll"
$BridgeDll = Join-Path $Repo "ThirdDigital.ProjectWise.Themes.Harness\out\Win32\$Configuration\bin\ThirdDigital.ProjectWise.Themes.HarnessBridge.dll"
$TestsDll = Join-Path $Repo "ThirdDigital.ProjectWise.Themes.Tests\out\Win32\$Configuration\bin\ThirdDigital.ProjectWise.Themes.Tests.dll"

function Invoke-Logged {
    param(
        [string]$File,
        [string[]]$Arguments,
        [hashtable]$Environment = @{},
        [string]$LogName,
        [int]$TimeoutSeconds = 1800
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new($File)
    foreach ($a in $Arguments) { $psi.ArgumentList.Add($a) }
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.WorkingDirectory = $Repo
    foreach ($k in $Environment.Keys) {
        if ($null -eq $Environment[$k]) { [void]$psi.Environment.Remove($k) } else { $psi.Environment[$k] = [string]$Environment[$k] }
    }
    $started = Get-Date
    $p = [System.Diagnostics.Process]::Start($psi)
    $stdout = $p.StandardOutput.ReadToEndAsync()
    $stderr = $p.StandardError.ReadToEndAsync()
    $timedOut = -not $p.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) { try { $p.Kill($true) } catch { } ; [void]$p.WaitForExit(10000) }
    $p.WaitForExit()
    $code = if ($timedOut) { -1 } else { $p.ExitCode }
    $text = $stdout.Result + $stderr.Result
    if ($LogName) { Set-Content -LiteralPath (Join-Path $Out $LogName) -Value $text -Encoding utf8 }
    $envText = ($Environment.Keys | Sort-Object | ForEach-Object { if ($null -eq $Environment[$_]) { "-$_" } else { "$_=$($Environment[$_])" } }) -join ' '
    $line = '{0:o} exit={1}{2} [{3}] "{4}" {5}' -f $started, $code, $(if ($timedOut) { ' (timeout)' } else { '' }), $envText, $File, (($Arguments | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' ')
    Add-Content -LiteralPath $CommandLog -Value $line -Encoding utf8
    [pscustomobject]@{ ExitCode = $code; Output = $text; TimedOut = $timedOut; Duration = (Get-Date) - $started }
}

function Get-SdkEnvironment([bool]$WithSdk) {
    if (-not $WithSdk) { return @{ PWSDKDir = $null } }
    if ([string]::IsNullOrWhiteSpace($PWSDKDir)) { throw 'The ProjectWise SDK location is required: pass -PWSDKDir or set PWSDKDir for this call.' }
    @{ PWSDKDir = $PWSDKDir }
}

function Invoke-Build {
    param(
        [string]$Target,
        [string]$Name,
        [hashtable]$Environment,
        [switch]$AllowFailure
    )
    $log = Join-Path $Out "build-$Name.log"
    $arguments = @($Solution, '-restore', "-t:$Target", "-p:Configuration=$Configuration", '-p:Platform=x86',
        '-m', '-nr:false', '-nologo', "-flp:LogFile=$log;Verbosity=normal;Encoding=UTF-8", '-clp:Summary;Verbosity=minimal')
    $result = Invoke-Logged -File $MSBuild -Arguments $arguments -Environment $Environment -LogName "build-$Name.console.txt"
    $content = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw } else { '' }
    $warnings = [regex]::Matches($content, '(?m)^\s*(\d+) Warning\(s\)') | Select-Object -Last 1
    $errors = [regex]::Matches($content, '(?m)^\s*(\d+) Error\(s\)') | Select-Object -Last 1
    $warningCount = if ($warnings) { [int]$warnings.Groups[1].Value } else { -1 }
    $errorCount = if ($errors) { [int]$errors.Groups[1].Value } else { -1 }
    Write-Summary ("build {0} [{1}] exit={2} warnings={3} errors={4} log={5}" -f $Name, $Target, $result.ExitCode, $warningCount, $errorCount, $log)
    if (-not $AllowFailure) {
        if ($result.ExitCode -ne 0 -or $errorCount -ne 0) { throw "Build '$Name' failed; see $log" }
        if ($warningCount -ne 0) { throw "Build '$Name' produced $warningCount warning(s); see $log" }
    }
    [pscustomobject]@{ ExitCode = $result.ExitCode; Warnings = $warningCount; Errors = $errorCount; Log = $log; Content = $content }
}

function Get-NativeProjects {
    Select-String -LiteralPath $Solution -Pattern '^Project\("\{[^}]+\}"\) = "([^"]+)", "([^"]+\.vcxproj)"' |
        ForEach-Object { [pscustomobject]@{ Name = $_.Matches[0].Groups[1].Value; Path = Join-Path $Repo $_.Matches[0].Groups[2].Value } }
}

function Test-CompilerFlags([string]$Content) {
    $lines = [regex]::Matches($Content, '(?m)^.*\\CL\.exe .*$') | ForEach-Object { $_.Value }
    foreach ($project in Get-NativeProjects) {
        $dir = (Split-Path -Parent $project.Path) -replace '\\+', '\'
        $declared = [regex]::Match((Get-Content -LiteralPath $project.Path -Raw), '<IntDir>([^<]+)</IntDir>').Groups[1].Value
        $intDir = ($declared -replace '\$\(ProjectDir\)', "$dir\" -replace '\$\(Platform\)', 'Win32' -replace '\$\(Configuration\)', $Configuration) -replace '\\+', '\'
        $mine = $lines | Where-Object { ($_ -replace '\\+', '\') -like "*/Fo`"$intDir*" }
        if (-not $mine) { throw "No compiler invocation found for $($project.Name) in the build log." }
        foreach ($line in $mine) {
            if ($project.Name -eq 'MinHook') {
                if ($line -notmatch ' /W3 ' -or $line -match ' /WX ') { throw "MinHook must compile with /W3 and without /WX: $line" }
            }
            elseif ($line -notmatch ' /W4 ' -or $line -notmatch ' /WX ' -or $line -notmatch ' /std:c\+\+20 ') {
                throw "$($project.Name) must compile with /W4 /WX /std:c++20: $line"
            }
        }
        Write-Summary ("flags {0}: {1} compiler invocation(s) checked" -f $project.Name, @($mine).Count)
    }
}

function Invoke-DumpBin([string]$Switch, [string]$Path, [string]$Name) {
    $r = Invoke-Logged -File $DumpBin -Arguments @('/nologo', $Switch, $Path) -LogName "dumpbin-$Name.txt" -TimeoutSeconds 120
    if ($r.ExitCode -ne 0) { throw "dumpbin $Switch failed for $Path" }
    $r.Output
}

function Test-X86Artifacts {
    $artifacts = @()
    foreach ($project in Get-NativeProjects) {
        $bin = Join-Path (Split-Path -Parent $project.Path) "out\Win32\$Configuration\bin"
        $artifacts += Get-ChildItem -LiteralPath $bin -File | Where-Object { $_.Extension -in '.dll', '.exe', '.lib' }
    }
    $artifacts += Get-Item -LiteralPath $WinFormsHarness
    foreach ($file in $artifacts) {
        $headers = Invoke-DumpBin '/headers' $file.FullName "headers-$($file.Name)"
        if ($headers -notmatch '14C machine \(x86\)' -or $headers -match '8664 machine') { throw "AllArtifactsAreX86 failed: $($file.FullName)" }
    }
    $clr = Invoke-DumpBin '/clrheader' $WinFormsHarness 'clrheader-winforms'
    if ($clr -notmatch '32-Bit Required') { throw 'AllArtifactsAreX86 failed: the WinForms harness is not marked 32-bit required.' }
    Write-Summary ("check AllArtifactsAreX86: pass ({0} artefacts)" -f $artifacts.Count)
}

function Test-SingleExport {
    $exports = Invoke-DumpBin '/exports' $ModuleDll 'exports-module'
    $names = @([regex]::Matches($exports, '(?m)^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)') | ForEach-Object { $_.Groups[1].Value })
    if ($names.Count -ne 1 -or $names[0] -ne 'CustomInitialize') { throw "CustomInitializeIsOnlyExport failed: exports are '$($names -join ', ')'" }
    Write-Summary 'check CustomInitializeIsOnlyExport: pass'
}

function Test-PickerImports {
    $imports = Invoke-DumpBin '/imports' $ModuleDll 'imports-module'
    $split = $imports -split 'Section contains the following delay load imports:', 2
    if ($split.Count -ne 2) { throw 'DelayLoadedDmawin failed: the module has no delay-load import section.' }
    if ($split[0] -match '(?im)^\s+dmawin\.dll') { throw 'DelayLoadedDmawin failed: dmawin.dll is a static import.' }
    if ($split[1] -notmatch '(?im)^\s+dmawin\.dll') { throw 'DelayLoadedDmawin failed: dmawin.dll is not delay-loaded.' }
    foreach ($name in 'aaApi_AddCustomMenuItem', 'aaApi_ReserveCommandIdByType') {
        if ($split[1] -notmatch "(?m)^\s+(?:[0-9A-F]+\s+){1,2}$name\s*$") { throw "DelayLoadedDmawin failed: undecorated import $name not found." }
    }
    Write-Summary 'check DelayLoadedDmawin: pass (aaApi_AddCustomMenuItem, aaApi_ReserveCommandIdByType)'
}

function Test-MissingSdk {
    $missing = Invoke-Build -Target 'ThirdDigital_ProjectWise_Themes:Rebuild' -Name 'missing-sdk' -Environment @{ PWSDKDir = $null } -AllowFailure
    if ($missing.ExitCode -eq 0 -or $missing.Content -notmatch 'PWSDKDir is not set') { throw 'MissingSdkFailsClearly failed: the unset SDK was not reported.' }
    $empty = Join-Path $Out 'empty-sdk'
    New-Item -ItemType Directory -Path $empty -Force | Out-Null
    $wrong = Invoke-Build -Target 'ThirdDigital_ProjectWise_Themes:Rebuild' -Name 'wrong-sdk' -Environment @{ PWSDKDir = $empty } -AllowFailure
    if ($wrong.ExitCode -eq 0 -or $wrong.Content -notmatch 'include\\aawindms\.h was not found') { throw 'MissingSdkFailsClearly failed: a folder without the SDK was not reported.' }
    Write-Summary 'check MissingSdkFailsClearly: pass'
}

function Invoke-Unit([string]$TestFilter) {
    Invoke-Build -Target 'ThirdDigital_ProjectWise_Themes_Tests' -Name 'unit' -Environment (Get-SdkEnvironment $false) | Out-Null
    $trxName = "unit-$Stamp.trx"
    $arguments = @($TestsDll, '/Platform:x86', "/logger:trx;LogFileName=$trxName", "/ResultsDirectory:$Out")
    if ($TestFilter) { $arguments += "/TestCaseFilter:FullyQualifiedName~$TestFilter" }
    $run = Invoke-Logged -File $VsTest -Arguments $arguments -LogName 'unit-vstest.txt'
    $trxPath = Join-Path $Out $trxName
    if (-not (Test-Path -LiteralPath $trxPath)) { throw "vstest produced no TRX (exit $($run.ExitCode)); see unit-vstest.txt" }
    [xml]$trx = Get-Content -LiteralPath $trxPath -Raw
    $counters = $trx.TestRun.ResultSummary.Counters
    $results = @($trx.TestRun.Results.UnitTestResult)
    foreach ($result in $results) { Write-Summary ("unit {0}: {1}" -f $result.testName, $result.outcome) }
    Write-Summary ("unit totals: total={0} passed={1} failed={2} trx={3}" -f $counters.total, $counters.passed, $counters.failed, $trxPath)
    if ([int]$counters.total -eq 0) { throw "No unit tests matched '$TestFilter'." }
    $unexpected = @($results | Where-Object { $_.outcome -notin 'Passed', 'Failed' })
    if ($unexpected.Count -gt 0) { throw "Unit tests did not execute cleanly: $($unexpected.testName -join ', ')" }
    if ($ExpectFailure) {
        if ([int]$counters.failed -eq 0) { throw "Expected failing tests for '$TestFilter' but all passed." }
        Write-Summary "expected failure confirmed for '$TestFilter'"
    }
    elseif ($run.ExitCode -ne 0 -or [int]$counters.failed -ne 0) { throw 'Unit tests failed.' }
}

function Get-SchemeNames {
    $r = Invoke-Logged -File $NativeHarness -Arguments @('--schemes') -LogName 'schemes.txt' -TimeoutSeconds 60
    if ($r.ExitCode -ne 0) { throw 'The native harness could not list the schemes.' }
    @($r.Output -split "`r?`n" | Where-Object { $_ })
}

function Invoke-HarnessChecks {
    param([string]$Kind, [string]$Exe, [string]$Group)
    if (-not (Test-Path -LiteralPath $Exe)) { throw "$Kind harness not found at $Exe" }
    $listArgs = @('--list')
    if ($Group) { $listArgs += @('--filter', $Group) }
    $list = Invoke-Logged -File $Exe -Arguments $listArgs -LogName "list-$Kind.txt" -TimeoutSeconds 60
    if ($list.ExitCode -ne 0) { throw "$Kind harness could not list checks." }
    $checks = @($list.Output -split "`r?`n" | Where-Object { $_ } | ForEach-Object {
            $f = $_ -split "`t"
            [pscustomobject]@{ Group = $f[0]; Name = $f[1]; Mode = $f[2]; Host = $f[3]; Schemes = $f[4] }
        } | Where-Object { $Mode -eq 'all' -or $_.Mode -eq '-' -or $_.Mode -eq $Mode })
    if ($checks.Count -eq 0) { throw "No $Kind checks matched group '$Group' and mode '$Mode'." }

    $schemeNames = $null
    $pwcExe = $null
    $results = @()
    $index = 0
    foreach ($check in $checks) {
        $schemes = @('')
        if ($check.Schemes -eq '*') {
            if ($null -eq $schemeNames) { $schemeNames = Get-SchemeNames }
            $schemes = $schemeNames
        }
        foreach ($scheme in $schemes) {
            $index++
            $label = if ($scheme) { "$($check.Name)[$scheme]" } else { $check.Name }
            $childDir = Join-Path $Out ("children\{0}-{1:D3}-{2}" -f $Kind, $index, ($label -replace '[^A-Za-z0-9\-]+', '_'))
            $childTemp = Join-Path $childDir 'temp'
            New-Item -ItemType Directory -Path $childTemp -Force | Out-Null
            $childExe = $Exe
            if ($check.Host -eq 'pwc') {
                if (-not $pwcExe) {
                    $pwcDir = Join-Path $Out "host-$Kind"
                    New-Item -ItemType Directory -Path $pwcDir -Force | Out-Null
                    $pwcExe = Join-Path $pwcDir 'pwc.exe'
                    Copy-Item -LiteralPath $Exe -Destination $pwcExe
                    $config = "$Exe.config"
                    if (Test-Path -LiteralPath $config) { Copy-Item -LiteralPath $config -Destination "$pwcExe.config" }
                }
                $childExe = $pwcExe
            }
            $key = "Software\TDPWThemesHarness-$PID-$index"
            $arguments = @('--run', $check.Name, '--dll', $ModuleDll, '--bridge', $BridgeDll, '--evidence', $childDir)
            if ($scheme) { $arguments += @('--scheme', $scheme) }
            $r = Invoke-OnHiddenDesktop -File $childExe -Arguments $arguments -OutputPath (Join-Path $childDir 'output.txt') -TimeoutSeconds 180 `
                -Environment @{ TDPWTHEMES_SETTINGS_KEY = $key; TEMP = $childTemp; TMP = $childTemp; PWSDKDir = $null }
            $outcome = switch ($r.ExitCode) { 0 { 'PASS' } 1 { 'FAIL' } 2 { 'INVALID' } -1 { 'TIMEOUT' } default { 'CRASH' } }
            if ($outcome -ne 'TIMEOUT' -and $r.Output -notmatch [regex]::Escape("desktop $DesktopName")) {
                $outcome = 'INVALID'
                Write-Summary "    INVALID $label did not report running on the hidden desktop"
            }
            $leftover = Test-Path -LiteralPath "HKCU:\$key"
            if ($leftover) { Remove-Item -LiteralPath "HKCU:\$key" -Recurse -Force }
            $results += [pscustomobject]@{ Label = $label; Outcome = $outcome; ExitCode = $r.ExitCode; KeyRemoved = -not $leftover }
            Write-Summary ("{0} {1} {2}: {3} (exit {4}, {5:N1}s){6}" -f $Kind, $check.Group, $label, $outcome, $r.ExitCode, $r.Duration.TotalSeconds, $(if ($leftover) { ' HarnessKeyRemoved: FAIL' } else { '' }))
            if ($outcome -ne 'PASS') {
                ($r.Output -split "`r?`n" | Where-Object { $_ -match '^(FAIL|INVALID)' }) | ForEach-Object { Write-Summary "    $_" }
            }
        }
    }
    $results | Export-Csv -LiteralPath (Join-Path $Out "results-$Kind.csv") -NoTypeInformation -Encoding utf8
    $counts = $results | Group-Object Outcome | ForEach-Object { "$($_.Name)=$($_.Count)" }
    $keysLeft = @($results | Where-Object { -not $_.KeyRemoved }).Count
    Write-Summary ("{0} totals: {1}; HarnessKeyRemoved: {2}" -f $Kind, ($counts -join ' '), $(if ($keysLeft -eq 0) { 'pass' } else { "FAIL ($keysLeft left)" }))
    $bad = @($results | Where-Object { $_.Outcome -in 'INVALID', 'TIMEOUT', 'CRASH' })
    if ($ExpectFailure) {
        if ($bad.Count -gt 0) { throw "$Kind checks did not run cleanly: $($bad.Label -join ', ')" }
        if (@($results | Where-Object Outcome -eq 'FAIL').Count -eq 0) { throw "Expected failing $Kind checks but all passed." }
        Write-Summary "expected failure confirmed for $Kind '$Group'"
    }
    elseif ($bad.Count -gt 0 -or @($results | Where-Object Outcome -ne 'PASS').Count -gt 0 -or $keysLeft -gt 0) {
        throw "$Kind checks failed."
    }
}

function Write-Environment {
    $os = Get-CimInstance Win32_OperatingSystem
    $light = (Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' -ErrorAction SilentlyContinue).AppsUseLightTheme
    $minhook = (& git -C (Join-Path $Repo 'external\minhook') rev-parse HEAD) 2>$null
    Write-Summary ("environment: {0} build {1} {2}; configuration {3}|x86; MinHook {4}; AppsUseLightTheme={5}" -f $os.Caption, $os.BuildNumber, $os.OSArchitecture, $Configuration, $minhook, $light)
}


# Harness children create windows, capture them and open menus; they run on a private desktop so nothing
# appears on, or takes focus from, the interactive desktop.
Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Text;

public static class HarnessDesktop
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInformation
    {
        public IntPtr hProcess, hThread;
        public int dwProcessId, dwThreadId;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct SecurityAttributes
    {
        public int nLength;
        public IntPtr lpSecurityDescriptor;
        public bool bInheritHandle;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr mode, uint flags, uint access, IntPtr attributes);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcessW(string application, StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes,
        bool inheritHandles, uint flags, IntPtr environment, string directory, ref StartupInfo startup, out ProcessInformation information);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFileW(string path, uint access, uint share, ref SecurityAttributes attributes, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    public static IntPtr Create(string name)
    {
        IntPtr desktop = CreateDesktopW(name, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000, IntPtr.Zero);
        if (desktop == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "CreateDesktopW failed");
        return desktop;
    }

    public static void Close(IntPtr desktop)
    {
        if (desktop != IntPtr.Zero) CloseDesktop(desktop);
    }

    // Returns the exit code, or -1 if the child was stopped after the timeout.
    public static int Run(string desktop, string executable, string commandLine, IDictionary environment, string directory, string outputPath, int timeoutMilliseconds)
    {
        var block = new StringBuilder();
        var keys = new ArrayList(environment.Keys);
        keys.Sort(StringComparer.OrdinalIgnoreCase);
        foreach (string key in keys) block.Append(key).Append('=').Append((string)environment[key]).Append('\0');
        block.Append('\0');
        IntPtr env = Marshal.StringToHGlobalUni(block.ToString());
        var inherit = new SecurityAttributes { nLength = Marshal.SizeOf(typeof(SecurityAttributes)), bInheritHandle = true };
        IntPtr output = CreateFileW(outputPath, 0x40000000, 1, ref inherit, 2, 0x80, IntPtr.Zero);
        if (output == new IntPtr(-1)) { Marshal.FreeHGlobal(env); throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "cannot create " + outputPath); }
        try
        {
            var startup = new StartupInfo { cb = Marshal.SizeOf(typeof(StartupInfo)), lpDesktop = desktop, dwFlags = 0x100, hStdOutput = output, hStdError = output };
            ProcessInformation info;
            // CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW
            if (!CreateProcessW(executable, new StringBuilder(commandLine), IntPtr.Zero, IntPtr.Zero, true, 0x00000400 | 0x08000000, env, directory, ref startup, out info))
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "CreateProcessW failed for " + executable);
            try
            {
                if (WaitForSingleObject(info.hProcess, (uint)timeoutMilliseconds) != 0)
                {
                    TerminateProcess(info.hProcess, 0xFFFFFFFF);
                    WaitForSingleObject(info.hProcess, 10000);
                    return -1;
                }
                uint code;
                GetExitCodeProcess(info.hProcess, out code);
                return unchecked((int)code);
            }
            finally
            {
                CloseHandle(info.hThread);
                CloseHandle(info.hProcess);
            }
        }
        finally
        {
            CloseHandle(output);
            Marshal.FreeHGlobal(env);
        }
    }
}
'@

$DesktopName = "TDPWThemesHarness-$PID-$Stamp"
$DesktopHandle = [IntPtr]::Zero

function Invoke-OnHiddenDesktop {
    param([string]$File, [string[]]$Arguments, [hashtable]$Environment, [string]$OutputPath, [int]$TimeoutSeconds)
    if ($DesktopHandle -eq [IntPtr]::Zero) {
        $script:DesktopHandle = [HarnessDesktop]::Create($DesktopName)
        Write-Summary "hidden desktop: WinSta0\$DesktopName"
    }
    $childEnvironment = @{}
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $childEnvironment[[string]$entry.Key] = [string]$entry.Value }
    foreach ($k in $Environment.Keys) { if ($null -eq $Environment[$k]) { $childEnvironment.Remove($k) } else { $childEnvironment[$k] = [string]$Environment[$k] } }
    $commandLine = (@($File) + $Arguments | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join ' '
    $started = Get-Date
    $code = [HarnessDesktop]::Run("WinSta0\$DesktopName", $File, $commandLine, $childEnvironment, $Repo, $OutputPath, $TimeoutSeconds * 1000)
    $text = if (Test-Path -LiteralPath $OutputPath) { Get-Content -LiteralPath $OutputPath -Raw -Encoding utf8 } else { '' }
    if ($null -eq $text) { $text = '' }
    $envText = ($Environment.Keys | Sort-Object | ForEach-Object { if ($null -eq $Environment[$_]) { "-$_" } else { "$_=$($Environment[$_])" } }) -join ' '
    Add-Content -LiteralPath $CommandLog -Value ('{0:o} exit={1} desktop=WinSta0\{2} [{3}] {4}' -f $started, $code, $DesktopName, $envText, $commandLine) -Encoding utf8
    [pscustomobject]@{ ExitCode = $code; Output = $text; TimedOut = ($code -eq -1); Duration = (Get-Date) - $started }
}

$BuildWithSdk = { param([string]$Target = 'Build', [string]$Name = 'solution') Invoke-Build -Target $Target -Name $Name -Environment (Get-SdkEnvironment $true) | Out-Null }

Write-Summary ("verify suite={0} configuration={1} filter='{2}' mode={3} expectFailure={4}" -f $Suite, $Configuration, $Filter, $Mode, [bool]$ExpectFailure)

try {
switch ($Suite) {
    'BuildContracts' {
        Test-MissingSdk
        Invoke-Build -Target 'ThirdDigital_ProjectWise_Themes_Tests:Rebuild' -Name 'tests-without-sdk' -Environment (Get-SdkEnvironment $false) | Out-Null
        Write-Summary 'check TestsBuildWithoutSdk: pass'
        $full = Invoke-Build -Target 'Rebuild' -Name 'rebuild' -Environment (Get-SdkEnvironment $true)
        Test-CompilerFlags $full.Content
        Test-X86Artifacts
        Test-SingleExport
        Invoke-HarnessChecks -Kind 'Native' -Exe $NativeHarness -Group 'Smoke'
        Invoke-HarnessChecks -Kind 'WinForms' -Exe $WinFormsHarness -Group 'Smoke'
    }
    'Build' {
        & $BuildWithSdk
        Test-SingleExport
    }
    'Unit' {
        Invoke-Unit $Filter
    }
    'Native' {
        & $BuildWithSdk
        Invoke-HarnessChecks -Kind 'Native' -Exe $NativeHarness -Group $Filter
    }
    'Picker' {
        Invoke-Unit 'ThemeMenu'
        & $BuildWithSdk
        Test-SingleExport
        Test-PickerImports
    }
    'WinForms' {
        & $BuildWithSdk
        Invoke-HarnessChecks -Kind 'WinForms' -Exe $WinFormsHarness -Group $Filter
    }
    'ContextMenus' {
        & $BuildWithSdk
        Write-Environment
        Invoke-HarnessChecks -Kind 'Native' -Exe $NativeHarness -Group 'ContextMenus'
    }
    'Regression' {
        Invoke-Unit ''
        & $BuildWithSdk
        Test-SingleExport
        Test-X86Artifacts
        Invoke-HarnessChecks -Kind 'Native' -Exe $NativeHarness -Group ''
        Invoke-HarnessChecks -Kind 'WinForms' -Exe $WinFormsHarness -Group ''
    }
    'Full' {
        Write-Environment
        Test-MissingSdk
        Invoke-Build -Target 'ThirdDigital_ProjectWise_Themes_Tests:Rebuild' -Name 'tests-without-sdk' -Environment (Get-SdkEnvironment $false) | Out-Null
        Write-Summary 'check TestsBuildWithoutSdk: pass'
        $full = Invoke-Build -Target 'Rebuild' -Name 'rebuild' -Environment (Get-SdkEnvironment $true)
        Test-CompilerFlags $full.Content
        Test-X86Artifacts
        Test-SingleExport
        Test-PickerImports
        Invoke-Unit ''
        Invoke-HarnessChecks -Kind 'Native' -Exe $NativeHarness -Group ''
        Invoke-HarnessChecks -Kind 'WinForms' -Exe $WinFormsHarness -Group ''
    }
}

}
finally {
    [HarnessDesktop]::Close($DesktopHandle)
}

Write-Summary "verify suite=$Suite configuration=${Configuration}: complete; evidence in $Out"
exit 0
