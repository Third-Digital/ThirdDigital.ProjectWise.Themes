# ProjectWise Themes v0.1.1

**Work in progress.** Application testing has only covered **Windows 11 Pro 26H2 (OS build 26300.9457)** with **ProjectWise Explorer CONNECT Edition 10.00.03.434 (32-bit)**. Compatibility with other Windows or ProjectWise Explorer versions is not guaranteed. Installer acceptance of other 10.00.03 builds, including .453, is not evidence of application compatibility.

Maintenance release of Third Digital's colour themes for ProjectWise Explorer CONNECT Edition 10.00.03 (32-bit).

- The Document Export Wizard's heading, description and option captions follow the selected theme. Previously they kept black text on a light background under dark schemes.
- Radio button and check box captions in ProjectWise dialogs use the theme's text colour, and the theme's disabled text colour when unavailable.
- The installer now includes `THIRD-PARTY-NOTICES.txt`.

Close ProjectWise Explorer, install the MSI as an administrator, then reopen Explorer. The installer upgrades v0.1.0 in place and keeps theme preferences. The Microsoft Visual C++ v14 Redistributable (x86), version 14.44.35207 or newer, is required. The installer is unsigned.

WPF and Chromium panes are not themed. Some Windows accents and third-party controls retain their own colours. See the README for coverage. This is an early release; test in your environment before wider deployment.

## Third-party notices

The Windows Terminal palette data is used under the MIT licence. The installed `THIRD-PARTY-NOTICES.txt` contains the Microsoft copyright and full licence, plus the MinHook/HDE notices.
