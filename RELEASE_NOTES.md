# ProjectWise Themes v0.1.0

**Work in progress.** Application testing has only covered **Windows 11 Pro 26H2 (OS build 26300.9457)** with **ProjectWise Explorer CONNECT Edition 10.00.03.434 (32-bit)**. Compatibility with other Windows or ProjectWise Explorer versions is not guaranteed. Installer acceptance of other 10.00.03 builds, including .453, is not evidence of application compatibility.

First release of Third Digital's colour themes for ProjectWise Explorer CONNECT Edition 10.00.03 (32-bit).

The presets are inspired by Windows Terminal colour schemes. A custom theme editor is planned for a future release; it is not included in v0.1.0.

- Nine colour schemes, plus None to restore the standard appearance.
- Live switching from Tools > Colour Theme, with per-user preferences.
- Themed Explorer trees, lists, menus, native document properties, Access Control, Audit Trail and Dependency Viewer.
- Third Digital branded x86 Windows Installer package, including registration, repair and removal.

Close ProjectWise Explorer, install the MSI as an administrator, then reopen Explorer. The Microsoft Visual C++ v14 Redistributable (x86), version 14.44.35207 or newer, is required. The installer is unsigned.

WPF and Chromium panes are not themed. Some Windows accents and third-party controls retain their own colours. See the README for coverage. This is an early release; test in your environment before wider deployment.

## Third-party notices

The Windows Terminal palette data is used under the MIT licence. Download the accompanying `THIRD-PARTY-NOTICES.txt` release asset for the Microsoft copyright and full licence, plus the MinHook/HDE notices. Keep that notice with the v0.1.0 installer when redistributing it: the original MSI predates the attribution correction and does not embed this file. The original MSI and its checksum are unchanged; new installer builds include the notice.
