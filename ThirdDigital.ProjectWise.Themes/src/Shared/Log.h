#pragma once

// printf-style; written to OutputDebugStringW and appended as UTF-8 to %TEMP%\ThirdDigital\projectwise-themes.log.
// Never pass document or user data. Failures are ignored.
void Log(const wchar_t* format, ...) noexcept;
