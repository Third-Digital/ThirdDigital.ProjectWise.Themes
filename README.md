# ThirdDigital.ProjectWise.Themes

Colour themes for Bentley ProjectWise Explorer, based on the standard Windows Terminal colour schemes.

Loaded by ProjectWise Explorer as a custom module. Not affiliated with or endorsed by Bentley Systems.

## Building

Requires Visual Studio 2022 with the "Desktop development with C++" workload (v143 toolset).

```
msbuild ThirdDigital.ProjectWise.Themes.sln -p:Configuration=Release -p:Platform=x86
```

Supports ProjectWise Explorer CONNECT Edition 10.00.03 (32-bit).

## Licence

MIT. See [LICENSE](LICENSE).
