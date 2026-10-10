# Contributing

ProjectWise Themes is a work in progress. Application testing has only covered **Windows 11 Pro 26H2 (OS build 26300.9457)** and **ProjectWise Explorer CONNECT Edition 10.00.03.434 (32-bit)**. Compatibility with other Windows or ProjectWise Explorer versions is not guaranteed. Reports from other environments are welcome; installer acceptance alone does not establish compatibility.

## Reporting issues

Search [existing issues](https://github.com/Third-Digital/ThirdDigital.ProjectWise.Themes/issues) before opening a bug report or feature request. Include the module version, full Windows and ProjectWise versions, selected theme, affected window/tab, reproduction steps and expected behaviour. For a visual problem, a small screenshot is especially useful. Explain whether **None** restores the original appearance.

Remove private datasource URLs, project/document names, account details and credentials from screenshots and logs. Do not attach Bentley SDK files or conversation transcripts. While the repository is private, issue and pull request access is limited to people with repository access.

## Making a change

1. Create a focused branch from current `main`. If you have access through a fork, clone it with `--recurse-submodules`; otherwise use an authorised branch in this repository.
2. Follow the existing C++20 style and keep theming scoped to supported ProjectWise windows. Preserve document data, semantic colours, icons and control behaviour. Returning to **None** must restore the normal appearance.
3. Add a regression check when fixing behaviour that could break again. Avoid unrelated formatting changes and generated files.
4. Run the relevant checks below and open a pull request against `main`. Describe the final change, its validation and any limitations. Keep commits logically grouped; the repository uses squash merges.

## Building and checking

See the [README](README.md#building) for Visual Studio and SDK prerequisites. The proprietary Bentley SDK is not distributed here.

Unit tests do not require that SDK. From PowerShell 7.2 or later on Windows with Visual Studio 2022:

```powershell
./ThirdDigital.ProjectWise.Themes.Harness/Verify.ps1 -Suite Unit -Configuration Release -ResultsRoot "$env:TEMP/pw-themes-verification"
./build/Check-Repository.ps1
```

For changes to hooks, drawing or lifecycle behaviour, run the full suite on a machine with the licensed SDK and the required .NET Framework targeting pack:

```powershell
./ThirdDigital.ProjectWise.Themes.Harness/Verify.ps1 -Suite Full -Configuration Release -ResultsRoot "$env:TEMP/pw-themes-verification" -PWSDKDir 'C:\path\to\ProjectWise SDK'
```

Visual changes also need checks in Explorer: the affected page, dark and light themes, live switching, newly opened windows and **None**. Use a test environment and sanitise evidence. Passing the synthetic harness does not prove compatibility with every ProjectWise build.

## Review and merge

Pull requests run repository validation, unit tests and native harness compilation on GitHub-hosted Windows runners. These jobs do not have the licensed Bentley SDK or a real Explorer installation; describe the additional local verification performed.

`main` requires passing checks against the current base, one approving review, and resolved review conversations. New commits dismiss old approvals. Force pushes and deletion of `main` are blocked, including for administrators. A different maintainer must review your own pull request. Use squash merge to keep history focused.

For packaging and releases, see [RELEASING.md](RELEASING.md). Keep source and release assets suitable for eventual public sharing, even while the repository remains private. The project uses the [MIT licence](LICENSE); do not contribute code or assets you are not entitled to share.
