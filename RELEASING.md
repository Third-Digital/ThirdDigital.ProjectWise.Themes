# Releasing

Release builds run on Windows with Visual Studio 2022, PowerShell 7.2+, the .NET 8 SDK (or later), the .NET Framework 4.6.2 targeting pack, and a separately licensed ProjectWise SDK 10.00.03. WiX 4.0.6 and its extensions restore through NuGet. Public CI runs unit tests and compiles the native harness without Bentley's SDK; a release additionally requires the full native and WinForms verification suite on Windows.

1. Update both version values in `Version.props` and the user-facing `RELEASE_NOTES.md`. MSI versions use three numeric fields; increment the version for each release. Keep the installer UpgradeCode and component GUIDs stable.
2. Commit the final source on `main`. Close any Explorer process using the development output DLL before rebuilding.
3. From PowerShell, run `./build/Build-Release.ps1 -PWSDKDir 'C:\path\to\ProjectWise SDK'`. This verifies the module, checks its version and builds the branded x86 MSI. `-SkipTests` is available for packaging development only; such output cannot be published by the release script.
4. Inspect the MSI and test install, repair, upgrade and removal on a disposable Windows machine with the supported ProjectWise installation. Check all theme choices, including None. Do not run destructive installer tests against an active work session.
5. Audit the files and commits to be pushed. Keep transcripts, plans, logs, screenshots, datasource demos, credentials, SDK files and generated debug symbols outside published history and release assets. Commit real source, tests and user/developer documentation only. Ignore rules cannot remove secrets from existing commits.
6. Push only the reviewed `main` branch to `Third-Digital/ThirdDigital.ProjectWise.Themes`. Do not push development recovery branches or use `--mirror`/`--all`.
7. Run `./build/Publish-Release.ps1`. It requires clean `main`, a matching fully tested build, and a matching remote head. It tags the revision and publishes only the MSI and SHA-256 checksum file. Build evidence and test logs remain local.

The initial package is unsigned. A production signing service can sign the module before packaging and the MSI after packaging; recalculate checksums and evidence after signing. Never commit signing keys or passwords. Windows may show an unknown-publisher warning for unsigned installers.

The installer uses Windows Installer transactions and major upgrades, registers the module in the 32-bit ProjectWise registry view and leaves user theme preferences intact on removal. If it replaces a manual development registration, uninstall removes that registration; it does not restore the previous path. Preserve any development registration separately before migration.
